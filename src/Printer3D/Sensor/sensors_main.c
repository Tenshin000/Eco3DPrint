/* Contiki core */
#include "contiki.h"
#include "sys/log.h"
#include "sys/node-id.h"

/* Hardware */
#include "os/dev/button-hal.h"
#include "os/dev/leds.h"

/* Networking (IPv6 / uIP) */
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"

/* Standard C libraries */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "lib/random.h"

/* Custom Modules */
#include "utility/coap_module.h"
#include "utility/mqtt_module.h"

// Smart Sensor Log
#define LOG_MODULE "Smart Sensor"
#define LOG_LEVEL LOG_LEVEL_APP

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Cross-platform LED compatibility macros to ensure correct behavior across simulation and hardware
#ifdef DEV_COOJA
  #define PLATFORM_LED_GREEN LEDS_NUM_TO_MASK(LEDS_GREEN)
  #define PLATFORM_LED_RED   LEDS_NUM_TO_MASK(LEDS_RED)
#else
  #define PLATFORM_LED_GREEN LEDS_GREEN
  #define PLATFORM_LED_RED   LEDS_RED
#endif

/* ==================================================== */ 
/* =            SENSOR SIMULATION STRUCTURES          = */ 
/* ==================================================== */ 

// Structure to hold 3-axis accelerometer data
typedef struct {
    float x;
    float y;
    float z;
} accel_data_t;

// Printer operating state simulation
typedef enum {
    PRINTER_STATE_NORMAL,
    PRINTER_STATE_ERROR
} printer_state_t;

// Sensor state machine definition
typedef enum {
    SENSOR_STATE_OFF = 0,
    SENSOR_STATE_INIT = 1,
    SENSOR_STATE_SLEEP = 2,
    SENSOR_STATE_ACTIVE = 3
} sensor_state_t;

/* ==================================================== */ 
/* =                  CONFIGURATION                   = */ 
/* ==================================================== */ 

// Simulated environment variables
static printer_state_t simulated_printer_state = PRINTER_STATE_NORMAL; 
static float current_power_draw_watts = 150.0; 

// Device and MQTT configuration variables
static sensor_state_t current_state = SENSOR_STATE_OFF;
static char device_name[32];
static char mqtt_payload[512];
static char pub_topic[128]; 

// Timers for sampling, networking, and pairing
static struct etimer sampling_timer;
static struct etimer mqtt_retry_timer;      
static struct etimer pairing_timer;         
static struct etimer health_check_timer;

// ML Batch Synchronization (Ping-Pong logic with Printer)
static uint8_t samples_sent_in_batch = 0;
static uint8_t target_batch_size = 5;
static uint8_t health_fail_count = 0;

// Process events for external triggers
process_event_t event_start_smart_sensor;

/* ==================================================== */
/* =             SENSOR HARDWARE SIMULATION           = */
/* ==================================================== */

// Generate a normally distributed random float
static float get_gaussian_float(float mean, float std_dev){
    float u1 = (float)random_rand() / RANDOM_RAND_MAX;
    float u2 = (float)random_rand() / RANDOM_RAND_MAX;
    float z0;
    float final_value;
    
    if(u1 <= 0.0001f) u1 = 0.0001f;
    z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    final_value = mean + z0 * std_dev;
    return final_value;
}

// Generate a bounded normally distributed random float
float get_bounded_gaussian(float mean, float std_dev, float limit_val){
    float generated_val = get_gaussian_float(mean, std_dev);
    float max_val = mean + limit_val;
    float min_val = mean - limit_val;
    
    if(generated_val > max_val) generated_val = max_val;
    else if(generated_val < min_val) generated_val = min_val;
    return generated_val;
}

// Generate a uniform probability between 0 and 1
static float get_uniform_probability(void){
    return (float)random_rand() / (float)RANDOM_RAND_MAX;
}

// Initialize the random seed for the simulated sensors
void sensors_init(void){
    random_init((unsigned short)clock_time());
}

// Randomly trigger natural transitions between normal and error states during active simulation
void sensor_activate(void){
    float roll = get_uniform_probability();

    if(simulated_printer_state == PRINTER_STATE_NORMAL){
        if(roll < 0.005f){ 
            simulated_printer_state = PRINTER_STATE_ERROR;
            LOG_DBG("SIMULATION: Natural transition to ERROR state!\n");
        } 
        else simulated_printer_state = PRINTER_STATE_NORMAL;
    } 
    else{
        if(roll < 0.20f){ 
            simulated_printer_state = PRINTER_STATE_NORMAL;
            LOG_DBG("SIMULATION: Natural recovery to NORMAL state.\n");
        } 
        else simulated_printer_state = PRINTER_STATE_ERROR;
    }
}

