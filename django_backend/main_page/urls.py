from django.urls import re_path
from rest_framework.urlpatterns import format_suffix_patterns
from . import views

app_name = 'main_page'

urlpatterns = [
    re_path(r'^tasks/$', views.task_list, name='tasks'),
    re_path(r'^control/$', views.motor_control_list, name='control'),
    re_path(r'^login/$', views.login, name='login'),
    re_path(r'^signup/$', views.sign_up, name='signup'),
    re_path(r'^token_validation/$', views.token_validation, name='token_validation'),
    re_path(r'^user_data/$', views.get_user_data, name='user_data'),
    re_path(r'^change_password/$', views.change_password, name='change_password'),
    re_path(r'^get_motors/$', views.get_motors, name='get_motors'),
    re_path(r'^test/$', views.test, name='test'),
    re_path(r'^spinning/$', views.spinning, name='spinning'),
    re_path(r'^spinning/cancel/$', views.spinning_cancel, name='spinning_cancel'),
    re_path(r'^spinning/delete/$', views.spinning_delete, name='spinning_delete'),
    re_path(r'^spinning/clear/$', views.spinning_clear, name='spinning_clear'),
    re_path(r'^mqtt_msg/$', views.mqtt_msg, name='mqtt_msg'),
    re_path(r'^mqtt/reconnect/$', views.mqtt_reconnect, name='mqtt_reconnect'),

    # 设备管理（Dashboard 核心）
    re_path(r'^device_list/$', views.device_list, name='device_list'),
    re_path(r'^devices/$', views.device_register_list, name='device_register_list'),
    re_path(r'^devices/(?P<device_id>[^/]+)/$', views.device_register_detail, name='device_register_detail'),
    re_path(r'^devices/emergency_stop/$', views.device_emergency_stop, name='device_emergency_stop'),
    re_path(r'^devices/resume/$', views.device_resume, name='device_resume'),
    re_path(r'^devices/acknowledge/$', views.device_acknowledge, name='device_acknowledge'),
    re_path(r'^devices/dispatch_task/$', views.device_dispatch_task, name='device_dispatch_task'),
    re_path(r'^devices/dispatch_batch/$', views.device_dispatch_batch, name='device_dispatch_batch'),
    re_path(r'^devices/emergency_stop_log/$', views.emergency_stop_log_list, name='emergency_stop_log_list'),

    re_path(r'^experiments/$', views.experiment_process_list, name='experiments'),
    re_path(r'^experiments/(?P<experiment_id>[^/]+)/$', views.experiment_process_detail, name='experiment_detail'),
]

urlpatterns = format_suffix_patterns(urlpatterns)
