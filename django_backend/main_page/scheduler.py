import logging
import threading
import time

from django.db import close_old_connections
from django.utils import timezone

from .models import Motor, Spinning
from .mqtt import dispatch_motor_task, resolve_dispatchable_device_id

logger = logging.getLogger(__name__)

POLL_INTERVAL_SECONDS = 0.5


class SpinningScheduler:
    """Django 进程内守护线程：轮询 Spinning 表，到期后下发电机任务。"""

    _instance_lock = threading.Lock()
    _thread = None
    _stop_event = threading.Event()

    @classmethod
    def start(cls):
        """启动调度线程；重复调用不会创建多个线程。"""
        with cls._instance_lock:
            if cls._thread is not None and cls._thread.is_alive():
                logger.info('SpinningScheduler already running')
                return
            cls._stop_event.clear()
            cls._thread = threading.Thread(
                target=cls._run,
                daemon=True,
                name='SpinningScheduler',
            )
            cls._thread.start()
            print('SpinningScheduler started')

    @classmethod
    def stop(cls):
        """停止调度线程。"""
        cls._stop_event.set()
        if cls._thread is not None:
            cls._thread.join(timeout=2)

    @classmethod
    def _run(cls):
        while not cls._stop_event.is_set():
            close_old_connections()
            try:
                cls._process_due_tasks()
            except Exception as exc:
                logger.exception('SpinningScheduler error: %s', exc)
            time.sleep(POLL_INTERVAL_SECONDS)

    @classmethod
    def _process_due_tasks(cls):
        """处理已到期的 PENDING 任务。"""
        now = timezone.now()
        pending = Spinning.objects.filter(
            status='PENDING',
            scheduled_time__lte=now,
        ).order_by('scheduled_time')

        for task in pending:
            cls._dispatch_task(task)

    @classmethod
    def _dispatch_task(cls, task):
        """尝试下发单条任务，并在数据库中记录结果。

        支持多电机：一条 Spinning 记录可包含多个电机名称，依次向同一设备下发。
        """
        # 先原子地将状态从 PENDING 改为 SENDING，防止并发重复触发
        claimed = Spinning.objects.filter(
            id=task.id,
            status='PENDING',
        ).update(
            status='SENDING',
            updated_at=timezone.now(),
        )
        if claimed == 0:
            return

        task = Spinning.objects.get(id=task.id)

        try:
            motor_names = task.effective_motor_names()
            if not motor_names:
                raise ValueError('No motor assigned to this task')

            motors = list(Motor.objects.filter(name__in=motor_names))
            if len(motors) != len(motor_names):
                found_names = {m.name for m in motors}
                missing = [name for name in motor_names if name not in found_names]
                raise ValueError(f'Motor(s) not found: {", ".join(missing)}')

            # 使用首个电机的电机级可用性解析目标设备，确保同一设备下其他空闲电机仍可被选中
            device_id = resolve_dispatchable_device_id(task.device_id, motors[0].motor_index)

            dispatched_commands = []
            first_error = None
            for index, motor in enumerate(motors):
                # 多电机任务中，首个电机已通过 resolve_dispatchable_device_id 校验可用，
                # 后续电机直接发布命令，避免被设备级 busy 状态拦截。
                result = dispatch_motor_task(
                    device_id,
                    motor.motor_index,
                    task.motor_speed,
                    task.duration_sec,
                    check_dispatch=(index == 0),
                )
                if result.get('success'):
                    dispatched_commands.append(result.get('command'))
                else:
                    first_error = result.get('error', 'Unknown dispatch error')
                    break

            if not first_error:
                Spinning.objects.filter(id=task.id).update(
                    status='SENT',
                    dispatched_at=timezone.now(),
                    error_message='',
                    updated_at=timezone.now(),
                )
                print(f"Dispatched scheduled task {task.id}: {dispatched_commands}")
            else:
                raise RuntimeError(first_error)
        except Exception as exc:
            error_text = str(exc)[:256]
            Spinning.objects.filter(id=task.id).update(
                status='FAILED',
                error_message=error_text,
                updated_at=timezone.now(),
            )
            print(f"Failed to dispatch scheduled task {task.id}: {exc}")
