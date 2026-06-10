import json
from channels.generic.websocket import AsyncWebsocketConsumer
from asgiref.sync import sync_to_async

# 导入同步的 MQTT 操作函数
from main_page.mqtt import emergency_stop, resume_devices, dispatch_motor_task, get_device_states


class MyConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        await self.channel_layer.group_add('mqtt_group', self.channel_name)
        await self.accept()
        # 连接成功后立即推送一次设备状态快照
        await self.send_snapshot()

    async def disconnect(self, close_code):
        await self.channel_layer.group_discard('mqtt_group', self.channel_name)

    async def receive(self, text_data=None):
        """接收前端通过 WebSocket 发送的指令。"""
        if text_data is None:
            return
        try:
            data = json.loads(text_data)
        except json.JSONDecodeError:
            await self.send(text_data=json.dumps({'error': 'Invalid JSON'}))
            return

        action = data.get('action')
        if action == 'emergency_stop':
            await self.handle_emergency_stop(data)
        elif action == 'resume_device':
            await self.handle_resume_device(data)
        elif action == 'dispatch_task':
            await self.handle_dispatch_task(data)
        elif action == 'get_snapshot':
            await self.send_snapshot()
        else:
            await self.send(text_data=json.dumps({'error': f'Unknown action: {action}'}))

    async def handle_emergency_stop(self, data):
        device_ids = data.get('device_ids', [])
        scope = data.get('scope', 'single')
        reason = data.get('reason', '')
        triggered_by = data.get('triggered_by', 'websocket_user')

        if scope == 'broadcast':
            device_ids = await self._get_all_registered_device_ids()

        if not device_ids:
            await self.send(text_data=json.dumps({
                'topic': 'estop_result',
                'success': False,
                'error': 'No target devices specified.'
            }))
            return

        results = await sync_to_async(emergency_stop, thread_sensitive=False)(
            device_ids,
            triggered_by=triggered_by,
            reason=reason,
            scope=scope
        )
        await self.send(text_data=json.dumps({
            'topic': 'estop_result',
            'scope': scope,
            'results': results,
        }))

    async def handle_resume_device(self, data):
        device_ids = data.get('device_ids', [])
        resumed_by = data.get('resumed_by', 'websocket_user')
        if not device_ids:
            await self.send(text_data=json.dumps({
                'topic': 'resume_result',
                'success': False,
                'error': 'No target devices specified.'
            }))
            return

        results = await sync_to_async(resume_devices, thread_sensitive=False)(
            device_ids,
            resumed_by=resumed_by
        )
        await self.send(text_data=json.dumps({
            'topic': 'resume_result',
            'results': results,
        }))

    async def handle_dispatch_task(self, data):
        device_id = data.get('device_id')
        motor = int(data.get('motor', 0))
        speed = int(data.get('speed', 0))
        duration = int(data.get('duration', 0))

        if not device_id:
            await self.send(text_data=json.dumps({
                'topic': 'dispatch_result',
                'success': False,
                'error': 'device_id is required.'
            }))
            return

        result = await sync_to_async(dispatch_motor_task, thread_sensitive=False)(
            device_id, motor, speed, duration
        )
        await self.send(text_data=json.dumps({
            'topic': 'dispatch_result',
            **result,
        }))

    async def send_snapshot(self):
        """向前端发送当前所有设备的状态快照。"""
        states = await sync_to_async(get_device_states, thread_sensitive=False)()
        # datetime 需要序列化
        serializable = {}
        for device_id, state in states.items():
            serializable[device_id] = {
                **state,
                'last_heartbeat': state.get('last_heartbeat').isoformat() if state.get('last_heartbeat') else None,
            }
        await self.send(text_data=json.dumps({
            'type': 'mqtt_msg_broadcast',
            'topic': 'device_snapshot',
            'timestamp': '',  # 前端收到后自行填充
            'payload': serializable,
        }))

    async def _get_all_registered_device_ids(self):
        """异步获取所有已注册设备的 device_id 列表。"""
        from main_page.models import Device
        return await sync_to_async(list, thread_sensitive=False)(
            Device.objects.filter(is_registered=True).values_list('device_id', flat=True)
        )

    async def mqtt_msg_broadcast(self, event):
        """接收来自 channel_layer 的 MQTT 广播消息并转发给前端。"""
        await self.send(text_data=json.dumps(event))
