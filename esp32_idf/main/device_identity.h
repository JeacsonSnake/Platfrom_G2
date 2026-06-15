#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <stdint.h>

// MAC 地址字符串：12 位十六进制 + '\0'
#define DEVICE_MAC_STR_LEN      13
// device_id 字符串："esp32_" + 12 位 MAC + '\0'
#define DEVICE_ID_LEN           32
// MQTT client_id 字符串："ESP32S3_" + 12 位 MAC + '\0'
#define MQTT_CLIENT_ID_LEN      32
// MQTT topic 字符串长度
#define MQTT_TOPIC_LEN          64

// 全局变量
extern char device_mac_str[DEVICE_MAC_STR_LEN];
extern char device_id[DEVICE_ID_LEN];
extern char mqtt_client_id[MQTT_CLIENT_ID_LEN];

extern char mqtt_control_topic[MQTT_TOPIC_LEN];
extern char mqtt_heartbeat_topic[MQTT_TOPIC_LEN];
extern char mqtt_telemetry_topic[MQTT_TOPIC_LEN];
extern char mqtt_task_topic[MQTT_TOPIC_LEN];

// 初始化函数：从 ESP32 底层读取 MAC，生成 device_id 与 MQTT topic
void device_identity_init(void);

#endif // DEVICE_IDENTITY_H
