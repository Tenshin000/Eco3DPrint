#include "mqtt_module.h"

#include "device_conf.h"
#include "mqtt.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "MQTT Module"
#define LOG_LEVEL LOG_LEVEL_APP

static struct mqtt_connection conn;
static bool mqtt_connected = false;
static struct process *app_process = NULL;

process_event_t event_mqtt_retry;

/* ==================================================== */
/* =              MQTT EVENT CALLBACK                 = */
/* ==================================================== */
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data) {
  switch(event){
    case MQTT_EVENT_CONNECTED:
      // LOG_INFO("MQTT Connected to broker\n");
      mqtt_connected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      // Fix for the double print: check if it was actually connected before logging
      if(mqtt_connected){
        LOG_WARN("MQTT Disconnected\n");
        mqtt_connected = false;
      }
      // Post an event to the main process to handle potential reconnection via hooks
      device_trigger_mqtt_retry();
      break;
    case MQTT_EVENT_PUBLISH:
      // Fired when an incoming message is received, not used in this publisher-only node
      break;
    case MQTT_EVENT_SUBACK:
    case MQTT_EVENT_UNSUBACK:
    case MQTT_EVENT_PUBACK:
    default:
      break;
  }
}

/* ==================================================== */
/* =                MODULE FUNCTIONS                  = */
/* ==================================================== */
void mqtt_module_init(struct process *main_proc, const char* device_name){
  app_process = main_proc;
  event_mqtt_retry = process_alloc_event();
  mqtt_register(&conn, app_process, (char*)device_name, mqtt_event, 256);
}

void mqtt_module_connect(void){
  // LOG_INFO("Attempting MQTT connection to %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
  mqtt_connect(&conn, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60 * 3, MQTT_CLEAN_SESSION_ON);
}

void mqtt_module_disconnect(void){
  if(mqtt_connected){
    mqtt_disconnect(&conn);
  }
}

bool mqtt_module_is_connected(void){
  return mqtt_connected;
}

void mqtt_module_publish(const char* json_payload){
  if(mqtt_connected && json_payload != NULL)
    mqtt_publish(&conn, NULL, MQTT_PUB_TOPIC, (uint8_t *)json_payload, strlen(json_payload), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
}