void sensor_sleep(void){}
void sensor_deactivate(void){}

// Read simulated plate acceleration data
accel_data_t read_plate_acceleration(void){
    accel_data_t data_value;
    if(simulated_printer_state == PRINTER_STATE_NORMAL){
        data_value.x = get_bounded_gaussian(-0.35, 0.20, 0.65);
        data_value.y = get_bounded_gaussian(-0.20, 0.15, 0.50);
        data_value.z = get_bounded_gaussian(9.67, 0.05, 0.20);
    } 
    else{
        data_value.x = get_gaussian_float(-0.23, 0.24); 
        data_value.y = get_gaussian_float(-0.25, 0.26); 
        data_value.z = get_gaussian_float(9.81, 0.32);  
    }
    return data_value;
}

// Read simulated extruder acceleration data
accel_data_t read_extruder_acceleration(void){
    accel_data_t data_value;
    if(simulated_printer_state == PRINTER_STATE_NORMAL){
        data_value.x = get_bounded_gaussian(0.25, 0.30, 1.10);
        data_value.y = get_bounded_gaussian(0.32, 0.40, 1.40);
        data_value.z = get_bounded_gaussian(-9.78, 0.18, 0.75);
    } 
    else{
        data_value.x = get_gaussian_float(0.27, 1.40); 
        data_value.y = get_gaussian_float(0.35, 1.45); 
        data_value.z = get_gaussian_float(-9.81, 0.75);
    }
    return data_value;
}

// Read simulated voltage tension
float read_tension(void){
    if(simulated_printer_state == PRINTER_STATE_NORMAL) return get_gaussian_float(331.0, 15.0);
    else return get_gaussian_float(312.0, 50.0); 
}

// Read simulated power consumption
float read_power(void){
    float delta;
    if(simulated_printer_state == PRINTER_STATE_NORMAL) delta = get_gaussian_float(0.0, 2.5); 
    else delta = get_gaussian_float(0.0, 15.0); 
    
    current_power_draw_watts += delta;
    if(current_power_draw_watts < 120.0) current_power_draw_watts = 120.0;
    if(current_power_draw_watts > 180.0) current_power_draw_watts = 180.0;
    return current_power_draw_watts;
}

/* ==================================================== */
/* =                 UTILITY FUNCTIONS                = */
/* ==================================================== */ 
PROCESS(setup_process, "Setup Process");
PROCESS(smart_sensor_process, "Smart Sensor Process");
PROCESS(health_check_process, "Health Check Process");
AUTOSTART_PROCESSES(&setup_process);

// Trigger an MQTT reconnection attempt
void sensor_trigger_mqtt_retry(void){ 
    process_post(&smart_sensor_process, event_mqtt_retry, NULL); 
}

// Update the sensor state and handle LED/hardware transitions
static void set_sensor_state(sensor_state_t new_state){
    if(current_state == new_state) return;
    current_state = new_state;

    leds_off(LEDS_ALL);
    switch(current_state){
        case SENSOR_STATE_OFF:
            LOG_INFO("STATE: OFF\n");
            sensor_mqtt_disconnect();            
            sensor_deactivate();
            break;
        case SENSOR_STATE_INIT:
            LOG_INFO("STATE: INITIALIZATION\n");
            leds_on(LEDS_NUM_TO_MASK(LEDS_YELLOW));
            sensors_init();
            if(!sensor_mqtt_is_connected()) sensor_trigger_mqtt_retry();
            break;
        case SENSOR_STATE_SLEEP:
            LOG_INFO("STATE: SLEEP\n");
            sensor_sleep();
            if(!sensor_mqtt_is_connected()) sensor_trigger_mqtt_retry();
            break;
        case SENSOR_STATE_ACTIVE:
            LOG_INFO("STATE: ACTIVE\n");
            leds_on(PLATFORM_LED_GREEN);
            sensor_activate();
            if(!sensor_mqtt_is_connected()) sensor_trigger_mqtt_retry();
            break;
    }
}

// Return the current sensor state
uint8_t get_sensor_state(void){
    return (uint8_t)current_state;
}

// Callback for the Parallel Health Check Process
void health_response_handler(coap_message_t* response){
    if(response == NULL){
        health_fail_count++;
        LOG_WARN("Health check failed (%d/3)\n", health_fail_count);
        if(health_fail_count >= 3){
            LOG_ERR("Actuator dead or unresponsive! Unpairing...\n");
            process_post(&smart_sensor_process, event_unpaired, NULL);
            health_fail_count = 0; 
        }
    } 
    else{
        health_fail_count = 0; 
    }
}

