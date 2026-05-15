#include "coap_module.h"

#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"
#include "sys/log.h"
#include "sys/node-id.h"
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE "CoAP Module"
#define LOG_LEVEL LOG_LEVEL_APP

coap_endpoint_t printer_ep;
coap_message_t request[1];
bool is_paired = false;

static char coap_payload[128];

process_event_t event_discovery_received;
process_event_t event_start_sampling;
process_event_t event_stop_sampling;
process_event_t event_pause_sampling;
process_event_t event_continue_sampling;
process_event_t event_unpaired;

extern uint8_t get_sensor_state(void);
#define SENSOR_STATE_OFF 0
#define SENSOR_STATE_INIT 1

/* ==================================================== */
/* =              RESOURCE HANDLERS                   = */
/* ==================================================== */
static void res_discovery_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    if(get_sensor_state() == SENSOR_STATE_OFF || is_paired) {
        coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
        return;
    }

    const uint8_t *payload = NULL;
    int len = coap_get_payload(req, &payload);
    
    if(len > 0) {
        LOG_INFO("Discovery ping received! Linking to printer...\n");
        coap_endpoint_copy(&printer_ep, req->src_ep);
        
        strncpy(coap_payload, "unknown_ip", sizeof(coap_payload));
        uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
        if(addr != NULL) {
            uiplib_ipaddr_snprint(coap_payload, sizeof(coap_payload), &addr->ipaddr);
        }

        coap_set_status_code(response, CONTENT_2_05);
        coap_set_payload(response, (uint8_t *)coap_payload, strlen(coap_payload));
        
        process_post(PROCESS_BROADCAST, event_discovery_received, NULL);
    } else {
        coap_set_status_code(response, BAD_REQUEST_4_00);
    }
}

static void res_print_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    uint8_t current_state = get_sensor_state();
    
    if(current_state == SENSOR_STATE_OFF || current_state == SENSOR_STATE_INIT) {
        LOG_WARN("Print command rejected: Sensor not ready.\n");
        coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
        return;
    }

    const uint8_t *payload = NULL;
    int len = coap_get_payload(req, &payload);
    
    if(len > 0) {
        char msg[16];
        int cp_len = len < 15 ? len : 15;
        memcpy(msg, payload, cp_len);
        msg[cp_len] = '\0';
        
        if(strncmp(msg, "STOP", 4) == 0) {
            LOG_INFO("Stop signal received from printer.\n");
            process_post(PROCESS_BROADCAST, event_stop_sampling, NULL);
        } else if(strncmp(msg, "PAUSE", 5) == 0) {
            LOG_INFO("Pause (Anomaly) signal received from printer.\n");
            process_post(PROCESS_BROADCAST, event_pause_sampling, NULL);
        } else if(strncmp(msg, "CONT", 4) == 0) {
            LOG_INFO("ML Verdict received: CONTINUE. Resuming sampling.\n");
            process_post(PROCESS_BROADCAST, event_continue_sampling, NULL);
        } else if(strncmp(msg, "START", 5) == 0) {
            LOG_INFO("Start signal received. Sampling begins indefinitely until told otherwise.\n");
            process_post(PROCESS_BROADCAST, event_start_sampling, NULL);
        }
    }
    coap_set_status_code(response, CHANGED_2_04);
}

static void res_unpair_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    LOG_WARN("Unpair signal received from printer!\n");
    coap_set_status_code(response, CHANGED_2_04);
    process_post(PROCESS_BROADCAST, event_unpaired, NULL);
}

/* ==================================================== */
/* =                    RESOURCES                     = */
/* ==================================================== */ 
RESOURCE(res_discovery, "title=\"Discovery Ping\";rt=\"Text\"", NULL, res_discovery_post_handler, NULL, NULL);
RESOURCE(res_print, "title=\"Print Control\";rt=\"Control\"", NULL, res_print_post_handler, NULL, NULL);
RESOURCE(res_unpair, "title=\"Unpair\";rt=\"Control\"", NULL, res_unpair_post_handler, NULL, NULL);

void sensor_coap_init(void) {
    event_discovery_received = process_alloc_event();
    event_start_sampling = process_alloc_event();
    event_stop_sampling = process_alloc_event();
    event_pause_sampling = process_alloc_event();
    event_continue_sampling = process_alloc_event();
    event_unpaired = process_alloc_event();

    coap_activate_resource(&res_discovery, "sensor/discovery");
    coap_activate_resource(&res_print, "sensor/print");
    coap_activate_resource(&res_unpair, "sensor/unpair");
}

void sensor_coap_prepare_discovery(void) {
    #ifdef DEV_COOJA
    uint16_t printer_id = (node_id > 0) ? (node_id - 1) : 0;
    uip_ipaddr_t pip;
    uip_ip6addr(&pip, 0xfd00, 0, 0, 0, 0x0200 + printer_id, printer_id, printer_id, printer_id);
    
    memset(&printer_ep, 0, sizeof(printer_ep));
    uip_ipaddr_copy(&printer_ep.ipaddr, &pip);
    printer_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
    printer_ep.secure = 0;
    
    coap_init_message(request, COAP_TYPE_CON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(request, "printer/discovery");
    #else
    coap_endpoint_parse("coap://[ff02::1]", 16, &printer_ep); 
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(request, "printer/discovery");
    #endif
    
    strncpy(coap_payload, "unknown_ip", sizeof(coap_payload));
    uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
    if(addr != NULL) uiplib_ipaddr_snprint(coap_payload, sizeof(coap_payload), &addr->ipaddr);
    coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));
}

void discovery_response_handler(coap_message_t* response) {
    if(response && response->code == CONTENT_2_05) {
        process_post(PROCESS_BROADCAST, event_discovery_received, NULL);
    }
}

void sensor_coap_send_off_signal(void) {
    if(!is_paired) return;
    
    static coap_message_t msg[1];
    coap_init_message(msg, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(msg, "printer/sensor_off");
    coap_set_payload(msg, (uint8_t *)"OFF", 3);
    
    LOG_INFO("Sending OFF notification to Printer...\n");
    
    coap_transaction_t* transaction = coap_new_transaction(msg->mid, &printer_ep);
    if(transaction) {
        transaction->message_len = coap_serialize_message(msg, transaction->message);
        coap_send_transaction(transaction);
    }
}