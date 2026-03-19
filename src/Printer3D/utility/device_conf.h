#ifndef DEVICE_CONF_H
#define DEVICE_CONF_H

#include <stdint.h>
#include <stdbool.h>

/* Device state machine definition */
typedef enum {
  STATE_OFF,
  STATE_INITIALIZATION,
  STATE_ONLINE,
  STATE_OFFLINE,
  STATE_PRINTING
} device_state_t;

/* Hooks implemented in device.c */
device_state_t device_get_state(void);
const char* device_get_state_string(device_state_t state);
void device_set_state(device_state_t new_state);
void device_trigger_mqtt_retry(void);
void device_add_stl_length(int len);
void device_trigger_print_simulation(void);
void device_reset_waiting_confirmation(void);

#endif /* DEVICE_CONF_H */