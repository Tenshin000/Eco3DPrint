/* Contiki core */
#include "contiki.h"
#include "sys/log.h"
#include "sys/node-id.h"

/* Hardware */
#include "os/dev/button-hal.h"
#include "os/dev/leds.h"
#include "os/dev/serial-line.h"

/* Networking (IPv6 / uIP) */
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"

/* Application protocols */
#include "coap.h"
#include "coap-blocking-api.h"
#include "coap-engine.h"
#include "coap-transactions.h"
#include "mqtt.h"

/* Standard C libraries */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Project resources */
#include "utility/sensors.h"
#include "utility/scaler_params.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "utility/print_prediction.h"
#pragma GCC diagnostic pop

// Smart Printer Log
#define LOG_MODULE "Smart Printer"
#define LOG_LEVEL LOG_LEVEL_APP

// CoAP Configuration
#define CLOUD_SERVER_EP "coap://[fd00::1]:5683"
#define REG_URI_PATH "/registration"
#define END_PRINT_URI_PATH "/print/finished"
#define OFF_SIGNAL_URI_PATH "/signal/off"

// MQTT Configuration
#define MQTT_BROKER_IP "fd00::1"
#define MQTT_BROKER_PORT 1883
#define MQTT_PUB_TOPIC "/print/measurements"

/* ==================================================== */ 
/* =                  CONFIGURATION                   = */ 
/* ==================================================== */ 
// Device state machine 
typedef enum {
  STATE_OFF,
  STATE_INITIALIZATION,
  STATE_ONLINE,
  STATE_OFFLINE,
  STATE_PRINTING
} device_state_t;

static device_state_t current_state = STATE_OFF;
static device_state_t last_state = STATE_OFF;

static const char* state_to_string(device_state_t state){
  switch(state){
    case STATE_OFF: return "OFF";
    case STATE_INITIALIZATION: return "INITIALIZATION";
    case STATE_ONLINE: return "ONLINE";
    case STATE_OFFLINE: return "OFFLINE";
    case STATE_PRINTING: return "PRINTING";
    default: return "UNKNOWN";
  }
}

// CoAP
static char payload[128];
static coap_endpoint_t server_ep;
static coap_message_t request[1];

// MQTT 
static struct mqtt_connection conn;
static bool mqtt_connected = false;
static process_event_t event_mqtt_retry;
static char mqtt_payload[256]; // Buffer for the JSON message

// Timers
static struct etimer retry_timer; // Timer to retry to connect with CoAP Server
static struct etimer print_timer; // Timer to simulate printing duration 
static struct etimer sample_timer; // Timer for takin the samples for prediction
static struct etimer mqtt_timer; // Timer to retry MQTT Connection

// Device Parameters
static char device_name[] = "printer_01";
static const char* device_type = "Filament";
static const char* device_utilization = "Printing";
static char registration_msg[128];

// Printing Parameters
static size_t stl_length = 0;
static bool waiting_for_confirmation = false;
static char print_result[16];
static clock_time_t remaining_print_time = 0;

// Sensing Variables
static float sensor_buffer[8][5]; // Indices: 0-2 (Plate X,Y,Z), 3-5 (Extruder X,Y,Z), 6 (Tension), 7 (Power)
static uint8_t sample_count = 0;
static float features[35];
static uint8_t error_count = 0;

/* ==================================================== */
/* =                 DECLARATIONS                     = */
/* ==================================================== */ 
static void set_state(device_state_t new_state);
static uint32_t calculate_print_duration(size_t stl_size);
static uint16_t prepare_coap_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path);
static void registration_handler(coap_message_t* response);
static void res_health_get_handler(coap_message_t* request, coap_message_t* response, uint8_t* buffer, uint16_t preferred_size, int32_t* offset);
static void res_print_post_handler(coap_message_t* request, coap_message_t* response, uint8_t* buffer, uint16_t preferred_size, int32_t* offset);
static void print_finished_handler(coap_message_t* response);

// Track total power consumed during a print job (in Watt-seconds / Joules)
static float total_power_consumed = 0.0f;

/* ==================================================== */
/* =                    PROCESSES                     = */
/* ==================================================== */ 
PROCESS(setup_process, "Setup Process");
PROCESS(smart_printer_process, "Smart Printer Process");
AUTOSTART_PROCESSES(&setup_process);

