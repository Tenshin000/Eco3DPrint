#include "sensors.h"
#include "contiki.h"
#include "lib/random.h"
#include "sys/log.h"
#include <math.h>  
#include <stdlib.h>

// Logging configuration
#define LOG_MODULE "Dev:Sensors"
#define LOG_LEVEL LOG_LEVEL_APP

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Printer states for the Markov Chain
typedef enum{
    PRINTER_STATE_NORMAL,
    PRINTER_STATE_ERROR
} printer_state_t;

// State variables
static printer_state_t current_state = PRINTER_STATE_NORMAL;
static float current_power_draw_amps = 2.75; 

// Sophisticated Gaussian Noise Generator (Box-Muller Transform)
static float get_gaussian_float(float mean, float std_dev){
    float u1 = (float)random_rand() / RAND_MAX;
    float u2 = (float)random_rand() / RAND_MAX;
    
    if (u1 <= 0.0001f) u1 = 0.0001f;
    
    float z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return mean + z0 * std_dev;
}

// Uniform probability generator (returns 0.0 to 1.0)
static float get_uniform_probability(void) {
    return (float)random_rand() / (float)RAND_MAX;
}

/* Public Functions */
void sensors_init(void){
    LOG_INFO("Initializing 3D Printer Sensors ...\\n");
    random_init(clock_time());
}

/* 
 * Markov Chain State Update 
 * Call this function once at the beginning of every sampling cycle
*/
void sensor_activate(void){
    float roll = get_uniform_probability();

    if(current_state == PRINTER_STATE_NORMAL){
        // 5% chance to transition into an ERROR state
        if(roll < 0.05f){ 
            current_state = PRINTER_STATE_ERROR;
            LOG_WARN("SIMULATION: Natural transition to ERROR state!\\n");
        }
    } 
    else{
        // 10% chance to recover back to NORMAL state
        // This keeps error sequences shorter and normal sequences longer
        if(roll < 0.10f){ 
            current_state = PRINTER_STATE_NORMAL;
            LOG_INFO("SIMULATION: Natural recovery to NORMAL state.\\n");
        }
    }
}

void sensor_sleep(void){
    // No specific sleep logic needed for this simulation
}

void sensor_deactivate(void){
    // No specific deactivation logic needed for this simulation
}

// Printing Plate Accelerometer
accel_data_t read_plate_acceleration(void){
    accel_data_t data;
    if (current_state == PRINTER_STATE_NORMAL){
        data.x = get_gaussian_float(-0.45, 0.11);
        data.y = get_gaussian_float(-0.14, 0.01);
        data.z = get_gaussian_float(9.50, 0.16); 
    } 
    else{
        // Massive vibration spike during error
        data.x = get_gaussian_float(-0.2283, 0.2134); 
        data.y = get_gaussian_float(-0.2355, 0.2255); 
        data.z = get_gaussian_float(9.81, 0.4195);  
    }
    return data;
}

// Extrusion Head Accelerometer
accel_data_t read_extruder_acceleration(void){
    accel_data_t data;
    if(current_state == PRINTER_STATE_NORMAL){
        data.x = get_gaussian_float(0.27, 0.03);
        data.y = get_gaussian_float(0.32, 0.04);
        data.z = get_gaussian_float(-9.50, 0.16);
    } 
    else{
        // Massive vibration spike during error
        data.x = get_gaussian_float(0.2728, 1.2407); 
        data.y = get_gaussian_float(0.3536, 1.2928); 
        data.z = get_gaussian_float(-9.81, 0.6472);
    }
    return data;
}

// Voltmeter 
float read_tension(void){
    float tension;
    if(current_state == PRINTER_STATE_NORMAL){
        tension = get_gaussian_float(285.0, 38.0);
    } 
    else{
        tension = get_gaussian_float(315.1302, 50.3328); 
    }
    
    return tension;
}

// Amperometer
float read_current(void){
    float delta;
    if(current_state == PRINTER_STATE_NORMAL){
        delta = get_gaussian_float(0.0, 0.15); 
    } 
    else{
        // Erratic power spikes during failure
        delta = get_gaussian_float(0.0, 0.60); 
    }
    
    current_power_draw_amps += delta;
    
    // Bounds check
    if(current_power_draw_amps < 1.0) 
        current_power_draw_amps = 1.0;
    if(current_power_draw_amps > 4.5) 
        current_power_draw_amps = 4.5;
    
    return current_power_draw_amps;
}

// Phase Shifter
float read_phase_shift(void){
    if(current_state == PRINTER_STATE_NORMAL)
        return get_gaussian_float(0.43, 0.04);
    else
        return get_gaussian_float(0.50, 0.15);
    return get_gaussian_float(0.43, 0.04);
}
