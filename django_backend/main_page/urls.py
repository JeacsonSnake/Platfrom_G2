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
    re_path(r'^mqtt_msg/$', views.mqtt_msg, name='mqtt_msg'),
    re_path(r'^device_list/$', views.device_list, name='device_list'),
    re_path(r'^experiments/$', views.experiment_process_list, name='experiments'),
    re_path(r'^experiments/(?P<experiment_id>[^/]+)/$', views.experiment_process_detail, name='experiment_detail'),
]

urlpatterns = format_suffix_patterns(urlpatterns)
