#include "mqtt.h"
#include "mqtt_module.h"
#include "device_conf.h"

#include <string.h>
#include "sys/log.h"

// MQTT Log Module
#define LOG_MODULE "MQTT Module"
#define LOG_LEVEL LOG_LEVEL_APP

// Connection and state variables
static struct mqtt_connection conn;
static bool mqtt_connected = false;
static struct process *app_process = NULL;

// Process events for MQTT state changes and incoming data
process_event_t event_mqtt_retry;
process_event_t event_mqtt_incoming;

// Single buffer restored to prevent BSS memory overflow
static char incoming_buffer[1024]; 
static uint16_t in_pub_offset = 0;

/* ==================================================== */
/* =              MQTT EVENT CALLBACK                 = */
/* ==================================================== */
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data){
  switch(event){
    case MQTT_EVENT_CONNECTED:
      // Mark as connected upon successful broker connection
      mqtt_connected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      if(mqtt_connected){
        LOG_WARN("MQTT Disconnected\n");
        mqtt_connected = false;
      }
      in_pub_offset = 0; 
      // Post an event to the main process to handle potential reconnection via hooks
      device_trigger_mqtt_retry();
      break;
    case MQTT_EVENT_PUBLISH: {
      struct mqtt_message *msg = (struct mqtt_message *)data;
      
      uint8_t *chunk_ptr = msg->payload_chunk;
      uint16_t chunk_len = msg->payload_chunk_length;
      
      if (in_pub_offset + chunk_len < 1024){
          memcpy(incoming_buffer + in_pub_offset, chunk_ptr, chunk_len);
          in_pub_offset += chunk_len;
          incoming_buffer[in_pub_offset] = '\0';
          
          // Check for the end of the JSON array (closed square bracket)
          if (in_pub_offset > 10 && incoming_buffer[in_pub_offset - 1] == ']'){
              
              // Send the pointer to the pristine string
              process_post(app_process, event_mqtt_incoming, incoming_buffer);
              in_pub_offset = 0; 
          }
      } 
      else{
          LOG_ERR("MQTT buffer overflow! Dropping corrupted fragments.\n");
          in_pub_offset = 0; 
      }
      break;
    }
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
// Initialize the MQTT module with the main process and device name
void mqtt_module_init(struct process *main_proc, const char* device_name){
  app_process = main_proc;
  
  // Force reset of static state variables. 
  // If the process is killed via Hard Reset, the static variables retain their old 
  // memory values. We must clean them up explicitly to allow reconnecting.
  mqtt_connected = false;
  in_pub_offset = 0;
  
  // Ensure events are allocated only once. 
  // Contiki limits process_alloc_event() IDs (max 255). Without this flag, 
  // multiple Hard Resets would leak IDs and eventually crash the OS.
  static bool events_allocated = false;
  if(!events_allocated){
      event_mqtt_retry = process_alloc_event();
      event_mqtt_incoming = process_alloc_event();
      events_allocated = true;
  }
  
  mqtt_register(&conn, app_process, (char*)device_name, mqtt_event, 1024);
}

// Attempt connection to the MQTT broker
void mqtt_module_connect(void){
  mqtt_connect(&conn, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60 * 3, MQTT_CLEAN_SESSION_ON);
}

// Disconnect from the MQTT broker safely
void mqtt_module_disconnect(void){
  if(mqtt_connected){
    mqtt_disconnect(&conn);
  }
}

// Check if the client is currently connected to the broker
bool mqtt_module_is_connected(void){
  return mqtt_connected;
}

// Subscribe to a specific MQTT topic
void mqtt_module_subscribe(const char* topic){
  if(mqtt_connected){
    // Using QoS 0 to prevent Mosquitto inflight window exhaustion
    mqtt_subscribe(&conn, NULL, (char*)topic, MQTT_QOS_LEVEL_0);
  }
}