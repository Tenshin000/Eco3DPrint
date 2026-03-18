#include "sensors.h"

#include "contiki.h"
#include "lib/random.h"
#include "sys/log.h"
#include <math.h>  
#include <stdlib.h>

/* Macros and Definitions */
#define LOG_MODULE "Sensors"
#define LOG_LEVEL LOG_LEVEL_APP

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Printer States */
typedef enum{
    PRINTER_STATE_NORMAL,
    PRINTER_STATE_ERROR
} printer_state_t;

/* State Variables */
static printer_state_t current_state = PRINTER_STATE_NORMAL;
static float current_power_draw_watts = 150.0; 

/* Utility Functions */
static float get_gaussian_float(float mean, float std_dev){
    float u1 = (float)random_rand() / RANDOM_RAND_MAX;
    float u2 = (float)random_rand() / RANDOM_RAND_MAX;
    float z0;
    float final_value;
    
    // Ensure no zero log
    if(u1 <= 0.0001f)
        u1 = 0.0001f;
    else
        u1 = u1;
    
    z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    final_value = mean + z0 * std_dev;
    
    return final_value;
}

float get_bounded_gaussian(float mean, float std_dev, float limit_val){
    float generated_val = get_gaussian_float(mean, std_dev);
    float max_val = mean + limit_val;
    float min_val = mean - limit_val;
    
    if(generated_val > max_val)
        generated_val = max_val;
    else{
        if(generated_val < min_val)
            generated_val = min_val;
        else
            generated_val = generated_val;
    }
    
    return generated_val;
}

static float get_uniform_probability(void){
    float prob_value;
    // Generate uniform probability
    prob_value = (float)random_rand() / (float)RANDOM_RAND_MAX;
    return prob_value;
}

/* Public Functions */
void sensors_init(void){
    LOG_INFO("Initializing 3D Printer Sensors ...\n");
    random_init((unsigned short)clock_time());
}

void sensor_activate(void){
    float roll = get_uniform_probability();

    LOG_INFO("Sensors Activated\n");

    if(current_state == PRINTER_STATE_NORMAL){
        // Enter error state occasionally
        if(roll < 0.005f){ 
            current_state = PRINTER_STATE_ERROR;
            LOG_DBG("SIMULATION: Natural transition to ERROR state!\n");
        } 
        else
            current_state = PRINTER_STATE_NORMAL;
    } 
    else{
        // Recover to normal state
        if(roll < 0.20f){ 
            current_state = PRINTER_STATE_NORMAL;
            LOG_DBG("SIMULATION: Natural recovery to NORMAL state.\n");
        } 
        else
            current_state = PRINTER_STATE_ERROR;
    }
}

void sensor_sleep(void){
    LOG_INFO("Sensors in Sleep Mode\n");
}

void sensor_deactivate(void){
    LOG_INFO("Sensors Deactivated\n");
}

/* Sensor Reading Functions */
accel_data_t read_plate_acceleration(void){
    accel_data_t data_value;
    
    if(current_state == PRINTER_STATE_NORMAL){
        // Normal printing plate readings
        data_value.x = get_bounded_gaussian(-0.35, 0.20, 0.65);
        data_value.y = get_bounded_gaussian(-0.20, 0.15, 0.50);
        data_value.z = get_bounded_gaussian(9.67, 0.05, 0.20);
    } 
    else{
        // Plate stuttering and sinking
        data_value.x = get_gaussian_float(-0.23, 0.24); 
        data_value.y = get_gaussian_float(-0.25, 0.26); 
        data_value.z = get_gaussian_float(9.81, 0.32);  
    }
    
    return data_value;
}

accel_data_t read_extruder_acceleration(void){
    accel_data_t data_value;
    
    if(current_state == PRINTER_STATE_NORMAL){
        // Smooth extruder movements
        data_value.x = get_bounded_gaussian(0.25, 0.30, 1.10);
        data_value.y = get_bounded_gaussian(0.32, 0.40, 1.40);
        data_value.z = get_bounded_gaussian(-9.78, 0.18, 0.75);
    } 
    else{
        // Massive vibration spike during mechanical failure
        data_value.x = get_gaussian_float(0.27, 1.40); 
        data_value.y = get_gaussian_float(0.35, 1.45); 
        data_value.z = get_gaussian_float(-9.81, 0.75);
    }
    
    return data_value;
}

// Voltmeter
float read_tension(void){
    float tension_value;
    if(current_state == PRINTER_STATE_NORMAL){
        // Stable tension supply
        tension_value = get_gaussian_float(331.0, 15.0);
    } 
    else{
        // Voltage drop during anomaly
        tension_value = get_gaussian_float(312.0, 50.0); 
    }
    return tension_value;
}

// Wattmeter (DC Power)
float read_power(void){
    float delta;
    if(current_state == PRINTER_STATE_NORMAL){
        // Normal power fluctuations
        delta = get_gaussian_float(0.0, 2.5); 
    } 
    else{
        // Erratic power spikes/drops during failure
        delta = get_gaussian_float(0.0, 15.0); 
    }
    
    current_power_draw_watts += delta;
    
    // Bounds check to keep it between 120W and 180W
    if(current_power_draw_watts < 120.0) 
        current_power_draw_watts = 120.0;
    if(current_power_draw_watts > 180.0) 
        current_power_draw_watts = 180.0;
    
    return current_power_draw_watts;
}