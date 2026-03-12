#ifndef SMART_PRINTER_SENSORS_H_
#define SMART_PRINTER_SENSORS_H_

#include <stdint.h>
#include <stdbool.h>

// Structure to hold 3-axis accelerometer data
typedef struct{
    float x;
    float y;
    float z;
} accel_data_t;

void sensors_init(void);
void sensor_activate(void);
void sensor_sleep(void);
void sensor_deactivate(void);

accel_data_t read_plate_acceleration(void);
accel_data_t read_extruder_acceleration(void);

float read_tension(void);       // Returns Voltage (V)
float read_current(void);       // Returns Current (A) 
float read_phase_shift(void);   // Returns phase shift angle (radians) 

#endif /* SMART_PRINTER_SENSORS_H_ */