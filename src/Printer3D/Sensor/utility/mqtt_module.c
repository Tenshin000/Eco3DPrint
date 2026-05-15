#include "mqtt_module.h"

#include "mqtt.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "Sensor MQTT"
#define LOG_LEVEL LOG_LEVEL_APP

static struct mqtt_connection conn;
static bool mqtt_connected = false;
static struct process *app_process = NULL;

process_event_t event_mqtt_retry;

/* ==================================================== */
/* =              MQTT EVENT CALLBACK                 = */
/* ==================================================== */
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data){
  switch(event){
    case MQTT_EVENT_CONNECTED:
      mqtt_connected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      if(mqtt_connected){
        LOG_WARN("MQTT Disconnected\n");
        mqtt_connected = false;
      }
      sensor_trigger_mqtt_retry();
      break;
    case MQTT_EVENT_PUBLISH:
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
void sensor_mqtt_init(struct process *main_proc, const char* device_name){
  app_process = main_proc;
  
  // FIX: Force reset of static state variables to survive PROCESS_EXIT()
  mqtt_connected = false;
  
  // FIX: Ensure events are allocated only once to prevent ID exhaustion
  static bool events_allocated = false;
  if(!events_allocated) {
      event_mqtt_retry = process_alloc_event();
      events_allocated = true;
  }
  
  mqtt_register(&conn, app_process, (char*)device_name, mqtt_event, 1024);
}

void sensor_mqtt_connect(void){
  LOG_INFO("Attempting MQTT connection to %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
  mqtt_connect(&conn, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60 * 3, MQTT_CLEAN_SESSION_ON);
}

void sensor_mqtt_disconnect(void){
  if(mqtt_connected){
    mqtt_disconnect(&conn);
  }
}

bool sensor_mqtt_is_connected(void){
  return mqtt_connected;
}

void sensor_mqtt_publish(const char* topic, const char* json_payload){
  if(mqtt_connected && json_payload != NULL && topic != NULL)
    // GUARANTEED DELIVERY via QoS 1 to prevent packet dropping
    mqtt_publish(&conn, NULL, (char*)topic, (uint8_t *)json_payload, strlen(json_payload), MQTT_QOS_LEVEL_1, MQTT_RETAIN_OFF);
}