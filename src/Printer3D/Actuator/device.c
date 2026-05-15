/* Contiki core */
#include "contiki.h"
#include "sys/log.h"
#include "sys/node-id.h"

/* Hardware */
#include "os/dev/button-hal.h"
#include "os/dev/leds.h"
#include "os/dev/serial-line.h"

#ifdef DEV_DONGLE
#include "usb/usb-serial.h" 
#endif

/* Networking (IPv6 / uIP) */
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"

/* Standard C libraries */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Project Utility */
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
static device_state_t current_state = STATE_OFF;
static device_state_t last_state = STATE_OFF;

// Timers
static struct etimer retry_timer; 
static struct etimer print_timer; 
static struct etimer mqtt_timer; 
static struct etimer sensor_pairing_timer; 
static struct etimer subscribe_timer; 

// Device Parameters
static char device_name[32];
static const char* device_type = "Filament";
static const char* device_utilization = "Printing";
static char registration_msg[256]; 

// Printing Parameters
static size_t stl_length = 0;
static bool waiting_for_confirmation = false;
static char print_result[16];
static clock_time_t remaining_print_time = 0;
static uint32_t current_print_duration = 0;
static bool print_pending = false;

// Track if the print timer is actively managing a job to prevent mid-print overwrites
static bool print_timer_started = false; 

// Sensing & ML Variables
static bool init_pairing_phase = true;
static bool sensor_discovery_attempted = false;
static float sensor_buffer[8][5]; 
static uint8_t sample_count = 0;
static float features[35];
static uint8_t error_count = 0;
static float total_power_consumed = 0.0f;

// Safe MQTT Subscription tracking
static bool has_active_subscription = false; 
static char target_topic[128]; 

process_event_t event_start_smart_printer;
process_event_t event_stl_received;

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
  else if(current_state == STATE_ONLINE) leds_on(LEDS_GREEN);
  else if(current_state == STATE_PRINTING && !sensor_is_paired) leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW) | LEDS_GREEN);
  
  if(current_state == STATE_OFF){
    mqtt_module_disconnect();
    has_active_subscription = false;
  } 
  else {
    if(!mqtt_module_is_connected()){
      device_trigger_mqtt_retry(); 
    }
  }
  
  LOG_INFO("STATE: %s\n", state_to_string(current_state));
  #ifdef DEV_DONGLE
  usb_serial_flush(); 
  #endif
}

device_state_t device_get_state(void){ return current_state; }
const char* device_get_state_string(device_state_t state){ return state_to_string(state); }
void device_set_state(device_state_t new_state){ set_state(new_state); }
void device_trigger_mqtt_retry(void){ process_post(&smart_printer_process, event_mqtt_retry, NULL); }
void device_add_stl_length(int len){ stl_length += len; }
void device_reset_waiting_confirmation(void){ waiting_for_confirmation = false; }

void device_trigger_print_simulation(void){ 
    if(current_state == STATE_ONLINE) {
        print_pending = true;
        process_poll(&smart_printer_process); 
    }
}

static uint32_t calculate_print_duration(size_t stl_size){
  uint32_t duration_seconds = (stl_size * 5) / 32;
  if(duration_seconds == 0 && stl_size > 0) duration_seconds = 1;
  return duration_seconds;
}

static float extract_json_value(const char* json, const char* key_name) {
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "\"n\":\"%s\"", key_name);
    char* n_pos = strstr(json, search_str);
    if (n_pos) {
        char* v_pos = strstr(n_pos, "\"v\":");
        if (v_pos) return atof(v_pos + 4);
    }
    return 0.0f;
}

