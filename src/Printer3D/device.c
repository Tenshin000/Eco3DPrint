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
#include "utility/sensors.h"
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

/*
typedef enum {
  STATE_OFF,
  STATE_INITIALIZATION,
  STATE_ONLINE,
  STATE_OFFLINE,
  STATE_PRINTING
} device_state_t;
*/

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

// Device Identity and Metadata
static char device_name[32];
static const char* device_type = "Filament";
static const char* device_utilization = "Printing";
static char registration_msg[128];
static char mqtt_payload[256];

// Printing Parameters
static size_t stl_length = 0;                 // Total STL bytes received
static bool waiting_for_confirmation = false; // True when user action is needed to confirm result
static char print_result[16];                 // Final print result: FAILED or FINISHED
static clock_time_t remaining_print_time = 0; // Remaining time when print is paused/stopped

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
/* =                     HELPERS                      = */
/* ==================================================== */ 
// Converts a device state to a readable string for logging
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

// Updates the current device state and changes LEDs accordingly
static void set_state(device_state_t new_state){
  if(current_state == new_state) return;

  last_state = current_state;
  current_state = new_state;

  // Turn off all LEDs before setting the new state indicator
  leds_off(LEDS_ALL);

  // Yellow means initialization, green means online/ready
  if(current_state == STATE_INITIALIZATION)
    leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
  else if(current_state == STATE_ONLINE)
    leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN));

  LOG_INFO("STATE: %s\n", state_to_string(current_state));
}

// Hook implementations requested by external modules
// These functions let other modules query or control the device state.
device_state_t device_get_state(void) { return current_state; }
const char* device_get_state_string(device_state_t state) { return state_to_string(state); }
void device_set_state(device_state_t new_state) { set_state(new_state); }
void device_trigger_mqtt_retry(void) { process_post(&smart_printer_process, event_mqtt_retry, NULL); }
void device_add_stl_length(int len) { stl_length += len; }
void device_trigger_print_simulation(void) { process_poll(&smart_printer_process); }
void device_reset_waiting_confirmation(void) { waiting_for_confirmation = false; }

