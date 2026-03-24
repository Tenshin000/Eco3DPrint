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

/* Standard C libraries */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Project Utility */
#include "utility/printer_sensors.h"
#include "utility/scaler_params.h"
#include "utility/device_conf.h" 
#include "utility/coap_module.h"
#include "utility/mqtt_module.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "utility/print_prediction.h"
#pragma GCC diagnostic pop

// Smart Printer Log
#define LOG_MODULE "Smart Printer"
#define LOG_LEVEL LOG_LEVEL_APP

/* ==================================================== */ 
/* =                  CONFIGURATION                   = */ 
/* ==================================================== */ 
// Current and previous device state
static device_state_t current_state = STATE_OFF;
static device_state_t last_state = STATE_OFF;

// Timers
static struct etimer retry_timer; // Timer to retry to connect with CoAP Server
static struct etimer print_timer; // Timer to simulate printing duration 
static struct etimer sample_timer; // Timer for takin the samples for prediction
static struct etimer mqtt_timer; // Timer to retry MQTT Connection
static struct etimer mqtt_disconnect_timer; // Timer to delay MQTT disconnection to save resources

// Device Parameters
static char device_name[32];
static const char* device_type = "Filament";
static const char* device_utilization = "Printing";
static char registration_msg[256]; // Increased buffer size for SenML
static char mqtt_payload[512];     // Greatly increased buffer size for SenML array

// Printing Parameters
static size_t stl_length = 0;
static bool waiting_for_confirmation = false;
static char print_result[16];
static clock_time_t remaining_print_time = 0;

// Sensing Variables
static float sensor_buffer[8][5]; 
static uint8_t sample_count = 0;
static float features[35];
static uint8_t error_count = 0;
static float total_power_consumed = 0.0f;

/* ==================================================== */
/* =                    PROCESSES                     = */
/* ==================================================== */ 
PROCESS(setup_process, "Setup Process");
PROCESS(smart_printer_process, "Smart Printer Process");
AUTOSTART_PROCESSES(&setup_process);

/* ==================================================== */
/* =                  HOOKS / HELPERS                 = */
/* ==================================================== */ 
const char* state_to_string(device_state_t state){
  switch(state){
    case STATE_OFF: return "OFF";
    case STATE_INITIALIZATION: return "INITIALIZATION";
    case STATE_ONLINE: return "ONLINE";
    case STATE_OFFLINE: return "OFFLINE";
    case STATE_PRINTING: return "PRINTING";
    default: return "UNKNOWN";
  }
}

static void set_state(device_state_t new_state){
  if(current_state == new_state) return;
  last_state = current_state;
  current_state = new_state;

  leds_off(LEDS_ALL);
  if(current_state == STATE_INITIALIZATION) leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
  else if(current_state == STATE_ONLINE) leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN));
  
  // Manage MQTT connection/disconnection based on state transitions
  if(current_state == STATE_PRINTING) {
    // Stop the disconnect timer if we re-entered PRINTING quickly
    etimer_stop(&mqtt_disconnect_timer);
    
    // Trigger MQTT connection if not connected
    if(!mqtt_module_is_connected()) {
      device_trigger_mqtt_retry();
    }
  } 
  else if(current_state == STATE_ONLINE && last_state == STATE_PRINTING) {
    // Delay disconnection to wait for potential incoming QUEUE jobs
    LOG_INFO("Print finished. Starting MQTT disconnect timer (15s)...\n");
    etimer_set(&mqtt_disconnect_timer, 15 * CLOCK_SECOND);
  }
  else if(current_state == STATE_OFF || current_state == STATE_OFFLINE) {
    // Immediate disconnect on critical states
    etimer_stop(&mqtt_disconnect_timer);
    mqtt_module_disconnect();
  }
  
  LOG_INFO("STATE: %s\n", state_to_string(current_state));
}

// Hook Implementations requested by external modules
device_state_t device_get_state(void) { return current_state; }
const char* device_get_state_string(device_state_t state) { return state_to_string(state); }
void device_set_state(device_state_t new_state) { set_state(new_state); }
void device_trigger_mqtt_retry(void) { process_post(&smart_printer_process, event_mqtt_retry, NULL); }
void device_add_stl_length(int len) { stl_length += len; }
void device_trigger_print_simulation(void) { process_poll(&smart_printer_process); }
void device_reset_waiting_confirmation(void) { waiting_for_confirmation = false; }

