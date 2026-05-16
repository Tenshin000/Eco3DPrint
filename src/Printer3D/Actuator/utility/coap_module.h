#ifndef COAP_MODULE_H
#define COAP_MODULE_H

#include "contiki.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include <stdbool.h>
#include "net/ipv6/uiplib.h"

// CoAP Configuration Macros
#define CLOUD_SERVER_EP "coap://[fd00::1]:5683"
#define REG_URI_PATH "/registration"
#define END_PRINT_URI_PATH "/print/finished"
#define OFF_SIGNAL_URI_PATH "/signal/off"

// Exposed CoAP variables for BLOCKING_REQUEST usage in device.c
extern coap_endpoint_t server_ep;
extern coap_endpoint_t sensor_ep;
extern coap_endpoint_t multicast_ep;
extern coap_message_t request[1];

extern char paired_sensor_ip_str[UIPLIB_IPV6_MAX_STR_LEN];
extern bool sensor_is_paired;

// Custom events exported to the main process
extern process_event_t event_sensor_paired;
extern process_event_t event_sensor_unpaired;

/* Initialize the CoAP module and activate internal resources */
void coap_module_init(void);

/* Prepare a CoAP request for the Cloud Server */
uint16_t coap_module_prepare_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path);

/* Prepare a CoAP command for the paired Sensor */
uint16_t coap_module_prepare_sensor_command(const char* command);

/* Non-Blocking Send for ML verdicts to prevent MQTT blocking */
void coap_module_send_sensor_command_non_blocking(const char* command);

/* Prepare an Unpair signal for the Sensor */
uint16_t coap_module_prepare_unpair(void);

/* Sensor Pairing Functions */
void coap_module_prepare_discovery(void);

/* FIX: Exposed Async Discovery Function to avoid blocking delays */
void coap_module_send_discovery_async(void);

/* Handlers exposed for BLOCKING_REQUEST usage */
void registration_handler(coap_message_t* response);
void print_finished_handler(coap_message_t* response);
void sensor_command_handler(coap_message_t* response);

#endif /* COAP_MODULE_H */