/* ==================================================== */
/* =                  SETUP PROCESS                   = */
/* ==================================================== */ 
PROCESS_THREAD(setup_process, ev, data){
  static button_hal_button_t *btn;
  static bool smart_printer_active = false;

  PROCESS_BEGIN();
  
  event_start_smart_printer = process_alloc_event();
  event_stl_received = process_alloc_event();

  LOG_INFO("Smart Printer SETUP process starting...\n");
  #ifdef DEV_DONGLE
  usb_serial_set_input(serial_line_input_byte); 
  #endif

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
  button_hal_init();
  btn = button_hal_get_by_index(0);

  if(btn){
    LOG_WARN("Setup: button initialized: %s on pin %u\n", BUTTON_HAL_GET_DESCRIPTION(btn), btn->pin);
    LOG_WARN("Short press to start SmartPrinter. Hold 5s to reset when active.\n");
  } 

  while(1){
    PROCESS_YIELD();

    if(ev == button_hal_press_event){
      if(!smart_printer_active){
        set_state(STATE_INITIALIZATION);
        process_start(&smart_printer_process, NULL);
        smart_printer_active = true;
        process_post(&smart_printer_process, event_start_smart_printer, NULL);
      } 
      else{
        LOG_WARN("Smart Printer already active. Hold button 5 seconds to perform hard reset.\n");
      }
    }

    if(ev == button_hal_periodic_event && btn && data){
      button_hal_button_t *b = (button_hal_button_t *)data;
      if(b == btn){
        LOG_DBG("Setup: Button hold duration: %u s\n", b->press_duration_seconds);
      }
    }

    // Handles the graceful shutdown logic when smart_printer_process yields PROCESS_EXIT()
    if(ev == PROCESS_EVENT_EXITED){
      struct process *exited = (struct process *)data;
      if(exited == &smart_printer_process){
        LOG_WARN("Smart Printer thread exited\n");
        smart_printer_active = false;
        sensor_is_paired = false;
        init_pairing_phase = true; 
        sensor_discovery_attempted = false; 
        print_pending = false;
        has_active_subscription = false; 
        print_timer_started = false; 
        
        memset(paired_sensor_ip_str, 0, sizeof(paired_sensor_ip_str));
        set_state(STATE_OFF);
        LOG_INFO("Device reset to STATE: OFF - await short press to start again\n");
      }
    }

    #ifdef DEV_DONGLE
    usb_serial_flush();
    #endif
  }

  PROCESS_END();
}