/* ==================================================== */
/* =                    RESOURCES                     = */
/* ==================================================== */ 
RESOURCE(res_health,
  "title=\"Health Check\";rt=\"Text\"",
  res_health_get_handler,
  NULL,
  NULL,
  NULL);

RESOURCE(res_print,
  "title=\"Print Job\";rt=\"Block\"",
  NULL,
  res_print_post_handler,
  NULL,
  NULL);

/* ==================================================== */
/* =                     HELPERS                      = */
/* ==================================================== */ 
// Set device state helper (updates last_state, logs and handles LEDs)
static void set_state(device_state_t new_state){
  if(current_state == new_state)
    return;
  last_state = current_state;
  current_state = new_state;

  // Turn off all LEDs first to ensure a clean state
  leds_off(LEDS_ALL);

  // Handle LED logic based on the new state
  if(current_state == STATE_INITIALIZATION){
    // Turn ON Yellow, others are OFF
    leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
  }
  else if(current_state == STATE_ONLINE){
    // Turn ON Green, others are OFF
    leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN));
  }
  LOG_INFO("STATE: %s\n", state_to_string(current_state));
}

// Calculate print time based on STL file size
static uint32_t calculate_print_duration(size_t stl_size){
  uint32_t duration_seconds = (stl_size * 5) / 32;
  
  // Make sure it lasts at least 1 second if the file is very small but > 0
  if(duration_seconds == 0 && stl_size > 0){
    duration_seconds = 1;
  }
  
  return duration_seconds;
}

/* ==================================================== */ 
/* =                    HANDLERS                      = */ 
/* ==================================================== */ 
// MQTT Event Callback to handle connection status changes
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data) {
  switch(event){
    case MQTT_EVENT_CONNECTED:
      LOG_INFO("MQTT Connected to broker\n");
      mqtt_connected = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      // Fix for the double print: check if it was actually connected before logging
      if(mqtt_connected){
        LOG_WARN("MQTT Disconnected\n");
        mqtt_connected = false;
      }
      // Post an event to the main process to handle potential reconnection
      process_post(&smart_printer_process, event_mqtt_retry, NULL);
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

// Prepare CoAP request payload from provided message (generic helper)
static uint16_t prepare_coap_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path){
  memset(payload, 0, sizeof(payload));

  if(message != NULL)
    strncpy(payload, message, sizeof(payload) - 1);

  coap_endpoint_parse(CLOUD_SERVER_EP, strlen(CLOUD_SERVER_EP), &server_ep);

  uint16_t mid = coap_get_mid();
  coap_init_message(request, type, method, mid);

  if(uri_path != NULL)
    coap_set_header_uri_path(request, uri_path);

  if(message != NULL)
    coap_set_payload(request, (uint8_t *)payload, strlen(payload));

  return mid;
}

// CoAP response handler (blocking request callback)
static void registration_handler(coap_message_t* response){
  // Prepares the payload reading for any error messages
  const uint8_t* payload_ptr = NULL;
  int len = coap_get_payload(response, &payload_ptr);
  
  if(current_state == STATE_OFF)
    return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
    set_state(STATE_OFFLINE);
    return; 
  } 

  if(response->code == 65){ 
      // CoAP Code 2.01 (Created)
      LOG_INFO("Registration Successful\n");
      set_state(STATE_ONLINE);
      
      // Connect to MQTT Broker once we are officially ONLINE
      if(!mqtt_connected){
        // Wake up the process to let the centralized MQTT retry logic handle it
        process_post(&smart_printer_process, event_mqtt_retry, NULL);
      }
  }
  else if(response->code == 67){
    // CoAP Code 2.03 (Valid)
      LOG_INFO("Login Successful\n");
      set_state(STATE_ONLINE);

      // Connect to MQTT Broker once we are officially ONLINE
      if(!mqtt_connected){
        // Wake up the process to let the centralized MQTT retry logic handle it
        process_post(&smart_printer_process, event_mqtt_retry, NULL);
      }
  }
  else{
      // Any other code (e.g. 4.00, 5.00) is a failure
      LOG_ERR("Registration Failed with code: %d\n", response->code);
      if(len > 0){
          char buffer[64]; 
          int copy_len = len < sizeof(buffer) - 1 ? len : sizeof(buffer) - 1;
          memcpy(buffer, payload_ptr, copy_len);
          buffer[copy_len] = '\0'; 
          LOG_ERR("Server Error Message: %s\n", buffer);
      }
      set_state(STATE_OFFLINE);
  }
  
  memset(payload, 0, sizeof(payload));
}

