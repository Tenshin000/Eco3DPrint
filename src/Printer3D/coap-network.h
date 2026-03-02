#ifndef NETWORK_COAP_H_
#define NETWORK_COAP_H_

#include <stdbool.h>

// Initializes the CoAP endpoints and resources
void coap_network_init(void);

// Sends a non-blocking registration request to the Cloud App
void coap_network_register(void);

// Checks if the device has successfully registered
bool is_coap_registered(void);

// Checks if a new STL job has been received via CoAP Server
bool has_new_stl_job(void);

// Retrieves the estimated print time from the parsed JSON
int get_print_time(void);

// Clears the job flag to allow receiving future STL files
void clear_stl_job(void);

// Resets the internal CoAP state (useful for hard resets)
void reset_coap_state(void);

#endif /* NETWORK_COAP_H_ */