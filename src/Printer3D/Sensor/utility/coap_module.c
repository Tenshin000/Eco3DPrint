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

static char coap_payload[256];

process_event_t event_discovery_received;
process_event_t event_start_sampling;
process_event_t event_stop_sampling;
process_event_t event_pause_sampling;
process_event_t event_continue_sampling;
process_event_t event_unpaired;

extern uint8_t get_sensor_state(void);
#define SENSOR_STATE_OFF 0
#define SENSOR_STATE_INIT 1

#ifdef DEV_DONGLE
static const char* known_dongle_ips[3] = {
    "fd00::f6ce:366a:718b:73f2",
    "fd00::f6ce:36fa:435f:f3d6",
    "fd00::f6ce:36cf:5367:3d5a"
};
static uint8_t current_dongle_idx = 0;
#endif

// FIX: Dedicated function to send a direct reciprocal ping without rotating the IP list
static void sensor_coap_send_direct_ping(coap_endpoint_t *target_ep) {
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(request, "printer/discovery");
    
    strncpy(coap_payload, "unknown_ip", sizeof(coap_payload));
    uip_ds6_addr_t *my_addr = uip_ds6_get_global(ADDR_PREFERRED);
    if(my_addr != NULL) uiplib_ipaddr_snprint(coap_payload, sizeof(coap_payload), &my_addr->ipaddr);
    coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));

    coap_transaction_t *t = coap_new_transaction(request->mid, target_ep);
    if(t) {
        t->message_len = coap_serialize_message(request, t->message);
        coap_send_transaction(t);
    }
}

/* ==================================================== */
/* =              RESOURCE HANDLERS                   = */
/* ==================================================== */
static void res_discovery_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    if(get_sensor_state() == SENSOR_STATE_OFF) {
        coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
        return;
    }

    const uint8_t *payload = NULL;
    int len = coap_get_payload(req, &payload);

    if(len > 0 && payload != NULL) {
        char new_ip[UIPLIB_IPV6_MAX_STR_LEN];
        memset(new_ip, 0, sizeof(new_ip));
        int cp_len = len < sizeof(new_ip) - 1 ? len : sizeof(new_ip) - 1;
        memcpy(new_ip, payload, cp_len);
        new_ip[cp_len] = '\0';

        if(strncmp(new_ip, "unknown_ip", 10) == 0) {
            #ifdef DEV_COOJA
            uint16_t printer_id = node_id - 1;
            if (printer_id == 0) printer_id = 1;
            uip_ipaddr_t printer_ip;
            uip_ip6addr(&printer_ip, 0xfd00, 0, 0, 0, 0x0200 + printer_id, printer_id, printer_id, printer_id);
            uiplib_ipaddr_snprint(new_ip, sizeof(new_ip), &printer_ip);
            #elif defined(DEV_DONGLE)
            uiplib_ipaddr_snprint(new_ip, sizeof(new_ip), &req->src_ep->ipaddr);
            #endif
        }

        coap_set_status_code(response, CONTENT_2_05);

        if(is_paired) {
             return;
        }

        coap_endpoint_copy(&printer_ep, req->src_ep);
        printer_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
        
        LOG_INFO("Discovery ping received! Linking to printer...\n");
        is_paired = true;
        process_post(PROCESS_BROADCAST, event_discovery_received, NULL);
        
        // FIX: Reply directly to the actuator bypassing the rotation logic
        sensor_coap_send_direct_ping(&printer_ep);

    } else {
        coap_set_status_code(response, BAD_REQUEST_4_00);
    }
}

static void res_print_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
    if(get_sensor_state() == SENSOR_STATE_OFF || !is_paired) {
        coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
        LOG_WARN("Print command rejected: Sensor not ready.\n");
        return;
    }

    const uint8_t *payload = NULL;
    int len = coap_get_payload(req, &payload);

    if(len > 0 && payload != NULL) {
        char command[16];
        memset(command, 0, sizeof(command));
        int cp_len = len < sizeof(command) - 1 ? len : sizeof(command) - 1;
        memcpy(command, payload, cp_len);
        command[cp_len] = '\0';

        if(strcmp(command, "START") == 0) {
            LOG_INFO("Start signal received. Sampling begins indefinitely until told otherwise.\n");
            process_post(PROCESS_BROADCAST, event_start_sampling, NULL);
            coap_set_status_code(response, CHANGED_2_04);
        } else if(strcmp(command, "STOP") == 0) {
            LOG_INFO("Stop signal received from printer.\n");
            process_post(PROCESS_BROADCAST, event_stop_sampling, NULL);
            coap_set_status_code(response, CHANGED_2_04);
        } else if(strcmp(command, "PAUSE") == 0) {
            LOG_INFO("Pause signal received.\n");
            process_post(PROCESS_BROADCAST, event_pause_sampling, NULL);
            coap_set_status_code(response, CHANGED_2_04);
        } else if(strcmp(command, "CONT") == 0) {
            process_post(PROCESS_BROADCAST, event_continue_sampling, NULL);
            coap_set_status_code(response, CHANGED_2_04);
        } else {
            LOG_WARN("Unknown print command received: %s\n", command);
            coap_set_status_code(response, BAD_REQUEST_4_00);
        }
    } else {
        coap_set_status_code(response, BAD_REQUEST_4_00);
    }
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
RESOURCE(res_print_control, "title=\"Print Control\";rt=\"Control\"", NULL, res_print_post_handler, NULL, NULL);
RESOURCE(res_unpair_control, "title=\"Unpair Sensor\";rt=\"Control\"", NULL, res_unpair_post_handler, NULL, NULL);

