from django.apps import AppConfig


class MyAppConfig(AppConfig):
    name = 'main_page'

    def ready(self):
        from . import mqtt
        if mqtt.client is not None:
            mqtt.client.loop_start()