static uint32_t calculate_print_duration(size_t stl_size){
  uint32_t duration_seconds = (stl_size * 5) / 32;
  if(duration_seconds == 0 && stl_size > 0) duration_seconds = 1;
  return duration_seconds;
}

/* ==================================================== */
/* =                  SETUP PROCESS                   = */
/* ==================================================== */ 
PROCESS_THREAD(setup_process, ev, data){
  static button_hal_button_t *btn;
  static bool smart_printer_active = false;

  PROCESS_BEGIN();

  LOG_INFO("Smart Printer SETUP process starting...\n");

  uint16_t safe_id = (node_id > 0) ? (node_id - 1) : 0;
  snprintf(device_name, sizeof(device_name), "printer_%02u", safe_id);

  if(node_id == 2 || node_id == 3) device_type = "Filament";
  else if(node_id == 4 || node_id == 5) device_type = "Resin";
  else{
    if(node_id % 2 != 0) device_type = "Filament";
    else device_type = "Resin";
  }

  current_state = STATE_OFF;
  last_state = STATE_OFF;
  LOG_INFO("STATE: OFF\n");

  button_hal_init();
  btn = button_hal_get_by_index(0);
  
  if(btn){
    LOG_WARN("Setup: button initialized: %s on pin %u\n", BUTTON_HAL_GET_DESCRIPTION(btn), btn->pin);
    LOG_WARN("Short press to start SmartPrinter. Hold 5s to reset when active.\n");
  } else {
    LOG_WARN("Setup: no button available\n");
  }

  while(1){
    PROCESS_YIELD();

    if(ev == button_hal_press_event){
      LOG_INFO("Setup: 'Button Pressed' Event\n");
      if(!smart_printer_active){
        set_state(STATE_INITIALIZATION);
        process_start(&smart_printer_process, NULL);
        smart_printer_active = true;
        process_poll(&smart_printer_process);
      } else {
        LOG_WARN("Smart Printer already active. Hold button 5 seconds to perform hard reset.\n");
        LOG_INFO("STATE: %s\n", state_to_string(current_state));
      }
    }

    if(ev == button_hal_periodic_event && btn && data){
      button_hal_button_t *b = (button_hal_button_t *)data;
      if(b == btn) {
        LOG_DBG("Setup: Button hold duration: %u s\n", b->press_duration_seconds);
        if(b->press_duration_seconds == 1) {
          char ipaddr_str[UIPLIB_IPV6_MAX_STR_LEN];
          uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
          if(addr != NULL) {
            uiplib_ipaddr_snprint(ipaddr_str, sizeof(ipaddr_str), &addr->ipaddr);
            LOG_INFO("Current IPv6 address: %s\n", ipaddr_str);
          } else {
            LOG_INFO("No global IPv6 address assigned yet\n");
          }
        }
      }
    }

    if(ev == PROCESS_EVENT_EXITED){
      struct process *exited = (struct process *)data;
      if(exited == &smart_printer_process){
        LOG_WARN("Smart Printer thread exited\n");
        smart_printer_active = false;
        sensor_deactivate();
        set_state(STATE_OFF);
        LOG_INFO("Device reset to STATE: OFF - await short press to start again\n");
      } else {
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

  // Initialize Modules
  mqtt_module_init(&smart_printer_process, device_name);
  coap_module_init();
  
  btn0 = button_hal_get_by_index(0);
  if(!btn0) LOG_WARN("Smart Printer: No button available\n");
  
  // Format the CoAP Registration Message using SenML standard
  snprintf(registration_msg, sizeof(registration_msg),
    "[{\"bn\":\"%s\",\"n\":\"type\",\"vs\":\"%s\"},{\"n\":\"utilization\",\"vs\":\"%s\"}]",
    device_name, device_type, device_utilization);

  while(1){
    PROCESS_WAIT_EVENT();

    /* MQTT CONNECTION AND DISCONNECTION EVENTS */
    if(ev == event_mqtt_retry || (ev == PROCESS_EVENT_TIMER && data == &mqtt_timer)){
      // Only attempt to connect if we are in PRINTING state
      if(!mqtt_module_is_connected() && current_state == STATE_PRINTING){
        mqtt_module_connect();
        etimer_set(&mqtt_timer, 10 * CLOCK_SECOND);
      }
    }
    else if(ev == PROCESS_EVENT_TIMER && data == &mqtt_disconnect_timer){
      // If the timer expires and we are NOT printing, disconnect MQTT
      if(current_state != STATE_PRINTING){
        LOG_INFO("No new print jobs received. Disconnecting MQTT to save resources.\n");
        mqtt_module_disconnect();
      }
    }

    /* BUTTON HANDLING */
    if(ev == button_hal_release_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;
      if(btn == btn0){
        if(current_state == STATE_PRINTING && waiting_for_confirmation){
          char end_payload[128]; // Increased buffer size for SenML
          int en_i = (int)total_power_consumed;
          int en_d = (int)(fabs(total_power_consumed - en_i) * 100);
          
          // Format the End-of-Print message using SenML
          snprintf(end_payload, sizeof(end_payload), 
                   "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"%s\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                   device_name, print_result, en_i, en_d);

          if(btn->press_duration_seconds < 2){
            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            stl_length = 0; error_count = 0;
            LOG_INFO("Button released (< 2s). Sending %s notification to server...\n", print_result);
            waiting_for_confirmation = false;
            coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
            COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
          }
          else if(btn->press_duration_seconds >= 2 && btn->press_duration_seconds < 5){
            if(strcmp(print_result, "FAILED") == 0){
              LOG_INFO("Button released (2-4s). Overriding ML Anomaly. Resuming print...\n");
              leds_off(LEDS_ALL);
              waiting_for_confirmation = false;
              error_count = 0; 
              etimer_restart(&sample_timer);
              etimer_set(&print_timer, remaining_print_time); 
            }
            else if(strcmp(print_result, "FINISHED") == 0){
              LOG_INFO("Button released (2-4s). Manual override: Declaring print as FAILED.\n");
              waiting_for_confirmation = false;
              
              snprintf(end_payload, sizeof(end_payload), 
                       "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"FAILED\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                       device_name, en_i, en_d);
                       
              coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
              COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
            }
          }
        }
      }
    }
    else if(ev == button_hal_periodic_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;
      if(btn == btn0){
        LOG_DBG("Smart Printer: Button hold duration: %u s\n", btn->press_duration_seconds);
        if(btn->press_duration_seconds >= 5 && current_state != STATE_OFF){
          LOG_INFO("Button held >= 5s -> Hard reset\n");
          
          if(current_state == STATE_PRINTING){
            char reset_payload[128];
            int en_i = (int)total_power_consumed;
            int en_d = (int)(fabs(total_power_consumed - en_i) * 100);
            
            // Format the Hard Reset message using SenML
            snprintf(reset_payload, sizeof(reset_payload), 
                     "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"ERROR\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                     device_name, en_i, en_d);

            uint16_t mid = coap_module_prepare_request(reset_payload, COAP_TYPE_NON, COAP_POST, END_PRINT_URI_PATH);
            coap_transaction_t* transaction = coap_new_transaction(mid, &server_ep);
            if(transaction){
              transaction->message_len = coap_serialize_message(request, transaction->message);
              coap_send_transaction(transaction);
            }
          } else {
            uint16_t mid = coap_module_prepare_request(NULL, COAP_TYPE_NON, COAP_POST, OFF_SIGNAL_URI_PATH);
            coap_transaction_t* off_transaction = coap_new_transaction(mid, &server_ep);
            if(off_transaction){
              off_transaction->message_len = coap_serialize_message(request, off_transaction->message);
              coap_send_transaction(off_transaction);
            }
          }

          mqtt_module_disconnect();
          leds_off(LEDS_ALL);
          
          sample_count = 0; error_count = 0; stl_length = 0; total_power_consumed = 0.0f;
          waiting_for_confirmation = false; 

          etimer_stop(&retry_timer); etimer_stop(&print_timer); etimer_stop(&sample_timer);
          etimer_stop(&mqtt_disconnect_timer); // Clear the disconnect timer
          
          set_state(STATE_OFF);
          PROCESS_EXIT();
        }
      }
    }

    /* STATE MACHINE */
    if(current_state == STATE_INITIALIZATION){
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("Initialization requested -> sending registration request\n");
        sensors_init();
        sensor_sleep();
 
        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        LOG_INFO("Sending registration request...\n");
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);

        if(current_state == STATE_OFFLINE){
          LOG_INFO("Retry in 10 seconds\n");
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        }
      }
    }
    else if(current_state == STATE_OFFLINE){
      if(ev == PROCESS_EVENT_TIMER && data == &retry_timer){
        LOG_INFO("Retry timer expired -> attempting registration again\n");
        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        LOG_INFO("Sending request...\n");
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);
        
        if(current_state != STATE_ONLINE){
          LOG_INFO("STATE: OFFLINE\n");
          LOG_INFO("Retry in 10 seconds\n");
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        }
      }
    }
    else if(current_state == STATE_ONLINE){
      // Handled by CoAP Resource
    }
    else if(current_state == STATE_PRINTING){
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("STL fully received. Starting physical print simulation...\n");
        sample_count = 0; total_power_consumed = 0.0f;

        uint32_t dynamic_print_time = calculate_print_duration(stl_length);
        LOG_INFO("Calculated print duration: %lu seconds for an STL of %zu bytes\n", (unsigned long)dynamic_print_time, stl_length);
        etimer_set(&print_timer, dynamic_print_time * CLOCK_SECOND);
        etimer_set(&sample_timer, 1 * CLOCK_SECOND);
      } 
      else if(ev == PROCESS_EVENT_TIMER && data == &sample_timer){
        if(waiting_for_confirmation) continue;

        leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
        sensor_activate();
        
        accel_data_t plate = read_plate_acceleration();
        accel_data_t extruder = read_extruder_acceleration();
        float tension = read_tension();
        float power = read_power(); 
        
        total_power_consumed += power;

        LOG_INFO("Measurements: Plate(%.3f, %.3f, %.3f), Extruder(%.3f, %.3f, %.3f), Tension(%.3fV), Power(%.3fW)\n", 
                 plate.x, plate.y, plate.z, extruder.x, extruder.y, extruder.z, tension, power);

        if(mqtt_module_is_connected()){
          int px_i = (int)plate.x; int px_d = (int)(fabs(plate.x - px_i) * 1000);
          int py_i = (int)plate.y; int py_d = (int)(fabs(plate.y - py_i) * 1000);
          int pz_i = (int)plate.z; int pz_d = (int)(fabs(plate.z - pz_i) * 1000);
          int ex_i = (int)extruder.x; int ex_d = (int)(fabs(extruder.x - ex_i) * 1000);
          int ey_i = (int)extruder.y; int ey_d = (int)(fabs(extruder.y - ey_i) * 1000);
          int ez_i = (int)extruder.z; int ez_d = (int)(fabs(extruder.z - ez_i) * 1000);
          int ten_i = (int)tension; int ten_d = (int)(fabs(tension - ten_i) * 1000);
          int pow_i = (int)power;   int pow_d = (int)(fabs(power - pow_i) * 1000);

          const char* px_sign = (plate.x < 0 && px_i == 0) ? "-" : "";
          const char* py_sign = (plate.y < 0 && py_i == 0) ? "-" : "";
          const char* pz_sign = (plate.z < 0 && pz_i == 0) ? "-" : "";
          const char* ex_sign = (extruder.x < 0 && ex_i == 0) ? "-" : "";
          const char* ey_sign = (extruder.y < 0 && ey_i == 0) ? "-" : "";
          const char* ez_sign = (extruder.z < 0 && ez_i == 0) ? "-" : "";
          const char* ten_sign = (tension < 0 && ten_i == 0) ? "-" : "";
          const char* pow_sign = (power < 0 && pow_i == 0) ? "-" : "";

          // Format MQTT message using SenML JSON standard array
          snprintf(mqtt_payload, sizeof(mqtt_payload),
            "["
            "{\"bn\":\"%s\",\"n\":\"X_Axis_Plate\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"Y_Axis_Plate\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"Z_Axis_Plate\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"X_Axis_Extrusion\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"Y_Axis_Extrusion\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"Z_Axis_Extrusion\",\"u\":\"m/s2\",\"v\":%s%d.%03d},"
            "{\"n\":\"Tension\",\"u\":\"V\",\"v\":%s%d.%03d},"
            "{\"n\":\"Power\",\"u\":\"W\",\"v\":%s%d.%03d}"
            "]",
            device_name,
            px_sign, px_i, px_d, py_sign, py_i, py_d, pz_sign, pz_i, pz_d, 
            ex_sign, ex_i, ex_d, ey_sign, ey_i, ey_d, ez_sign, ez_i, ez_d, 
            ten_sign, ten_i, ten_d, pow_sign, pow_i, pow_d);

          mqtt_module_publish(mqtt_payload);
        }
                 
        sensor_buffer[0][sample_count] = plate.x; sensor_buffer[1][sample_count] = plate.y;
        sensor_buffer[2][sample_count] = plate.z; sensor_buffer[3][sample_count] = extruder.x;
        sensor_buffer[4][sample_count] = extruder.y; sensor_buffer[5][sample_count] = extruder.z;
        sensor_buffer[6][sample_count] = tension; sensor_buffer[7][sample_count] = power;
        
        sample_count++;
        
        if(sample_count == 5){
          leds_off(LEDS_NUM_TO_MASK(LEDS_YELLOW)); 
          LOG_INFO("Window full. Extracting features and running prediction...\n");
          
          uint8_t feature_idx = 0;
          for(uint8_t var = 0; var < 7; var++){
            float sum = 0.0f;
            float max_val = sensor_buffer[var][0];
            float min_val = sensor_buffer[var][0];
            
            for(uint8_t i = 0; i < 5; i++){
              float val = sensor_buffer[var][i];
              sum += val;
              if(val > max_val) max_val = val;
              if(val < min_val) min_val = val;
            }
            float mean = sum / 5.0f;
            float ptp = max_val - min_val;
            
            float variance_sum = 0.0f;
            for(uint8_t i = 0; i < 5; i++)
              variance_sum += (sensor_buffer[var][i] - mean) * (sensor_buffer[var][i] - mean);

            float std_dev = sqrt(variance_sum / 5.0f); 
            
            features[feature_idx++] = mean; features[feature_idx++] = std_dev;
            features[feature_idx++] = max_val; features[feature_idx++] = min_val;
            features[feature_idx++] = ptp;
          }
          
          for(uint8_t i = 0; i < 35; i++)
            features[i] = (features[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];
          
          LOG_INFO("Features scaled. Running inference...\n");          
          int32_t prediction = print_prediction_predict(features, 35);
          
          if(prediction == 1){
            error_count++;
            LOG_WARN("Anomaly Detected! (Alarm %d/3)\n", error_count);
          } else {
            LOG_INFO("Machine Learning Model: Regular print...\n");
            error_count = 0; 
          }

          if(error_count >= 3){
            LOG_ERR("EARLY STOPPING: Three consecutive anomalies detected. Printing stopped preemptively!\n");
            leds_off(LEDS_ALL);
            leds_on(LEDS_NUM_TO_MASK(LEDS_RED));

            remaining_print_time = etimer_expiration_time(&print_timer) - clock_time();

            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            sample_count = 0;
            sensor_deactivate();
            
            waiting_for_confirmation = true;
            strncpy(print_result, "FAILED", sizeof(print_result));
            LOG_INFO("Print aborted. Press the button to confirm and notify the server.\n");            
          } else {
            for(uint8_t var = 0; var < 8; var++)
              sensor_buffer[var][0] = sensor_buffer[var][4];
            sample_count = 1;
          }
        }
        
        sensor_sleep();
        if(!waiting_for_confirmation){
          etimer_reset(&sample_timer);
        }
      }
      else if(ev == PROCESS_EVENT_TIMER && data == &print_timer){
        LOG_INFO("Printing complete (simulated)\n");
        stl_length = 0; 
        
        leds_off(LEDS_ALL);
        leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN) | LEDS_NUM_TO_MASK(LEDS_YELLOW));
        
        etimer_stop(&sample_timer);
        sample_count = 0; error_count = 0;
        sensor_deactivate();

        waiting_for_confirmation = true;
        strncpy(print_result, "FINISHED", sizeof(print_result));
        LOG_INFO("Print finished successfully. Press the button to confirm and notify the server.\n");
      }
    }
  }

  PROCESS_END();
}