// Calculates a simulated print duration based on STL file size
static uint32_t calculate_print_duration(size_t stl_size){
  uint32_t duration_seconds = (stl_size * 5) / 32;

  // Ensure a non-zero duration for non-empty STL files
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

  // Build a readable device name using the node ID
  uint16_t safe_id = (node_id > 0) ? (node_id - 1) : 0;
  snprintf(device_name, sizeof(device_name), "printer_%02u", safe_id);

  // Choose device type based on node ID
  if(node_id == 2 || node_id == 3)
    device_type = "Filament";
  else if(node_id == 4 || node_id == 5)
    device_type = "Resin";
  else{
    // Fallback rule if the node ID is outside the predefined range
    if(node_id % 2 != 0)
      device_type = "Filament";
    else
      device_type = "Resin";
  }

  // Start from a known OFF state
  current_state = STATE_OFF;
  last_state = STATE_OFF;
  LOG_INFO("STATE: OFF\n");

  // Initialize button hardware and retrieve the first button
  button_hal_init();
  btn = button_hal_get_by_index(0);

  if(btn){
    LOG_WARN("Setup: button initialized: %s on pin %u\n", BUTTON_HAL_GET_DESCRIPTION(btn), btn->pin);
    LOG_WARN("Short press to start SmartPrinter. Hold 3s to reset when active.\n");
  }
  else{
    LOG_WARN("Setup: no button available\n");
  }

  while(1){
    PROCESS_YIELD();

    // Short button press starts the smart printer process
    if(ev == button_hal_press_event){
      LOG_INFO("Setup: 'Button Pressed' Event\n");

      if(!smart_printer_active){
        // Move into initialization state and start the main application process
        set_state(STATE_INITIALIZATION);
        process_start(&smart_printer_process, NULL);
        smart_printer_active = true;
        process_poll(&smart_printer_process);
      }
      else{
        LOG_WARN("Smart Printer already active. Hold button 5 seconds to perform hard reset.\n");
        LOG_INFO("STATE: %s\n", state_to_string(current_state));
      }
    }

    // Periodic button events are used to detect short holds and report IPv6 address
    if(ev == button_hal_periodic_event && btn && data){
      button_hal_button_t *b = (button_hal_button_t *)data;
      if(b == btn){
        LOG_DBG("Setup: Button hold duration: %u s\n", b->press_duration_seconds);

        // A 1-second hold prints the current global IPv6 address
        if(b->press_duration_seconds == 1){
          char ipaddr_str[UIPLIB_IPV6_MAX_STR_LEN];
          uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);

          if(addr != NULL){
            uiplib_ipaddr_snprint(ipaddr_str, sizeof(ipaddr_str), &addr->ipaddr);
            LOG_INFO("Current IPv6 address: %s\n", ipaddr_str);
          }
          else{
            LOG_INFO("No global IPv6 address assigned yet\n");
          }
        }
      }
    }

    // Detect when the smart printer process exits and reset the system state
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

  // Initialize communication modules
  mqtt_module_init(&smart_printer_process, device_name);
  coap_module_init();

  // Get the main button for control actions
  btn0 = button_hal_get_by_index(0);
  if(!btn0) LOG_WARN("Smart Printer: No button available\n");

  // Build the registration payload used during CoAP registration
  snprintf(registration_msg, sizeof(registration_msg),
    "{\"name\":\"%s\",\"type\":\"%s\",\"utilization\":\"%s\"}",
    device_name, device_type, device_utilization);

  while(1){
    PROCESS_WAIT_EVENT();

    // Retry MQTT connection when requested or when the retry timer expires
    if(ev == event_mqtt_retry || (ev == PROCESS_EVENT_TIMER && data == &mqtt_timer)){
      if(!mqtt_module_is_connected() && (current_state == STATE_ONLINE || current_state == STATE_PRINTING)){
        mqtt_module_connect();
        etimer_set(&mqtt_timer, 10 * CLOCK_SECOND);
      }
    }

    /* BUTTON HANDLING */
    // Release events are used to confirm or override print outcomes
    if(ev == button_hal_release_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;

      if(btn == btn0){
        if(current_state == STATE_PRINTING && waiting_for_confirmation){
          char end_payload[64];
          int en_i = (int)total_power_consumed;
          int en_d = (int)(fabs(total_power_consumed - en_i) * 100);

          // Build a JSON payload with final status and energy usage
          snprintf(end_payload, sizeof(end_payload), "{\"status\":\"%s\",\"energy\":%d.%02d}", print_result, en_i, en_d);

          // Short press: confirm the current result and notify the server
          if(btn->press_duration_seconds < 2){
            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            stl_length = 0;
            error_count = 0;

            LOG_INFO("Button released (< 2s). Sending %s notification to server...\n", print_result);
            waiting_for_confirmation = false;

            coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
            COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
          }
          // Medium press: allow manual override of the ML decision
          else if(btn->press_duration_seconds >= 2 && btn->press_duration_seconds < 5){
            if(strcmp(print_result, "FAILED") == 0){
              // Resume printing if the user overrides a false anomaly alarm
              LOG_INFO("Button released (2-4s). Overriding ML Anomaly. Resuming print...\n");
              leds_off(LEDS_ALL);
              waiting_for_confirmation = false;
              error_count = 0;
              etimer_restart(&sample_timer);
              etimer_set(&print_timer, remaining_print_time);
            }
            else if(strcmp(print_result, "FINISHED") == 0){
              // Mark the print as failed even if it finished successfully
              LOG_INFO("Button released (2-4s). Manual override: Declaring print as FAILED.\n");
              waiting_for_confirmation = false;

              snprintf(end_payload, sizeof(end_payload), "{\"status\":\"FAILED\",\"energy\":%d.%02d}", en_i, en_d);
              coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
              COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
            }
          }
        }
      }
    }
    // Periodic button events are used to detect long holds for hard reset
    else if(ev == button_hal_periodic_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;

      if(btn == btn0){
        LOG_DBG("Smart Printer: Button hold duration: %u s\n", btn->press_duration_seconds);

        /* HARD RESET*/
        if(btn->press_duration_seconds >= 5 && current_state != STATE_OFF){
          LOG_INFO("Button held >= 5s -> Hard reset\n");

          // If printing, report an error before shutting down
          if(current_state == STATE_PRINTING){
            char reset_payload[64];
            int en_i = (int)total_power_consumed;
            int en_d = (int)(fabs(total_power_consumed - en_i) * 100);

            snprintf(reset_payload, sizeof(reset_payload), "{\"status\":\"ERROR\",\"energy\":%d.%02d}", en_i, en_d);

            uint16_t mid = coap_module_prepare_request(reset_payload, COAP_TYPE_NON, COAP_POST, END_PRINT_URI_PATH);
            coap_transaction_t* transaction = coap_new_transaction(mid, &server_ep);
            if(transaction){
              transaction->message_len = coap_serialize_message(request, transaction->message);
              coap_send_transaction(transaction);
            }
          }
          else{
            // If not printing, just notify the server that the device is turning off
            uint16_t mid = coap_module_prepare_request(NULL, COAP_TYPE_NON, COAP_POST, OFF_SIGNAL_URI_PATH);
            coap_transaction_t* off_transaction = coap_new_transaction(mid, &server_ep);
            if(off_transaction){
              off_transaction->message_len = coap_serialize_message(request, off_transaction->message);
              coap_send_transaction(off_transaction);
            }
          }

          // Clean up all runtime services and reset local state
          mqtt_module_disconnect();
          leds_off(LEDS_ALL);

          sample_count = 0;
          error_count = 0;
          stl_length = 0;
          total_power_consumed = 0.0f;
          waiting_for_confirmation = false;

          etimer_stop(&retry_timer);
          etimer_stop(&print_timer);
          etimer_stop(&sample_timer);

          set_state(STATE_OFF);
          PROCESS_EXIT();
        }
      }
    }

    // STATE MACHINE
    if(current_state == STATE_INITIALIZATION){
      // Initialization starts by sending a registration request to the server
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("Initialization requested -> sending registration request\n");

        sensors_init();
        sensor_sleep();

        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        LOG_INFO("Sending registration request...\n");
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);

        // If registration fails, schedule a retry
        if(current_state == STATE_OFFLINE){
          LOG_INFO("Retry in 10 seconds\n");
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        }
      }
    }
    else if(current_state == STATE_OFFLINE){
      // Retry registration every 10 seconds until the server accepts it
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
      // Online behavior is handled by the CoAP resource callbacks
    }
    else if(current_state == STATE_PRINTING){
      // Poll event means the full STL file was received and printing can start
      if(ev == PROCESS_EVENT_POLL){
        LOG_INFO("STL fully received. Starting physical print simulation...\n");
        sample_count = 0;
        total_power_consumed = 0.0f;

        // Compute the simulated print time from STL size
        uint32_t dynamic_print_time = calculate_print_duration(stl_length);
        LOG_INFO("Calculated print duration: %lu seconds for an STL of %zu bytes\n", (unsigned long)dynamic_print_time, stl_length);

        etimer_set(&print_timer, dynamic_print_time * CLOCK_SECOND);
        etimer_set(&sample_timer, 1 * CLOCK_SECOND);
      }
      // Periodic sampling event: read sensors, publish MQTT, and run ML prediction
      else if(ev == PROCESS_EVENT_TIMER && data == &sample_timer){
        if(waiting_for_confirmation) continue;

        leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
        sensor_activate();

        // Read all sensors used for anomaly detection
        accel_data_t plate = read_plate_acceleration();
        accel_data_t extruder = read_extruder_acceleration();
        float tension = read_tension();
        float power = read_power();

        total_power_consumed += power;

        LOG_INFO("Measurements: Plate(%.3f, %.3f, %.3f), Extruder(%.3f, %.3f, %.3f), Tension(%.3fV), Power(%.3fW)\n",
                 plate.x, plate.y, plate.z, extruder.x, extruder.y, extruder.z, tension, power);

        // Get the current IPv6 address to include it in MQTT telemetry
        char ipaddr_str[UIPLIB_IPV6_MAX_STR_LEN];
        uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);

        if(addr != NULL)
          uiplib_ipaddr_snprint(ipaddr_str, sizeof(ipaddr_str), &addr->ipaddr);
        else
          strcpy(ipaddr_str, "unknown");

        // Publish measurements through MQTT when connected
        if(mqtt_module_is_connected()){
          int px_i = (int)plate.x; int px_d = (int)(fabs(plate.x - px_i) * 1000);
          int py_i = (int)plate.y; int py_d = (int)(fabs(plate.y - py_i) * 1000);
          int pz_i = (int)plate.z; int pz_d = (int)(fabs(plate.z - pz_i) * 1000);
          int ex_i = (int)extruder.x; int ex_d = (int)(fabs(extruder.x - ex_i) * 1000);
          int ey_i = (int)extruder.y; int ey_d = (int)(fabs(extruder.y - ey_i) * 1000);
          int ez_i = (int)extruder.z; int ez_d = (int)(fabs(extruder.z - ez_i) * 1000);
          int ten_i = (int)tension; int ten_d = (int)(fabs(tension - ten_i) * 1000);
          int pow_i = (int)power;   int pow_d = (int)(fabs(power - pow_i) * 1000);

          // Preserve the sign for values between -1 and 0, where integer truncation would hide the minus sign
          const char* px_sign = (plate.x < 0 && px_i == 0) ? "-" : "";
          const char* py_sign = (plate.y < 0 && py_i == 0) ? "-" : "";
          const char* pz_sign = (plate.z < 0 && pz_i == 0) ? "-" : "";
          const char* ex_sign = (extruder.x < 0 && ex_i == 0) ? "-" : "";
          const char* ey_sign = (extruder.y < 0 && ey_i == 0) ? "-" : "";
          const char* ez_sign = (extruder.z < 0 && ez_i == 0) ? "-" : "";
          const char* ten_sign = (tension < 0 && ten_i == 0) ? "-" : "";
          const char* pow_sign = (power < 0 && pow_i == 0) ? "-" : "";

          // Build a JSON payload with all measured values
          snprintf(mqtt_payload, sizeof(mqtt_payload),
            "{\"ip\":\"%s\",\"X_Axis_Plate\":%s%d.%03d,\"Y_Axis_Plate\":%s%d.%03d,\"Z_Axis_Plate\":%s%d.%03d,\"X_Axis_Extrusion\":%s%d.%03d,\"Y_Axis_Extrusion\":%s%d.%03d,\"Z_Axis_Extrusion\":%s%d.%03d,\"Tension\":%s%d.%03d,\"Power\":%s%d.%03d}",
            ipaddr_str, px_sign, px_i, px_d, py_sign, py_i, py_d, pz_sign, pz_i, pz_d,
            ex_sign, ex_i, ex_d, ey_sign, ey_i, ey_d, ez_sign, ez_i, ez_d,
            ten_sign, ten_i, ten_d, pow_sign, pow_i, pow_d);

          mqtt_module_publish(mqtt_payload);
        }

        // Store the current sample in the rolling buffer
        sensor_buffer[0][sample_count] = plate.x; sensor_buffer[1][sample_count] = plate.y;
        sensor_buffer[2][sample_count] = plate.z; sensor_buffer[3][sample_count] = extruder.x;
        sensor_buffer[4][sample_count] = extruder.y; sensor_buffer[5][sample_count] = extruder.z;
        sensor_buffer[6][sample_count] = tension;   sensor_buffer[7][sample_count] = power;

        sample_count++;

        // When 5 samples are collected, compute features and run inference
        if(sample_count == 5){
          leds_off(LEDS_NUM_TO_MASK(LEDS_YELLOW));
          LOG_INFO("Window full. Extracting features and running prediction...\n");

          // Extract 5 statistics for each of the 7 variables: mean, std, max, min, peak-to-peak
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

            features[feature_idx++] = mean;
            features[feature_idx++] = std_dev;
            features[feature_idx++] = max_val;
            features[feature_idx++] = min_val;
            features[feature_idx++] = ptp;
          }

          // Scale features using precomputed scaler parameters
          for(uint8_t i = 0; i < 35; i++)
            features[i] = (features[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];

          LOG_INFO("Features scaled. Running inference...\n");
          int32_t prediction = print_prediction_predict(features, 35);

          // Prediction result: 1 means anomaly, 0 means normal
          if(prediction == 1){
            error_count++;
            LOG_WARN("Anomaly Detected! (Alarm %d/3)\n", error_count);
          }
          else{
            LOG_INFO("Machine Learning Model: Regular print...\n");
            error_count = 0;
          }

          // Stop the print after 3 consecutive anomaly detections
          if(error_count >= 3){
            LOG_ERR("EARLY STOPPING: Three consecutive anomalies detected. Printing stopped preemptively!\n");
            leds_off(LEDS_ALL);
            leds_on(LEDS_NUM_TO_MASK(LEDS_RED));

            // Save how much time is left so the print can be resumed later
            remaining_print_time = etimer_expiration_time(&print_timer) - clock_time();

            etimer_stop(&sample_timer);
            etimer_stop(&print_timer);
            sample_count = 0;
            sensor_deactivate();

            // Wait for user confirmation before notifying the server
            waiting_for_confirmation = true;
            strncpy(print_result, "FAILED", sizeof(print_result));
            LOG_INFO("Print aborted. Press the button to confirm and notify the server.\n");
          }
          else{
            // Keep the last sample as the first sample for the next sliding window
            for(uint8_t var = 0; var < 8; var++)
              sensor_buffer[var][0] = sensor_buffer[var][4];
            sample_count = 1;
          }
        }

        // Put sensors back to sleep after each sample cycle
        sensor_sleep();

        // Continue sampling only if confirmation is not pending
        if(!waiting_for_confirmation){
          etimer_reset(&sample_timer);
        }
      }
      // Print timer expired: printing is complete
      else if(ev == PROCESS_EVENT_TIMER && data == &print_timer){
        LOG_INFO("Printing complete (simulated)\n");
        stl_length = 0;

        // Show completion using green and yellow LEDs together
        leds_off(LEDS_ALL);
        leds_on(LEDS_NUM_TO_MASK(LEDS_GREEN) | LEDS_NUM_TO_MASK(LEDS_YELLOW));

        etimer_stop(&sample_timer);
        sample_count = 0;
        error_count = 0;
        sensor_deactivate();

        // Wait for button confirmation before sending completion status to server
        waiting_for_confirmation = true;
        strncpy(print_result, "FINISHED", sizeof(print_result));
        LOG_INFO("Print finished successfully. Press the button to confirm and notify the server.\n");
      }
    }
  }

  PROCESS_END();
}