/* ==================================================== */
/* =                  SETUP PROCESS                   = */
/* ==================================================== */ 
PROCESS_THREAD(setup_process, ev, data){
    static button_hal_button_t *btn;
    static bool smart_sensor_active = false;

    PROCESS_BEGIN();
    
    // Allocate core application events
    event_start_smart_sensor = process_alloc_event();

    LOG_INFO("Smart Sensor SETUP process starting...\n");
    
    // Generate a safe device name based on the node ID
    uint16_t safe_id = (node_id > 0) ? (node_id - 1) : 0;
    snprintf(device_name, sizeof(device_name), "sensor_%02u", safe_id);

    current_state = SENSOR_STATE_OFF;
    
    // Initialize hardware button
    button_hal_init();
    btn = button_hal_get_by_index(0);

    if(btn){
        LOG_WARN("Setup: button initialized: %s on pin %u\n", BUTTON_HAL_GET_DESCRIPTION(btn), btn->pin);
        LOG_WARN("Short press to start Smart Sensor. Hold 5s to reset when active.\n");
    }

    while(1){
        PROCESS_YIELD();

        // Handle primary button presses to start the device
        if(ev == button_hal_press_event){
            if(!smart_sensor_active){
                smart_sensor_active = true;
                process_start(&smart_sensor_process, NULL);
                process_start(&health_check_process, NULL);
                process_post(&smart_sensor_process, event_start_smart_sensor, NULL);
            } 
            else{
                LOG_WARN("Smart Sensor already active. Hold button 5 seconds to perform hard reset.\n");
            }
        }

        // Monitor button holds
        if(ev == button_hal_periodic_event && btn && data){
            button_hal_button_t *b = (button_hal_button_t *)data;
            if(b == btn){
                LOG_DBG("Setup: Button hold duration: %u s\n", b->press_duration_seconds);
            }
        }

        // Handles the graceful shutdown logic when smart_sensor_process yields PROCESS_EXIT()
        if(ev == PROCESS_EVENT_EXITED){
            struct process *exited = (struct process *)data;
            if(exited == &smart_sensor_process){
                LOG_WARN("Smart Sensor thread exited\n");
                smart_sensor_active = false;
                
                is_paired = false;
                target_batch_size = 5;
                samples_sent_in_batch = 0;
                health_fail_count = 0;
                simulated_printer_state = PRINTER_STATE_NORMAL;
                current_power_draw_watts = 150.0;

                set_sensor_state(SENSOR_STATE_OFF);
                LOG_INFO("Device reset to STATE: OFF - await short press to start again\n");
            }
        }
    }

    PROCESS_END();
}

/* ==================================================== */
/* =              HEALTH CHECK PROCESS                = */
/* ==================================================== */ 
PROCESS_THREAD(health_check_process, ev, data){
    static coap_message_t health_req[1];
    PROCESS_BEGIN();
    
    // Periodically ping the actuator to ensure it is still reachable
    etimer_set(&health_check_timer, 60 * CLOCK_SECOND);
    
    while(1){
        PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &health_check_timer);
        if(is_paired && current_state != SENSOR_STATE_OFF){
            coap_init_message(health_req, COAP_TYPE_CON, COAP_GET, coap_get_mid());
            coap_set_header_uri_path(health_req, "health");
            COAP_BLOCKING_REQUEST(&printer_ep, health_req, health_response_handler);
        }
        etimer_set(&health_check_timer, 60 * CLOCK_SECOND);
    }
    PROCESS_END();
}

