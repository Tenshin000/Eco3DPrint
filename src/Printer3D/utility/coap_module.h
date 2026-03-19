#ifndef COAP_MODULE_H
#define COAP_MODULE_H

#include "contiki.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"

// CoAP Configuration Macros
#define CLOUD_SERVER_EP "coap://[fd00::1]:5683"
#define REG_URI_PATH "/registration"
#define END_PRINT_URI_PATH "/print/finished"
#define OFF_SIGNAL_URI_PATH "/signal/off"

// Exposed CoAP variables for BLOCKING_REQUEST usage in device.c
extern coap_endpoint_t server_ep;
extern coap_message_t request[1];

/* Initialize the CoAP module and activate internal resources */
void coap_module_init(void);

/* Prepare a CoAP request. Returns the generated Message ID (MID) */
uint16_t coap_module_prepare_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path);

/* Handlers exposed for BLOCKING_REQUEST usage */
void registration_handler(coap_message_t* response);
void print_finished_handler(coap_message_t* response);

#endif /* COAP_MODULE_H */