import logging
import threading
import time
from datetime import timedelta

from django.conf import settings
from django.db import close_old_connections
from django.utils import timezone

from .models import Motor, Spinning
from .mqtt import dispatch_motor_task

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
                cls._process_completed_tasks()
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
        """尝试下发单条任务，并在数据库中记录结果。"""
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
            motor = Motor.objects.filter(name=task.motor_name).first()
            if motor is None:
                raise ValueError(f'Motor "{task.motor_name}" not found')

            device_id = task.device_id or getattr(
                settings, 'MQTT_DEFAULT_DEVICE_ID', 'esp32_1'
            )

            result = dispatch_motor_task(
                device_id,
                motor.motor_index,
                task.motor_speed,
                task.duration_sec,
            )

            if result.get('success'):
                Spinning.objects.filter(id=task.id).update(
                    status='SENT',
                    dispatched_at=timezone.now(),
                    error_message='',
                    updated_at=timezone.now(),
                )
                print(f"Dispatched scheduled task {task.id}: {result.get('command')}")
            else:
                raise RuntimeError(
                    result.get('error', 'Unknown dispatch error')
                )
        except Exception as exc:
            error_text = str(exc)[:256]
            Spinning.objects.filter(id=task.id).update(
                status='FAILED',
                error_message=error_text,
                updated_at=timezone.now(),
            )
            print(f"Failed to dispatch scheduled task {task.id}: {exc}")

    @classmethod
    def _process_completed_tasks(cls):
        """将已运行到期的 SENT 任务标记为 COMPLETED。"""
        now = timezone.now()
        sent_tasks = Spinning.objects.filter(status='SENT')
        for task in sent_tasks:
            finish_time = task.scheduled_time + timedelta(seconds=task.duration_sec)
            if now >= finish_time:
                Spinning.objects.filter(id=task.id).update(
                    status='COMPLETED',
                    completed_at=now,
                    updated_at=now,
                )
                print(f"Scheduled task {task.id} marked COMPLETED")
