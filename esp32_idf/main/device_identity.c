#include "device_identity.h"

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_log.h"

char device_mac_str[DEVICE_MAC_STR_LEN] = {0};
char device_id[DEVICE_ID_LEN] = {0};
char mqtt_client_id[MQTT_CLIENT_ID_LEN] = {0};

char mqtt_control_topic[MQTT_TOPIC_LEN] = {0};
char mqtt_heartbeat_topic[MQTT_TOPIC_LEN] = {0};
char mqtt_telemetry_topic[MQTT_TOPIC_LEN] = {0};
char mqtt_task_topic[MQTT_TOPIC_LEN] = {0};

static const char* TAG = "DEVICE_IDENTITY";

void device_identity_init(void)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取 MAC 地址失败: %s", esp_err_to_name(ret));
        memset(mac, 0, sizeof(mac));
    }

    snprintf(device_mac_str, sizeof(device_mac_str),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(device_id, sizeof(device_id), "esp32_%s", device_mac_str);
    snprintf(mqtt_client_id, sizeof(mqtt_client_id), "ESP32S3_%s", device_mac_str);

    snprintf(mqtt_control_topic, sizeof(mqtt_control_topic), "esp32/%s/control", device_mac_str);
    snprintf(mqtt_heartbeat_topic, sizeof(mqtt_heartbeat_topic), "esp32/%s/heartbeat", device_mac_str);
    snprintf(mqtt_telemetry_topic, sizeof(mqtt_telemetry_topic), "esp32/%s/telemetry", device_mac_str);
    snprintf(mqtt_task_topic, sizeof(mqtt_task_topic), "esp32/%s/task", device_mac_str);

    ESP_LOGI(TAG, "MAC=%s, device_id=%s, client_id=%s",
             device_mac_str, device_id, mqtt_client_id);
    ESP_LOGI(TAG, "control=%s, heartbeat=%s, telemetry=%s, task=%s",
             mqtt_control_topic, mqtt_heartbeat_topic,
             mqtt_telemetry_topic, mqtt_task_topic);
}
