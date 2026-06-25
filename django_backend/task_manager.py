import sqlite3, time, threading
from datetime import datetime, timedelta
from paho.mqtt import client as mqtt


def _device_control_topic(device_id):
    """根据 device_id 生成对应的 MQTT 控制 topic。"""
    if device_id.startswith('esp32_') and len(device_id) == 6 + 12:
        return f"esp32/{device_id[6:]}/control"
    return f"{device_id}/control"

# 让独立进程能够读取 Django settings.py 中的 MQTT 配置
import os
import sys
import django

# 将 django_backend 目录加入路径，以便导入 settings
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, BASE_DIR)
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'django_backend.settings')
try:
    django.setup()
    from django.conf import settings
    MQTT_SERVER = getattr(settings, 'MQTT_SERVER', '127.0.0.1')
    MQTT_PORT = getattr(settings, 'MQTT_PORT', 1883)
    MQTT_KEEPALIVE = getattr(settings, 'MQTT_KEEPALIVE', 60)
except Exception as exc:
    print(f'Warning: unable to load Django settings ({exc}), falling back to defaults.')
    MQTT_SERVER = '127.0.0.1'
    MQTT_PORT = 1883
    MQTT_KEEPALIVE = 60

task_finished = False


# MQTT 相关组件
def mqtt_on_connect(mqtt_client, userdata, flags, rc):
    if rc == 0:
        print("MQTT Connect Success!")
        mqtt_client.subscribe('task_manager')
    else:
        print("Bad Connection Code: ", rc)


def mqtt_on_disconnect(mqtt_client, userdata, rc):
    print(f"Task Manager MQTT disconnected (rc={rc}). Will rely on paho auto-reconnect.")