// GET handler for the /health resource
static void res_health_get_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  const char* msg = state_to_string(current_state);
  if(current_state == STATE_OFF)
    return;

  // If the device is not online or printing, reject the ping with a 5.03 Service Unavailable.
  // The Python NodeMonitor will read this as a failed response and mark the node OFFLINE.
  if(current_state != STATE_ONLINE && current_state != STATE_PRINTING){
    LOG_WARN("Health check received but device is not ready (State: %s)\n", state_to_string(current_state));
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  LOG_INFO("Health check ping received! Responding with state: %s\n", msg);
  
  // Building the successful response (2.05 Content)
  coap_set_header_content_format(response, TEXT_PLAIN);
  coap_set_payload(response, (uint8_t *)msg, strlen(msg));
}

// POST handler for the /print resource (Handles Block-wise transfer)
static void res_print_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  uint32_t block_num = 0;
  uint8_t more = 0;
  uint16_t block_size = 0;
  uint32_t block_offset = 0;
  
  // Refuse incoming files if the node is not ONLINE (e.g. if it is already PRINTING or OFFLINE)
  if(current_state != STATE_ONLINE){
    LOG_WARN("Print job rejected: device is currently in state %s\n", state_to_string(current_state));
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  // Check if the request contains the Block1 option (payload chunking)
  if(coap_get_header_block1(request, &block_num, &more, &block_size, &block_offset)) {
    const uint8_t *payload_ptr = NULL;
    int len = coap_get_payload(request, &payload_ptr);

    // Accumulate the size of the received file
    stl_length += len;
    LOG_INFO("Received Block %lu (Size: %d bytes, Total STL so far: %zu bytes)\n", (unsigned long)block_num, len, stl_length);

    if(more){
      // More blocks are incoming. Acknowledge this chunk.
      coap_set_status_code(response, CONTINUE_2_31);
      coap_set_header_block1(response, block_num, more, block_size);
    } 
    else{
      // Final block received!
      LOG_INFO("Final STL block received! Total file size: %zu bytes.\n", stl_length);
      
      // Acknowledge the final block (Backend will receive this ACK and mark DB as PRINTING)
      coap_set_status_code(response, CHANGED_2_04);
      coap_set_header_block1(response, block_num, more, block_size);
      
      // Change internal state
      set_state(STATE_PRINTING);
      
      // Wake up the main process to start the printing simulation
      process_poll(&smart_printer_process);
    }
  } 
  else{
    // Request does not have Block1 option
    LOG_WARN("Missing Block1 option in /print POST request\n");
    coap_set_status_code(response, BAD_REQUEST_4_00);
  }
}

// Helper function to prepare the end-of-print request
static void print_finished_handler(coap_message_t* response){
  // Prepares the payload reading for any error messages
  const uint8_t* payload_ptr = NULL;
  int len = coap_get_payload(response, &payload_ptr);

  if(current_state == STATE_OFF)
    return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
    set_state(STATE_ONLINE);
    waiting_for_confirmation = false; 
    return; 
  } 

  if(response->code == 68)
      // CoAP Code 2.04
      LOG_INFO("Print Memorized in the Database\n");
  else{
      // Any other code (e.g. 4.00, 5.00) is a failure
      LOG_ERR("Memorization of the print failed Failed with code: %d\n", response->code);
      if(len > 0){
          char buffer[64]; 
          int copy_len = len < sizeof(buffer) - 1 ? len : sizeof(buffer) - 1;
          memcpy(buffer, payload_ptr, copy_len);
          buffer[copy_len] = '\0'; 
          LOG_ERR("Server Error Message: %s\n", buffer);
      }
  }

  waiting_for_confirmation = false; 
  set_state(STATE_ONLINE);
  memset(payload, 0, sizeof(payload));
}