/* ==================================================== */
/* =                MODULE FUNCTIONS                  = */
/* ==================================================== */
void sensor_coap_init(void) {
    static bool initialized = false;
    if(!initialized) {
        event_discovery_received = process_alloc_event();
        event_start_sampling = process_alloc_event();
        event_stop_sampling = process_alloc_event();
        event_pause_sampling = process_alloc_event();
        event_continue_sampling = process_alloc_event();
        event_unpaired = process_alloc_event();
        
        coap_activate_resource(&res_discovery, "sensor/discovery");
        coap_activate_resource(&res_print_control, "sensor/print");
        coap_activate_resource(&res_unpair_control, "sensor/unpair");
        initialized = true;
    }
}

void sensor_coap_prepare_discovery(void) {
    #ifdef DEV_COOJA
    uint16_t printer_id = node_id - 1;
    if (printer_id == 0) printer_id = 1; 
    uip_ipaddr_t printer_ip;
    uip_ip6addr(&printer_ip, 0xfd00, 0, 0, 0, 0x0200 + printer_id, printer_id, printer_id, printer_id);
    
    memset(&printer_ep, 0, sizeof(printer_ep));
    uip_ipaddr_copy(&printer_ep.ipaddr, &printer_ip);
    printer_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
    printer_ep.secure = 0;
    
    #elif defined(DEV_DONGLE)
    char my_ip_str[UIPLIB_IPV6_MAX_STR_LEN] = "unknown_ip";
    uip_ds6_addr_t *addr = uip_ds6_get_global(ADDR_PREFERRED);
    if(addr != NULL) uiplib_ipaddr_snprint(my_ip_str, sizeof(my_ip_str), &addr->ipaddr);

    bool valid_target_found = false;
    uint8_t attempts = 0;
    uip_ipaddr_t target_ip;

    while(!valid_target_found && attempts < 3) {
        const char* candidate_ip = known_dongle_ips[current_dongle_idx];
        current_dongle_idx = (current_dongle_idx + 1) % 3; 
        attempts++;

        uip_ipaddr_t cand_ip_struct;
        uiplib_ipaddrconv(candidate_ip, &cand_ip_struct);

        if(addr == NULL || !uip_ipaddr_cmp(&addr->ipaddr, &cand_ip_struct)) {
            uip_ipaddr_copy(&target_ip, &cand_ip_struct);
            valid_target_found = true;
        }
    }

    memset(&printer_ep, 0, sizeof(printer_ep));
    uip_ipaddr_copy(&printer_ep.ipaddr, &target_ip);
    printer_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
    printer_ep.secure = 0;
    
    #else
    coap_endpoint_parse("coap://[ff02::1]", 16, &printer_ep); 
    #endif
    
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(request, "printer/discovery");
    
    strncpy(coap_payload, "unknown_ip", sizeof(coap_payload));
    uip_ds6_addr_t *my_addr = uip_ds6_get_global(ADDR_PREFERRED);
    if(my_addr != NULL) uiplib_ipaddr_snprint(coap_payload, sizeof(coap_payload), &my_addr->ipaddr);
    coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));
}

void sensor_coap_send_discovery_async(void) {
    sensor_coap_prepare_discovery(); 
    coap_transaction_t *t = coap_new_transaction(request->mid, &printer_ep);
    if(t) {
        t->message_len = coap_serialize_message(request, t->message);
        coap_send_transaction(t);
    }
}

void discovery_response_handler(coap_message_t* response) {
    // Left empty, handled natively via resources
}

void sensor_coap_send_off_signal(void) {
    if(!is_paired) return;
    
    static coap_message_t msg[1];
    coap_init_message(msg, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(msg, "printer/sensor_off");
    
    coap_transaction_t* trans = coap_new_transaction(msg->mid, &printer_ep);
    if(trans) {
        trans->message_len = coap_serialize_message(msg, trans->message);
        coap_send_transaction(trans);
    }
}