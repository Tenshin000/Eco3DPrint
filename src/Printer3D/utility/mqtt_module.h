#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include "contiki.h"
#include <stdbool.h>

// Configuration Macros
#define MQTT_BROKER_IP "fd00::1"
#define MQTT_BROKER_PORT 1883
#define MQTT_PUB_TOPIC "/print/measurements"

// Custom event exposed to the main process
extern process_event_t event_mqtt_retry;

/* Initialize the MQTT module with the main process and device name */
void mqtt_module_init(struct process *main_proc, const char* device_name);

/* Handle connection and disconnection */
void mqtt_module_connect(void);
void mqtt_module_disconnect(void);

/* Check if the client is currently connected to the broker */
bool mqtt_module_is_connected(void);

/* Publish a JSON payload to the pre-configured topic */
void mqtt_module_publish(const char* json_payload);

#endif /* MQTT_MODULE_H */