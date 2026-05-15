#ifndef COAP_MODULE_H
#define COAP_MODULE_H

#include "contiki.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include <stdbool.h>

extern coap_endpoint_t printer_ep;
extern coap_message_t request[1];
extern bool is_paired;

extern process_event_t event_discovery_received;
extern process_event_t event_start_sampling;
extern process_event_t event_stop_sampling;
extern process_event_t event_pause_sampling;
extern process_event_t event_continue_sampling; 
extern process_event_t event_unpaired;

void sensor_coap_init(void);
void sensor_coap_send_off_signal(void);
void sensor_coap_prepare_discovery(void);
void discovery_response_handler(coap_message_t* response);

#endif /* COAP_MODULE_H */