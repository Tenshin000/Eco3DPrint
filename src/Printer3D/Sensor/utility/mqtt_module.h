#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include "contiki.h"
#include <stdbool.h>

// Configuration Macros
#define MQTT_BROKER_IP "fd00::1"
#define MQTT_BROKER_PORT 1883

// Custom event exposed to the main process
extern process_event_t event_mqtt_retry;

/* Initialize the MQTT module with the main process and device name */
void sensor_mqtt_init(struct process *main_proc, const char* device_name);

/* Handle connection and disconnection */
void sensor_mqtt_connect(void);
void sensor_mqtt_disconnect(void);

/* Check if the client is currently connected to the broker */
bool sensor_mqtt_is_connected(void);

/* Publish a JSON payload to a specific topic (QoS 1) */
void sensor_mqtt_publish(const char* topic, const char* json_payload);

/* Hook back to the main sensor process to trigger a reconnection */
extern void sensor_trigger_mqtt_retry(void);

#endif /* MQTT_MODULE_H */