/* ==================================================== */
/* =                 MAIN SENSOR PROCESS              = */
/* ==================================================== */ 
PROCESS_THREAD(smart_sensor_process, ev, data){
    PROCESS_BEGIN();

    LOG_INFO("Smart Sensor Booting up...\n");

    // Initialize Network Modules
    sensor_coap_init();
    sensor_mqtt_init(&smart_sensor_process, device_name);
    
    set_sensor_state(SENSOR_STATE_OFF);

    while(1){
        PROCESS_WAIT_EVENT();

        /* GLOBAL / CROSS-STATE EVENTS */
        // Handle initial pairing request
        if (ev == event_start_smart_sensor){
            set_sensor_state(SENSOR_STATE_INIT);
            LOG_INFO("Attempting initial pairing with Printer...\n");
            sensor_coap_send_discovery_async();
            etimer_set(&pairing_timer, 5 * CLOCK_SECOND);
        }

        // Handle successful pairing
        if(ev == event_discovery_received){
            is_paired = true;
            health_fail_count = 0;
            if(current_state == SENSOR_STATE_INIT){
                set_sensor_state(SENSOR_STATE_SLEEP);
            }
        }
        
        // Handle unpairing or disconnection
        if(ev == event_unpaired){
            LOG_INFO("Printer disconnected or unpair signal received! Restarting discovery.\n");
            is_paired = false;
            health_fail_count = 0;
            etimer_stop(&sampling_timer);
            set_sensor_state(SENSOR_STATE_INIT);
            
            sensor_coap_send_discovery_async();
            etimer_set(&pairing_timer, 5 * CLOCK_SECOND);
        }

        // Handle sampling control signals (START, PAUSE, STOP)
        if(ev == event_start_sampling){
            samples_sent_in_batch = 0;
            target_batch_size = 5; 
            set_sensor_state(SENSOR_STATE_ACTIVE);
            
            etimer_set(&sampling_timer, 1 * CLOCK_SECOND);
        }
        
        if(ev == event_pause_sampling || ev == event_stop_sampling){
            LOG_INFO("Sampling session ended. Returning to SLEEP.\n");
            etimer_stop(&sampling_timer);
            set_sensor_state(SENSOR_STATE_SLEEP);
        }

        // Ensure MQTT remains connected while the device is active
        if(ev == event_mqtt_retry || (ev == PROCESS_EVENT_TIMER && data == &mqtt_retry_timer)){
            if(!sensor_mqtt_is_connected() && current_state != SENSOR_STATE_OFF){
                sensor_mqtt_connect();
            }
            if(current_state != SENSOR_STATE_OFF){
                etimer_set(&mqtt_retry_timer, 10 * CLOCK_SECOND);
            }
        }

        // Handling Hard Reset Trigger
        if(ev == button_hal_periodic_event && data){
            button_hal_button_t *b = (button_hal_button_t *)data;
            if(b->press_duration_seconds >= 5 && current_state != SENSOR_STATE_OFF){
                LOG_WARN("Hard Reset Triggered. Breaking pair and returning to OFF.\n");
                sensor_coap_send_off_signal();
                
                etimer_stop(&sampling_timer);
                etimer_stop(&pairing_timer);
                
                sensor_mqtt_disconnect();
                
                process_exit(&health_check_process);
                PROCESS_EXIT();
            }
        }

        /* STATE MACHINE */
        if(current_state == SENSOR_STATE_INIT){
            // Handle pairing timeouts
            if(ev == PROCESS_EVENT_TIMER && data == &pairing_timer){
                LOG_INFO("Initial pairing timeout. Going to SLEEP.\n");
                is_paired = false;
                set_sensor_state(SENSOR_STATE_SLEEP);
                
                etimer_set(&pairing_timer, 10 * CLOCK_SECOND);
            }
        }
        else if(current_state == SENSOR_STATE_SLEEP){
            // Retry discovery periodically if not paired
            if(ev == PROCESS_EVENT_TIMER && data == &pairing_timer){
                if(!is_paired){
                    sensor_coap_send_discovery_async();
                    etimer_set(&pairing_timer, 10 * CLOCK_SECOND);
                }
            }
        }
        else if(current_state == SENSOR_STATE_ACTIVE){
            // Handle ML continuation signal
            if(ev == event_continue_sampling){
                LOG_INFO("ML Verdict received: CONTINUE. Resuming sampling.\n");
                samples_sent_in_batch = 0;
                target_batch_size = 4; 
                
                etimer_set(&sampling_timer, 1 * CLOCK_SECOND);
            }

            // Process active sampling and MQTT publishing
            if(ev == PROCESS_EVENT_TIMER && data == &sampling_timer){
                accel_data_t plate = read_plate_acceleration();
                accel_data_t extruder = read_extruder_acceleration();
                float tension = read_tension();
                float power = read_power(); 

                // Extract integer and decimal components manually for formatted output
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

                if(sensor_mqtt_is_connected()){
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

                    char my_ip_str[UIPLIB_IPV6_MAX_STR_LEN] = "unknown_ip";
                    uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
                    if(addr != NULL) uiplib_ipaddr_snprint(my_ip_str, sizeof(my_ip_str), &addr->ipaddr);
                    
                    snprintf(pub_topic, sizeof(pub_topic), "%s/print/measurements", my_ip_str);
                    sensor_mqtt_publish(pub_topic, mqtt_payload);
                    LOG_INFO("Sample taken and published to %s\n", pub_topic);
                } 
                else{
                    LOG_WARN("MQTT disconnected. Attempting to reconnect...\n");
                    sensor_trigger_mqtt_retry(); 
                }

                samples_sent_in_batch++;
                if(samples_sent_in_batch >= target_batch_size){
                    LOG_INFO("Batch complete. Awaiting ML verdict... (Fallback timeout active)\n");
                    etimer_set(&sampling_timer, 10 * CLOCK_SECOND);
                } 
                else{
                    etimer_reset(&sampling_timer);
                }
            }
        }
    }

    PROCESS_END();
}