/* ==================================================== */
/* =              SMART PRINTER PROCESS               = */
/* ==================================================== */ 
PROCESS_THREAD(smart_printer_process, ev, data){
  static button_hal_button_t *btn0 = NULL;
  PROCESS_BEGIN();

  mqtt_module_init(&smart_printer_process, device_name);
  coap_module_init();
  btn0 = button_hal_get_by_index(0);

  while(1){
    PROCESS_WAIT_EVENT();

    /* GUARANTEED PRINT INITIATION CHECK */
    if(print_pending && current_state == STATE_ONLINE) {
        print_pending = false;
        set_state(STATE_PRINTING);
        LOG_INFO("STL fully received. Starting print sequence...\n");
        
        // Setup initial variables for the new print job
        sample_count = 0; error_count = 0; total_power_consumed = 0.0f;

        current_print_duration = calculate_print_duration(stl_length);
        stl_length = 0; 
        print_timer_started = false; // Clean slate for a completely new print
        
        if(sensor_is_paired) {
            snprintf(target_topic, sizeof(target_topic), "%s/print/measurements", paired_sensor_ip_str);
            // Trigger the safe subscription flow
            etimer_set(&subscribe_timer, 1 * CLOCK_SECOND); 
        } else {
            LOG_WARN("No sensor paired. Proceeding with print without ML telemetry.\n");
            etimer_set(&print_timer, current_print_duration * CLOCK_SECOND);
            print_timer_started = true; // The timer is now actively shielding the print job
        }
    }
    
    /* DYNAMIC SENSOR UNPAIRING LOGIC */
    if(ev == event_sensor_unpaired) {
        LOG_WARN("Sensor sent OFF signal or timed out! Continuing print alone & Re-registering with Cloud...\n");
        sensor_is_paired = false;
        
        // FIX: Use static! Local variables are lost when COAP_BLOCKING_REQUEST yields the protothread.
        static bool was_printing_unpaired;
        was_printing_unpaired = (current_state == STATE_PRINTING);

        snprintf(registration_msg, sizeof(registration_msg),
            "[{\"bn\":\"%s\",\"n\":\"type\",\"vs\":\"%s\"},{\"n\":\"utilization\",\"vs\":\"%s\"},{\"n\":\"sensor_ip\",\"vs\":\"NULL\"}]",
            device_name, device_type, device_utilization);
        
        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);
        
        mqtt_module_disconnect();
        has_active_subscription = false; 
        
        // Resume standalone printing if we were interrupted mid-print
        if(was_printing_unpaired && !waiting_for_confirmation) {
            
            // If the CoAP handler accidentally changed our state to ONLINE, force it back silently.
            if(current_state != STATE_PRINTING) {
                current_state = STATE_PRINTING; 
            }
            
            sample_count = 0;
            error_count = 0;
            etimer_stop(&subscribe_timer);
            
            leds_off(LEDS_ALL);
            leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW) | LEDS_GREEN);
        }
    }
    
    /* PAIRING SUCCESS LOGIC */
    if(ev == event_sensor_paired) {
        LOG_INFO("Handling sensor paired event. Updating Cloud...\n");
        sensor_is_paired = true;
        init_pairing_phase = false;
        
        // FIX: Use static! Local variables are lost when COAP_BLOCKING_REQUEST yields the protothread.
        static bool was_printing_paired;
        was_printing_paired = (current_state == STATE_PRINTING);

        snprintf(registration_msg, sizeof(registration_msg),
            "[{\"bn\":\"%s\",\"n\":\"type\",\"vs\":\"%s\"},{\"n\":\"utilization\",\"vs\":\"%s\"},{\"n\":\"sensor_ip\",\"vs\":\"%s\"}]",
            device_name, device_type, device_utilization, paired_sensor_ip_str);
            
        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);
        
        // Smoothly re-integrate the sensor into the active print job without resetting the job!
        if(was_printing_paired && !waiting_for_confirmation) {
            
            if(current_state != STATE_PRINTING) {
                current_state = STATE_PRINTING;
            }
            
            snprintf(target_topic, sizeof(target_topic), "%s/print/measurements", paired_sensor_ip_str);
            
            // Force MQTT re-subscription and START signal logic
            has_active_subscription = false; 
            etimer_set(&subscribe_timer, 1 * CLOCK_SECOND);
            
            leds_off(LEDS_ALL);
            leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
        }
    }
    
    /* PERIODIC SENSOR PAIRING RETRY EVENT */
    if(ev == PROCESS_EVENT_TIMER && data == &sensor_pairing_timer) {
        if(!sensor_is_paired && current_state != STATE_OFF) {
            if(current_state == STATE_INITIALIZATION && init_pairing_phase) {
                init_pairing_phase = false;
                LOG_INFO("Initial pairing timeout. Proceeding with NULL sensor.\n");
                snprintf(registration_msg, sizeof(registration_msg),
                    "[{\"bn\":\"%s\",\"n\":\"type\",\"vs\":\"%s\"},{\"n\":\"utilization\",\"vs\":\"%s\"},{\"n\":\"sensor_ip\",\"vs\":\"NULL\"}]",
                    device_name, device_type, device_utilization);
                process_post(&smart_printer_process, event_start_smart_printer, NULL); 
            } 
        }
    }

    /* INCOMING MQTT DATA FROM SENSOR & ML COORDINATION */
    if(ev == event_mqtt_incoming && current_state == STATE_PRINTING){
      if(sensor_is_paired) {
          const char* payload = (const char*)data;
          
          float px = extract_json_value(payload, "X_Axis_Plate");
          float py = extract_json_value(payload, "Y_Axis_Plate");
          float pz = extract_json_value(payload, "Z_Axis_Plate");
          float ex = extract_json_value(payload, "X_Axis_Extrusion");
          float ey = extract_json_value(payload, "Y_Axis_Extrusion");
          float ez = extract_json_value(payload, "Z_Axis_Extrusion");
          float tension = extract_json_value(payload, "Tension");
          float power = extract_json_value(payload, "Power");
          
          int pow_i = (int)power; 
          int pow_d = (int)(fabs(power - pow_i) * 1000);
          LOG_INFO("MQTT Power Extracted: %d.%03d W\n", pow_i, pow_d);
          LOG_INFO("Sample Count: %d\n", sample_count);
          
          total_power_consumed += power;
          
          sensor_buffer[0][sample_count] = px; sensor_buffer[1][sample_count] = py;
          sensor_buffer[2][sample_count] = pz; sensor_buffer[3][sample_count] = ex;
          sensor_buffer[4][sample_count] = ey; sensor_buffer[5][sample_count] = ez;
          sensor_buffer[6][sample_count] = tension; sensor_buffer[7][sample_count] = power;
          
          sample_count++;
          
          // ML INFERENCE BATCH - Ping-Pong Coordination (Exactly 5)
          if(sample_count >= 5){
              leds_off(LEDS_NUM_TO_MASK(LEDS_YELLOW));
              LOG_INFO("ML Batch full. Running Inference...\n");
              
              uint8_t feature_idx = 0;
              for(uint8_t var = 0; var < 7; var++){
                float sum = 0.0f; float max_val = sensor_buffer[var][0]; float min_val = sensor_buffer[var][0];
                for(uint8_t i = 0; i < 5; i++){
                  float val = sensor_buffer[var][i];
                  sum += val;
                  if(val > max_val) max_val = val;
                  if(val < min_val) min_val = val;
                }
                float mean = sum / 5.0f; float ptp = max_val - min_val;
                float variance_sum = 0.0f;
                for(uint8_t i = 0; i < 5; i++) variance_sum += (sensor_buffer[var][i] - mean) * (sensor_buffer[var][i] - mean);

                float std_dev = sqrt(variance_sum / 5.0f); 
                features[feature_idx++] = mean; features[feature_idx++] = std_dev;
                features[feature_idx++] = max_val; features[feature_idx++] = min_val;
                features[feature_idx++] = ptp;
              }
              
              for(uint8_t i = 0; i < 35; i++)
                features[i] = (features[i] - SCALER_MEANS[i]) / SCALER_SCALES[i];
                
              int32_t prediction = print_prediction_predict(features, 35);
              
              if(prediction == 1){
                error_count++;
                LOG_WARN("Anomaly Detected! (Alarm %d/3)\n", error_count);
              } else {
                error_count = 0; 
              }

              if(error_count >= 3){
                LOG_ERR("EARLY STOPPING: Anomalies detected. Halting Sensor & Printer!\n");
                
                coap_module_send_sensor_command_non_blocking("STOP");
                leds_off(LEDS_ALL); leds_on(LEDS_RED);

                if(etimer_expired(&print_timer)){
                    remaining_print_time = current_print_duration * CLOCK_SECOND;
                } else {
                    remaining_print_time = etimer_expiration_time(&print_timer) - clock_time();
                }
                
                etimer_stop(&print_timer);
                etimer_stop(&subscribe_timer);
                
                sample_count = 0;
                waiting_for_confirmation = true;
                strncpy(print_result, "FAILED", sizeof(print_result));
                LOG_INFO("Print aborted. Press the button to confirm and notify the server.\n");
              } else {
                for(uint8_t var = 0; var < 8; var++) sensor_buffer[var][0] = sensor_buffer[var][4];
                sample_count = 1; 
                leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
                
                LOG_INFO("ML Verdict: OK. Sending CONTINUE to sensor.\n");
                coap_module_send_sensor_command_non_blocking("CONT");
              }
          }
      }
    }

    /* BUTTON HANDLING (OFF Logic) */
    if(ev == button_hal_release_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;
      if(btn == btn0){
        if(current_state == STATE_PRINTING && waiting_for_confirmation){
          char end_payload[128]; 
          int en_i = (int)total_power_consumed; 
          int en_d = (int)(fabs(total_power_consumed - en_i) * 100);
          
          snprintf(end_payload, sizeof(end_payload), 
                   "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"%s\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                   device_name, print_result, en_i, en_d);

          if(btn->press_duration_seconds < 2){
            etimer_stop(&print_timer);
            error_count = 0;
            LOG_INFO("Button released (< 2s). Sending %s notification to server...\n", print_result);
            waiting_for_confirmation = false;
            coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
            COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
          }
          else if(btn->press_duration_seconds >= 2 && btn->press_duration_seconds < 5){
            if(strcmp(print_result, "FAILED") == 0){
              LOG_INFO("Button released (2-4s). Overriding ML Anomaly. Restarting print...\n");
              leds_off(LEDS_ALL);
              waiting_for_confirmation = false;
              error_count = 0; 
              
              etimer_set(&print_timer, remaining_print_time); 
              print_timer_started = true; // Shield the timer
              
              // If we override the anomaly while paired, we must cleanly re-initialize the MQTT flow
              if(sensor_is_paired) {
                  snprintf(target_topic, sizeof(target_topic), "%s/print/measurements", paired_sensor_ip_str);
                  has_active_subscription = false; 
                  etimer_set(&subscribe_timer, 1 * CLOCK_SECOND);
              } else {
                  leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW) | LEDS_GREEN);
              }
            }
            else if(strcmp(print_result, "FINISHED") == 0){
              waiting_for_confirmation = false;
              snprintf(end_payload, sizeof(end_payload), 
                       "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"FAILED\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                       device_name, en_i, en_d);
              coap_module_prepare_request(end_payload, COAP_TYPE_CON, COAP_POST, END_PRINT_URI_PATH);
              COAP_BLOCKING_REQUEST(&server_ep, request, print_finished_handler);
              sample_count = 0;
            }
          }
        }
      }
    }
    else if(ev == button_hal_periodic_event && btn0 && data){
      button_hal_button_t *btn = (button_hal_button_t *)data;
      if(btn == btn0){
        // Handling Hard Reset Trigger
        if(btn->press_duration_seconds >= 5 && current_state != STATE_OFF){
          LOG_INFO("Button held >= 5s -> Hard reset\n");
          if(current_state == STATE_PRINTING){
            if(sensor_is_paired) {
                coap_module_send_sensor_command_non_blocking("STOP");
            }
            char reset_payload[128];
            int en_i = (int)total_power_consumed;
            int en_d = (int)(fabs(total_power_consumed - en_i) * 100);

            snprintf(reset_payload, sizeof(reset_payload), 
                     "[{\"bn\":\"%s\",\"n\":\"status\",\"vs\":\"ERROR\"},{\"n\":\"energy\",\"u\":\"J\",\"v\":%d.%02d}]", 
                     device_name, en_i, en_d);
            uint16_t mid = coap_module_prepare_request(reset_payload, COAP_TYPE_NON, COAP_POST, END_PRINT_URI_PATH);
            coap_transaction_t* transaction = coap_new_transaction(mid, &server_ep);
            if(transaction){
              transaction->message_len = coap_serialize_message(request, transaction->message);
              coap_send_transaction(transaction);
            }
          } 
          else{
            uint16_t mid = coap_module_prepare_request(NULL, COAP_TYPE_NON, COAP_POST, OFF_SIGNAL_URI_PATH);
            coap_transaction_t* off_transaction = coap_new_transaction(mid, &server_ep);
            if(off_transaction){
              off_transaction->message_len = coap_serialize_message(request, off_transaction->message);
              coap_send_transaction(off_transaction);
            }
          }

          if(sensor_is_paired) {
            uint16_t unpair_mid = coap_module_prepare_unpair();
            coap_transaction_t* unpair_trans = coap_new_transaction(unpair_mid, &sensor_ep);
            if(unpair_trans){
              unpair_trans->message_len = coap_serialize_message(request, unpair_trans->message);
              coap_send_transaction(unpair_trans);
            }
          }

          mqtt_module_disconnect();
          has_active_subscription = false;
          leds_off(LEDS_ALL);
          
          sample_count = 0; error_count = 0; stl_length = 0; total_power_consumed = 0.0f;
          waiting_for_confirmation = false; 
          print_timer_started = false;

          etimer_stop(&retry_timer); 
          etimer_stop(&print_timer); 
          etimer_stop(&sensor_pairing_timer);
          etimer_stop(&subscribe_timer);
          
          set_state(STATE_OFF);
          
          // Kills the process, triggering the exit event in setup_process.
          PROCESS_EXIT();
        }
      }
    }

    /* STATE MACHINE */
    if(current_state == STATE_INITIALIZATION){
      if(ev == PROCESS_EVENT_INIT || ev == event_start_smart_printer || (ev == PROCESS_EVENT_TIMER && data == &retry_timer)){
        if(!sensor_discovery_attempted) {
            LOG_INFO("Attempting initial pairing with Sensor...\n");
            coap_module_prepare_discovery();
            COAP_BLOCKING_REQUEST(&multicast_ep, request, discovery_handler);
            sensor_discovery_attempted = true;
            
            etimer_set(&sensor_pairing_timer, 3 * CLOCK_SECOND);
        } 
        else {
            coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
            LOG_INFO("Sending registration request to Server...\n");
            COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);

            if(current_state == STATE_OFFLINE) {
              etimer_set(&retry_timer, 10 * CLOCK_SECOND);
            } 
        }
      }
    }
    else if(current_state == STATE_OFFLINE){
      if(ev == PROCESS_EVENT_TIMER && data == &retry_timer){
        coap_module_prepare_request(registration_msg, COAP_TYPE_CON, COAP_POST, REG_URI_PATH);
        COAP_BLOCKING_REQUEST(&server_ep, request, registration_handler);
        
        if(current_state != STATE_ONLINE){
          etimer_set(&retry_timer, 10 * CLOCK_SECOND);
        } 
      }
    }

    /* SAFE SUBSCRIPTION & TIMERS DURING PRINTING */
    if(current_state == STATE_PRINTING){
      if(ev == PROCESS_EVENT_TIMER && data == &subscribe_timer){
        if(!mqtt_module_is_connected()) {
            LOG_INFO("Waiting for MQTT connection...\n");
            device_trigger_mqtt_retry();
            etimer_set(&subscribe_timer, 2 * CLOCK_SECOND);
        } else {
            if(!has_active_subscription) {
                LOG_INFO("Subscribing to telemetry on topic: %s\n", target_topic);
                mqtt_module_subscribe(target_topic);
                has_active_subscription = true;
                // Wait 1 second for SUBACK before sending START CoAP
                etimer_set(&subscribe_timer, 1 * CLOCK_SECOND);
            } else {
                LOG_INFO("MQTT Ready! Awakening Sensor...\n");
                coap_module_send_sensor_command_non_blocking("START");
                
                // Only set the print_timer if it hasn't been started yet.
                // This allows mid-print pairing to resume ML without restarting the total print time!
                if(!print_timer_started) {
                    etimer_set(&print_timer, current_print_duration * CLOCK_SECOND);
                    print_timer_started = true;
                }
            }
        }
      }
      else if(ev == PROCESS_EVENT_TIMER && data == &print_timer){
        LOG_INFO("Printing complete (simulated)\n");
        
        leds_off(LEDS_ALL);
        leds_on(LEDS_GREEN | LEDS_NUM_TO_MASK(LEDS_YELLOW));
        
        if(sensor_is_paired) {
            coap_module_send_sensor_command_non_blocking("STOP");
        }
        
        waiting_for_confirmation = true;
        strncpy(print_result, "FINISHED", sizeof(print_result));
        LOG_INFO("Print finished successfully. Press the button to confirm and notify the server.\n");
        
        sample_count = 0; error_count = 0;
      }
    }

    /* CONSTANT MQTT CONNECTION LOGIC */
    if(ev == event_mqtt_retry || (ev == PROCESS_EVENT_TIMER && data == &mqtt_timer)){
      if(!mqtt_module_is_connected() && current_state != STATE_OFF){
        has_active_subscription = false; // Reset sub flag to force resubscribe if dropped
        mqtt_module_connect();
        
        // If it dropped while mid-print, make sure we resubscribe automatically
        if(current_state == STATE_PRINTING) {
            etimer_set(&subscribe_timer, 2 * CLOCK_SECOND);
        }
      }
      
      // Loop the timer continuously unless the device is OFF
      if(current_state != STATE_OFF) {
          etimer_set(&mqtt_timer, 10 * CLOCK_SECOND);
      }
    }
  }

  PROCESS_END();
}