/* ==================================================== */
/* =                  SETUP PROCESS                   = */
/* ==================================================== */ 
PROCESS_THREAD(setup_process, ev, data){
  static button_hal_button_t *btn;
  /* track whether smart_printer_process is active to avoid double starts */
  static bool smart_printer_active = false;

  PROCESS_BEGIN();

  LOG_INFO("Smart Printer SETUP process starting...\n");

  // Generate a unique name based on the node ID
  snprintf(device_name, sizeof(device_name), "printer_%02u", node_id - 1);

  // Generate the device type
  if(node_id == 2 || node_id == 3)
    device_type = "Filament";
  else if(node_id == 4 || node_id == 5)
    device_type = "Resin";
  else{
    if(node_id % 2 != 0)
      device_type = "Filament";
    else
      device_type = "Resin";
  }

  // Initialize global state 
  current_state = STATE_OFF;
  last_state = STATE_OFF;
  LOG_INFO("STATE: OFF\n");

  // Initialize Button HAL once 
  button_hal_init();

  btn = button_hal_get_by_index(0);
  if(btn){
    LOG_WARN("Setup: button initialized: %s on pin %u\n", BUTTON_HAL_GET_DESCRIPTION(btn), btn->pin);
    LOG_WARN("Short press to start SmartPrinter. Hold 3s to reset when active.\n");
  } 
  else
    LOG_WARN("Setup: no button available\n");

  // Monitor process exit events to track smart_printer lifecycle
  while(1){
    PROCESS_YIELD();

    // Short press event: start/notify
    if(ev == button_hal_press_event){
      LOG_INFO("Setup: 'Button Pressed' Event\n");

      if(!smart_printer_active){
        set_state(STATE_INITIALIZATION);

        process_start(&smart_printer_process, NULL);
        smart_printer_active = true;

        // Trigger initialization immediately
        process_poll(&smart_printer_process);
      } 
      else{
        LOG_WARN("Smart Printer already active. Hold button 5 seconds to perform hard reset.\n");
        LOG_INFO("STATE: %s\n", state_to_string(current_state));
      }
    }

    // Periodic button event
    if(ev == button_hal_periodic_event && btn && data){
      button_hal_button_t *b = (button_hal_button_t *)data;

      if(b == btn) {
        LOG_DBG("Setup: Button hold duration: %u s\n", b->press_duration_seconds);
        if(b->press_duration_seconds == 1) {
          char ipaddr_str[UIPLIB_IPV6_MAX_STR_LEN];
          uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);

          if(addr != NULL) {
            uiplib_ipaddr_snprint(ipaddr_str,
                                  sizeof(ipaddr_str),
                                  &addr->ipaddr);

            LOG_INFO("Current IPv6 address: %s\n", ipaddr_str);
          } 
          else{
            LOG_INFO("No global IPv6 address assigned yet\n");
          }
        }
      }
    }

    // Detect when the smart_printer process exits
    if(ev == PROCESS_EVENT_EXITED){
      struct process *exited = (struct process *)data;

      if(exited == &smart_printer_process){
        LOG_WARN("Smart Printer thread exited\n");
        smart_printer_active = false;

        sensor_deactivate();

        set_state(STATE_OFF);
        LOG_INFO("Device reset to STATE: OFF - await short press to start again\n");
      } 
      else{
        LOG_ERR("Some other process exited: %s\n", exited->name);
      }
    }
  }

  PROCESS_END();
}

