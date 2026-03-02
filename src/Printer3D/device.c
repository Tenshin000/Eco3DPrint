#include "contiki.h"
#include "sys/etimer.h"
#include "dev/button-hal.h"
#include "sys/log.h"
#include <string.h>

/* Custom headers */
#include "sensors.h"
#include "scaler_params.h" 
#include "print_prediction.h"
#include "network_coap.h"

// Logging
#define LOG_MODULE "Dev:Main"
#define LOG_LEVEL LOG_LEVEL_APP

/* State Machine Definition */
typedef enum{
    STATE_OFF,
    STATE_INITIALIZATION,
    STATE_ONLINE,
    STATE_OFFLINE,
    STATE_PRINTING
} device_state_t;

/* Global Variables */
static device_state_t current_state = STATE_OFF;
static device_state_t previous_state = STATE_OFF; 

static struct etimer print_timer;
static uint8_t sample_index = 0;
static float ml_window[NUM_FEATURES]; 
static uint8_t consecutive_errors = 0;

static int current_job_time = 0; 
static bool init_requested = false; 

PROCESS(smart_printer_process, "Smart Printer Node Process");
AUTOSTART_PROCESSES(&smart_printer_process);

/* Utility Functions */
static void reset_system_state(void){
    current_state = STATE_OFF;
    sample_index = 0;
    consecutive_errors = 0;
    init_requested = false;
    reset_coap_state(); 
    etimer_stop(&print_timer);
    LOG_INFO("System reset to OFF state.\n");
}

static void button_handler(button_hal_button_t *btn){
    if(btn->state == BUTTON_HAL_STATE_PRESSED){
        if(current_state == STATE_OFF){
            LOG_INFO("Button pressed. Booting up...\n");
            current_state = STATE_INITIALIZATION;
            init_requested = false; 
            process_poll(&smart_printer_process); 
        }
    }
    
    // Check for 3-second long press to trigger emergency OFF
    if(btn->press_duration_seconds >= 3 && current_state != STATE_OFF){
        LOG_WARN("Button held for 3 seconds! Hard Reset!\n");
        reset_system_state();
        current_state = STATE_OFF;
        LOG_INFO("STATE: OFF\n");
        process_poll(&smart_printer_process);
    }
}

/* Main Process Loop */
PROCESS_THREAD(smart_printer_process, ev, data){
    PROCESS_BEGIN();
    LOG_INFO("Device Firmware Booted. Waiting in OFF state.\n");

    // Hook the button hal logic
    button_hal_init();

    while(1){
        PROCESS_WAIT_EVENT();

        // 1. Button events routing
        if(ev == button_hal_press_event || ev == button_hal_periodic_event){
            button_handler((button_hal_button_t *)data);
        }

        // 2. State Machine Logic
        switch(current_state){
            case STATE_OFF:
                // Device is idle, wait for button press
                break;

            case STATE_INITIALIZATION:
                // Send CoAP request only once
                if(!init_requested){
                    LOG_INFO("STATE: INITIALIZATION\n");
                    sensors_init();
                    coap_network_init(); 
                    
                    // Non-blocking CoAP request
                    coap_network_register(); 
                    init_requested = true;
                }

                // Check if the callback in network_coap.c validated the registration
                if(is_coap_registered()){
                    current_state = STATE_ONLINE;
                    LOG_INFO("Cloud connection successful.\n");
                }
                break;

            case STATE_ONLINE:
                LOG_INFO("STATE: ONLINE\n");
                // Wait for an STL job via CoAP
                if(has_new_stl_job()){
                    current_job_time = get_print_time();
                    clear_stl_job(); 
                    
                    LOG_INFO("STL received! Print time: %d sec. Starting...\n", current_job_time);
                    previous_state = STATE_ONLINE;
                    current_state = STATE_PRINTING;
                    
                    // Reset ML variables for the new print
                    sample_index = 0;
                    consecutive_errors = 0;
                    etimer_set(&print_timer, CLOCK_SECOND * 1);
                    
                    process_poll(&smart_printer_process);
                }
                break;

            case STATE_OFFLINE:
                // Printer works manually, cloud logic is disabled
                break;

            case STATE_PRINTING:
                if(ev == PROCESS_EVENT_TIMER && data == &print_timer){
                    
                    if(current_job_time <= 0){
                        LOG_INFO("Print job completed! Back to ONLINE.\n");
                        current_state = previous_state;
                        etimer_stop(&print_timer);
                        break;
                    }
                    current_job_time--;

                    // 1. Read sensors 
                    accel_data_t plate = read_plate_acceleration();
                    accel_data_t ext = read_extruder_acceleration();
                    float power = get_active_power(read_tension(), read_current(), read_phase_shift());

                    // Populate the window array 
                    uint8_t offset = sample_index * 7;
                    ml_window[offset + 0] = plate.x;
                    ml_window[offset + 1] = plate.y;
                    ml_window[offset + 2] = plate.z;
                    ml_window[offset + 3] = ext.x;
                    ml_window[offset + 4] = ext.y;
                    ml_window[offset + 5] = ext.z;
                    ml_window[offset + 6] = power;

                    LOG_INFO("Sampling sec %d... Time left: %d\n", sample_index + 1, current_job_time);

                    sample_index++;

                    // If we reached 5 samples (5 seconds), run ML
                    if(sample_index == 5){
                        LOG_INFO("5-second window full. Running ML Prediction!\n");
                        
                        // Mock prediction
                        int prediction = 0; 

                        if(prediction == 1){ 
                            consecutive_errors++;
                            if(consecutive_errors == 1){
                                LOG_WARN("ML Error Predicted (1st time) -> Status: MONITORING\n");
                            } else{
                                if(consecutive_errors == 2){
                                    LOG_ERR("ML Error Predicted (2nd time) -> Status: ERROR! Halting.\n");
                                    current_state = previous_state; 
                                    etimer_stop(&print_timer);
                                    process_poll(&smart_printer_process);
                                    break; 
                                }
                            }
                        } else{
                            consecutive_errors = 0; 
                        }

                        // Implement 1-second overlap
                        memcpy(&ml_window[0], &ml_window[4 * 7], 7 * sizeof(float));
                        sample_index = 1; 
                    }

                    // Reset timer for the next second
                    etimer_reset(&print_timer);
                }
                break;
        }
    }

    PROCESS_END();
}