def mqtt_on_message(mqtt_client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()
    print(f'[{topic}]: {payload}')
    if payload == 'Task Finished.':
        global task_finished
        task_finished = True


class Task_Manager():
    def __init__(self):
        self.conn = None
        self.c = None
        self.first_task = None
        self.terminate = False
        self.task_triggered = False
        self.mqtt_client = None

    # 整体控制初始化
    def manager_init(self):
        self.mqtt_init()
        threading.Thread(target=self.update_thread).start()
        threading.Thread(target=self.timer_thread).start()
        threading.Thread(target=self.mqtt_heartbeat).start()
        self.user_input()

    # 数据库相关组件
    def database_connect(self):
        self.conn = sqlite3.connect('db.sqlite3')
        self.c = self.conn.cursor()

    def database_disconnect(self):
        self.conn.close()

    def update_time_schedule(self):
        local_first_task = None
        self.c.execute('select COUNT(*) from main_page_spinning')
        row_num = self.c.fetchone()
        results = self.c.execute('select * from main_page_spinning')
        # 监测库中是否有未完成项目
        if row_num[0] > 0:
            for row in results:
                if local_first_task == None:
                    local_first_task = row
                elif datetime.strptime(local_first_task[2], '%Y-%m-%d %H:%M:%S') > datetime.strptime(row[2],
                                                                                                     '%Y-%m-%d %H:%M:%S'):
                    local_first_task = row
            # 将UTC转化成UTC+8
            UTC = datetime.strptime(local_first_task[2], '%Y-%m-%d %H:%M:%S')
            UTC_8 = UTC + timedelta(hours=8)
            # 将tuple类型转化成list类型方便修改
            tmp = local_first_task
            local_first_task = []
            for item in tmp:
                local_first_task.append(item)
            # 修改时间参数
            local_first_task[2] = UTC_8.strftime('%Y-%m-%d %H:%M:%S')
            if self.first_task == None:
                self.first_task = local_first_task
                self.task_triggered = False
                record_update = 'Updated First Task Detial: \n' + str(self.first_task)
                print(record_update)
                self.mqtt_client.publish('task_manager', record_update)
            else:
                if datetime.strptime(self.first_task[2], '%Y-%m-%d %H:%M:%S') != UTC_8:
                    self.first_task = local_first_task
                    self.task_triggered = False
                    record_update = 'Updated First Task Detial: \n' + str(self.first_task)
                    print(record_update)
                    self.mqtt_client.publish('task_manager', record_update)
        else:
            self.first_task = None

    # 更新数据线程
    def update_thread(self):

        while not self.terminate:
            self.database_connect()
            # 监测任务是否完成
            global task_finished
            if task_finished == True:
                sql_cmd = 'delete from main_page_spinning where id=' + str(self.first_task[0]) + ';'
                self.c.execute(sql_cmd)
                self.conn.commit()
                task_finished = False
            self.update_time_schedule()
            self.database_disconnect()
            time.sleep(1)

    # 触发器线程
    def timer_thread(self):
        while not self.terminate:
            time.sleep(0.1)
            if self.first_task == None:
                pass
            else:
                if (datetime.now() > datetime.strptime(self.first_task[2],
                                                       '%Y-%m-%d %H:%M:%S')) and not self.task_triggered:
                    timer_trigger = 'Timer at ' + str(self.first_task[2]) + ' has be triggered. Current time is ' + str(
                        datetime.now())
                    print(timer_trigger)
                    self.mqtt_client.publish('task_manager', timer_trigger)
                    self.task_triggered = True

                    # 统一使用 esp32/<mac>/control（与 Django 主进程一致）
                    default_device_id = getattr(settings, 'MQTT_DEFAULT_DEVICE_ID', 'esp32_1')

                    # 检查后端 MQTT 客户端与目标设备状态
                    if not self.mqtt_client.is_connected():
                        error_msg = 'Task Manager MQTT client is not connected. Skip dispatch.'
                        print(error_msg)
                        self.mqtt_client.publish('task_manager', error_msg)
                        continue

                    ready, reason = self._check_target_device_ready(default_device_id)
                    if not ready:
                        error_msg = f'Skip dispatch to {default_device_id}: {reason}'
                        print(error_msg)
                        self.mqtt_client.publish('task_manager', error_msg)
                        continue

                    cmd = 'cmd_' + str(self.first_task[3]) + '_' + str(self.first_task[4]) + '_0'
                    self.mqtt_client.publish(_device_control_topic(default_device_id), cmd)

    # MQTT 初始化方法
    def mqtt_init(self):
        self.mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)
        self.mqtt_client.on_connect = mqtt_on_connect
        self.mqtt_client.on_disconnect = mqtt_on_disconnect
        self.mqtt_client.on_message = mqtt_on_message
        self.mqtt_client.username_pw_set('Task_Manager_py', '123456')
        self.mqtt_client.connect(
            host=MQTT_SERVER,
            port=MQTT_PORT,
            keepalive=MQTT_KEEPALIVE
        )

    def _check_target_device_ready(self, device_id):
        """查询 Django Device 表，确认目标设备在线且空闲。"""
        try:
            conn = sqlite3.connect('db.sqlite3')
            cursor = conn.cursor()
            cursor.execute(
                'SELECT is_online, task_status FROM main_page_device WHERE device_id = ?',
                (device_id,)
            )
            row = cursor.fetchone()
            conn.close()
            if not row:
                return False, 'Device not registered'
            is_online, task_status = row
            if not is_online:
                return False, 'Device is offline'
            if task_status != 'idle':
                return False, f'Device status is {task_status}'
            return True, ''
        except Exception as exc:
            return False, f'Failed to query device status: {exc}'

    # MQTT心跳，告知客户端连接状态
    def mqtt_heartbeat(self):
        self.mqtt_client.loop_start()
        while not self.terminate:
            self.mqtt_client.publish('heartbeat/task_manager', 'Status: Alive')
            time.sleep(10)
        self.mqtt_client.loop_stop()

    # 用户操作界面
    def user_input(self):
        while not self.terminate:
            usr_in = input('What do you want?\n')
            if usr_in == 'quit()':
                self.terminate = True
            elif usr_in == 'get_first_task':
                print(self.first_task)


if __name__ == '__main__':
    tm = Task_Manager()
    tm.manager_init()