/* ==================================================== */
/* =              SMART PRINTER PROCESS               = */
/* ==================================================== */ 
PROCESS_THREAD(smart_printer_process, ev, data){
  static button_hal_button_t *btn0 = NULL;
  PROCESS_BEGIN();

  LOG_INFO("Smart Printer process started\n");

  event_mqtt_retry = process_alloc_event();
  
  // Register the MQTT client context 
  mqtt_register(&conn, &smart_printer_process, device_name, mqtt_event, 256);
  
  // Activate resources 
  coap_activate_resource(&res_health, "health");
  coap_activate_resource(&res_print, "print"); 
  
  btn0 = button_hal_get_by_index(0);
  if(!btn0)
    LOG_WARN("Smart Printer: No button available\n");
  
  snprintf(registration_msg,
    sizeof(registration_msg),
    "{\"name\":\"%s\",\"type\":\"%s\",\"utilization\":\"%s\"}",
    device_name,
    device_type,
    device_utilization);

  while(1){
    PROCESS_WAIT_EVENT();

    if(ev == event_mqtt_retry || (ev == PROCESS_EVENT_TIMER && data == &mqtt_timer)){
      // Only attempt to connect if we are supposed to be connected (ONLINE or PRINTING)
      if(!mqtt_connected && (current_state == STATE_ONLINE || current_state == STATE_PRINTING)){
        LOG_INFO("Attempting MQTT connection to %s:%d\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);
        mqtt_connect(&conn, MQTT_BROKER_IP, MQTT_BROKER_PORT, 60 * 3, MQTT_CLEAN_SESSION_ON);
        
        // Set a timer to retry in 10 seconds if the connection fails or drops again
        etimer_set(&mqtt_timer, 10 * CLOCK_SECOND);
      }
    }

    /* BUTTON HANDLING */
    // Handle Button Release (for short press confirmation or 2-4s override)
    if(ev == button_hal_release_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;
      if(btn == btn0){
        // If we finished printing (or ML stopped it) and are waiting for the user...
        if(current_state == STATE_PRINTING && waiting_for_confirmation){
          // Format the CoAP payload with status and accumulated power
          char end_payload[64];
          int en_i = (int)total_power_consumed;
          int en_d = (int)(fabs(total_power_consumed - en_i) * 100);
          snprintf(end_payload, sizeof(end_payload), "{\"status\":\"%s\",\"energy\":%d.%02d}", print_result, en_i, en_d);

          // Short press (< 2 seconds): Confirm the current print_result
          if(btn->press_duration_seconds < 2){
            // Let's stop the running timers
            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            
            // Let's clean up the state to prepare the node for new prints
            stl_length = 0;
            error_count = 0;

            LOG_INFO("Button released (< 2s). Sending %s notification to server...\n", print_result);
            waiting_for_confirmation = false; // Reset the flag
              
            prepare_coap_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
            COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
          }
          // Long press (between 2 and 4 seconds): Override or Manual Fail
          else if(btn->press_duration_seconds >= 2 && btn->press_duration_seconds < 5){
            // Case A: Override ML Early Stopping
            if(strcmp(print_result, "FAILED") == 0){
              LOG_INFO("Button released (2-4s). Overriding ML Anomaly. Resuming print...\n");
              
              // Turn OFF Red LED, ensure others are OFF
              leds_off(LEDS_ALL);
              
              waiting_for_confirmation = false;
              error_count = 0; // Reset error count to avoid immediate re-trigger
              
              // Restart the timers
              etimer_restart(&sample_timer);
              etimer_set(&print_timer, remaining_print_time); // He starts from where he left off 
            }
            // Case B: Manual Failure after successful timer completion
            else if(strcmp(print_result, "FINISHED") == 0){
              LOG_INFO("Button released (2-4s). Manual override: Declaring print as FAILED.\n");
              
              waiting_for_confirmation = false;
              
              // Overwrite the payload to ensure status is FAILED instead of FINISHED
              snprintf(end_payload, sizeof(end_payload), "{\"status\":\"FAILED\",\"energy\":%d.%02d}", en_i, en_d);
              
              prepare_coap_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
              COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
            }
          }
        }
      }
    }
    // Handle Long Press (for Hard Reset immediately at >= 5 seconds)
    else if(ev == button_hal_periodic_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;

      if(btn == btn0){
        LOG_DBG("Smart Printer: Button hold duration: %u s\n", btn->press_duration_seconds);
        
        // Hard Reset (>= 5 seconds)
        if(btn->press_duration_seconds >= 5 && current_state != STATE_OFF){
          LOG_INFO("Button held >= 5s -> Hard reset\n");
          
          // Create a transaction for sending
          if(current_state == STATE_PRINTING){
            char reset_payload[64];
            int en_i = (int)total_power_consumed;
            int en_d = (int)(fabs(total_power_consumed - en_i) * 100);
            snprintf(reset_payload, sizeof(reset_payload), "{\"status\":\"ERROR\",\"energy\":%d.%02d}", en_i, en_d);

            uint16_t mid = prepare_coap_request(reset_payload, COAP_TYPE_NON, COAP_POST, END_PRINT_URI_PATH);
            coap_transaction_t* transaction = coap_new_transaction(mid, &server_ep);
            if(transaction){
              transaction->message_len = coap_serialize_message(request, transaction->message);
              coap_send_transaction(transaction);
            }
          }
          else{
            uint16_t mid = prepare_coap_request(NULL, COAP_TYPE_NON, COAP_POST, OFF_SIGNAL_URI_PATH);
            coap_transaction_t* off_transaction = coap_new_transaction(mid, &server_ep);
            if(off_transaction){
              off_transaction->message_len = coap_serialize_message(request, off_transaction->message);
              coap_send_transaction(off_transaction);
            }
          }

          // Disconnect MQTT if active
          if(mqtt_connected){
            mqtt_disconnect(&conn);
          }

          // Ensure all LEDs are OFF when the system is resetting
          leds_off(LEDS_ALL);
          
          sample_count = 0;
          error_count = 0;
          stl_length = 0;
          total_power_consumed = 0.0f; // Reset energy tracking
          
          waiting_for_confirmation = false; 

          etimer_stop(&retry_timer);
          etimer_stop(&print_timer);
          etimer_stop(&sample_timer);

          set_state(STATE_OFF);

          PROCESS_EXIT();
        }
      }
    }

    /* STATE MACHINE */
    // INITIALIZATION 
    if(current_state == STATE_INITIALIZATION){
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("Initialization requested -> sending registration request\n");

        // Initializating Sensors
        sensors_init();
        // Put Sensors to sleep
        sensor_sleep();
 
        prepare_coap_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);

        LOG_INFO("Sending registration request...\n");
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);

        if(current_state == STATE_OFFLINE){
          LOG_INFO("Retry in 10 seconds\n");
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        }
      }
    }

    // OFFLINE
    else if(current_state == STATE_OFFLINE){
      if(ev == PROCESS_EVENT_TIMER && data == &retry_timer){
        LOG_INFO("Retry timer expired -> attempting registration again\n");
 
        prepare_coap_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);

        LOG_INFO("Sending request...\n");
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);
        
        if(current_state != STATE_ONLINE){
          LOG_INFO("STATE: OFFLINE\n");
          LOG_INFO("Retry in 10 seconds\n");
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        }
      }
    }

    // ONLINE 
    else if(current_state == STATE_ONLINE){
      /* Ready to accept print jobs via /print resource */
      /* No active polling needed here; CoAP handles incoming requests */
    }

    // PRINTING
    else if(current_state == STATE_PRINTING){
      // This poll event is triggered by the res_print_post_handler when the last chunk arrives
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("STL fully received. Starting physical print simulation...\n");
        
        // Reset the sample counter for the sliding window and the total power consumed
        sample_count = 0;
        total_power_consumed = 0.0f;

        // Calculate time dynamically
        uint32_t dynamic_print_time = calculate_print_duration(stl_length);
        LOG_INFO("Calculated print duration: %lu seconds for an STL of %zu bytes\n", (unsigned long)dynamic_print_time, stl_length);
        
        // Start the printing process timer with the calculated time
        etimer_set(&print_timer, dynamic_print_time * CLOCK_SECOND);
        // Start the 1-second sampling timer
        etimer_set(&sample_timer, 1 * CLOCK_SECOND);
      } 
      
      // 1-Second Sampling Timer Event
      else if(ev == PROCESS_EVENT_TIMER && data == &sample_timer){
        if(waiting_for_confirmation)
          continue;

        // Turn ON Yellow LED to indicate measurement in progress
        leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
        
        // Activate Sensors
        sensor_activate();
        // Read data from sensors
        accel_data_t plate = read_plate_acceleration();
        accel_data_t extruder = read_extruder_acceleration();
        float tension = read_tension();
        float power = read_power(); // Read power directly
        
        // Accumulate consumed energy (Watt-seconds)
        total_power_consumed += power;

        // Print the current readings
        LOG_INFO("Measurements: Plate(%.3f, %.3f, %.3f), Extruder(%.3f, %.3f, %.3f), Tension(%.3fV), Power(%.3fW)\n", 
                 plate.x, plate.y, plate.z, extruder.x, extruder.y, extruder.z, tension, power);

        // Fetch IPv6 Address to include in the MQTT JSON payload
        char ipaddr_str[UIPLIB_IPV6_MAX_STR_LEN];
        uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
        if(addr != NULL)
          uiplib_ipaddr_snprint(ipaddr_str, sizeof(ipaddr_str), &addr->ipaddr);
        else
          strcpy(ipaddr_str, "unknown");

        // Format the JSON payload and publish via MQTT if connected
        if(mqtt_connected){
          // Convert variables into Integer + Fractional (3 decimals) for JSON compliance
          int px_i = (int)plate.x; int px_d = (int)(fabs(plate.x - px_i) * 1000);
          int py_i = (int)plate.y; int py_d = (int)(fabs(plate.y - py_i) * 1000);
          int pz_i = (int)plate.z; int pz_d = (int)(fabs(plate.z - pz_i) * 1000);
          
          int ex_i = (int)extruder.x; int ex_d = (int)(fabs(extruder.x - ex_i) * 1000);
          int ey_i = (int)extruder.y; int ey_d = (int)(fabs(extruder.y - ey_i) * 1000);
          int ez_i = (int)extruder.z; int ez_d = (int)(fabs(extruder.z - ez_i) * 1000);
          
          int ten_i = (int)tension; int ten_d = (int)(fabs(tension - ten_i) * 1000);
          int pow_i = (int)power;   int pow_d = (int)(fabs(power - pow_i) * 1000);

          // Handle negative numbers correctly when integer part is 0 (e.g. -0.385)
          const char* px_sign = (plate.x < 0 && px_i == 0) ? "-" : "";
          const char* py_sign = (plate.y < 0 && py_i == 0) ? "-" : "";
          const char* pz_sign = (plate.z < 0 && pz_i == 0) ? "-" : "";
          const char* ex_sign = (extruder.x < 0 && ex_i == 0) ? "-" : "";
          const char* ey_sign = (extruder.y < 0 && ey_i == 0) ? "-" : "";
          const char* ez_sign = (extruder.z < 0 && ez_i == 0) ? "-" : "";
          const char* ten_sign = (tension < 0 && ten_i == 0) ? "-" : "";
          const char* pow_sign = (power < 0 && pow_i == 0) ? "-" : "";

          snprintf(mqtt_payload, sizeof(mqtt_payload),
            "{\"ip\":\"%s\",\"X_Axis_Plate\":%s%d.%03d,\"Y_Axis_Plate\":%s%d.%03d,\"Z_Axis_Plate\":%s%d.%03d,\"X_Axis_Extrusion\":%s%d.%03d,\"Y_Axis_Extrusion\":%s%d.%03d,\"Z_Axis_Extrusion\":%s%d.%03d,\"Tension\":%s%d.%03d,\"Power\":%s%d.%03d}",
            ipaddr_str, 
            px_sign, px_i, px_d, 
            py_sign, py_i, py_d, 
            pz_sign, pz_i, pz_d, 
            ex_sign, ex_i, ex_d, 
            ey_sign, ey_i, ey_d, 
            ez_sign, ez_i, ez_d, 
            ten_sign, ten_i, ten_d, 
            pow_sign, pow_i, pow_d);

          mqtt_publish(&conn, NULL, MQTT_PUB_TOPIC, (uint8_t *)mqtt_payload, strlen(mqtt_payload), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
        }
                 
        // Store readings into the sliding window buffer
        sensor_buffer[0][sample_count] = plate.x;
        sensor_buffer[1][sample_count] = plate.y;
        sensor_buffer[2][sample_count] = plate.z;
        sensor_buffer[3][sample_count] = extruder.x;
        sensor_buffer[4][sample_count] = extruder.y;
        sensor_buffer[5][sample_count] = extruder.z;
        sensor_buffer[6][sample_count] = tension;
        sensor_buffer[7][sample_count] = power;
        
        sample_count++;
        
        // If we reached 5 samples, process the window
        if(sample_count == 5){
          leds_off(LEDS_NUM_TO_MASK(LEDS_YELLOW)); // Turn OFF Yellow LED to create the blinking effect
          LOG_INFO("Window full. Extracting features and running prediction...\n");
          
          uint8_t feature_idx = 0;
          
          // Process each of the 7 variables (power excluded)
          for(uint8_t var = 0; var < 7; var++){
            float sum = 0.0f;
            float max_val = sensor_buffer[var][0];
            float min_val = sensor_buffer[var][0];
            
            // Calculate Mean, Max, Min
            for(uint8_t i = 0; i < 5; i++){
              float val = sensor_buffer[var][i];
              sum += val;
              if(val > max_val) max_val = val;
              if(val < min_val) min_val = val;
            }
            float mean = sum / 5.0f;
            float ptp = max_val - min_val;
            
            // Calculate Standard Deviation
            float variance_sum = 0.0f;
            for(uint8_t i = 0; i < 5; i++)
              variance_sum += (sensor_buffer[var][i] - mean) * (sensor_buffer[var][i] - mean);

            float std_dev = sqrt(variance_sum / 5.0f); // Population std dev
            
            // Store the 5 features for this variable
            features[feature_idx++] = mean;
            features[feature_idx++] = std_dev;
            features[feature_idx++] = max_val;
            features[feature_idx++] = min_val;
            features[feature_idx++] = ptp;
          }
          
          // Apply Z-Score Scaling using SCALER_MEANS and SCALER_SCALES
          for(uint8_t i = 0; i < 35; i++)
            features[i] = (features[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];
          
          /* PREDICTION */
          LOG_INFO("Features scaled. Running inference...\n");          
          
          // Run the prediction by passing the feature array and its size (35)
          int32_t prediction = print_prediction_predict(features, 35);
          
          if(prediction == 1){
            error_count++;
            LOG_WARN("Anomaly Detected! (Alarm %d/3)\n", error_count);
          } 
          else{
            LOG_INFO("Machine Learning Model: Regular print...\n");
            error_count = 0; // Reset the counter if there is a return to normal
          }

          // EARLY STOPPING LOGIC
          if(error_count >= 3){
            LOG_ERR("EARLY STOPPING: Three consecutive anomalies detected. Printing stopped preemptively!\n");
            // Turn ON Red LED and ensure others are OFF due to Machine Learning anomaly
            leds_off(LEDS_ALL);
            leds_on(LEDS_NUM_TO_MASK(LEDS_RED));

            // Save the remaining print time before stopping the timer
            remaining_print_time = etimer_expiration_time(&print_timer) - clock_time();

            // Let's stop the running timers
            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            sample_count = 0;
            
            sensor_deactivate();
            
            // Set flags to wait for user confirmation instead of sending CoAP immediately
            waiting_for_confirmation = true;
            
            strncpy(print_result, "FAILED", sizeof(print_result));
            
            LOG_INFO("Print aborted. Press the button to confirm and notify the server.\n");            
          }
          else{
            // If printing continues, we shift the window: we keep the last measurement (index 4) at index 0
            for(uint8_t var = 0; var < 8; var++)
              sensor_buffer[var][0] = sensor_buffer[var][4];
            
            // We reset the sample_count to 1 (since we kept 1 sample)
            sample_count = 1;
          }
        }
        
        // Put Sensors to sleep
        sensor_sleep();

        // Restart the sampling timer and turn OFF Yellow LED if no errors occurred
        if(!waiting_for_confirmation){
          etimer_reset(&sample_timer);
        }
      }
      
      // This event triggers when the fake printing timer ends
      else if(ev == PROCESS_EVENT_TIMER && data == &print_timer){
        LOG_INFO("Printing complete (simulated)\n");
        stl_length = 0; // Reset for the next print
        
        // Ensure Yellow LED is OFF when printing is successfully done
        leds_off(LEDS_ALL);
        leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN) | LEDS_NUM_TO_MASK(LEDS_YELLOW));
        
        // Stop the sampling timer and reset the window count to cancel any pending prediction
        etimer_stop(&sample_timer);
        sample_count = 0;
        error_count = 0;

        sensor_deactivate();

        // Set flags to wait for user confirmation instead of sending CoAP immediately
        waiting_for_confirmation = true;
        
        strncpy(print_result, "FINISHED", sizeof(print_result));
        
        LOG_INFO("Print finished successfully. Press the button to confirm and notify the server.\n");
      }
    }
  }

  PROCESS_END();
}