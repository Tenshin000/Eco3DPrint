#include "network_coap.h"
#include "contiki.h"
#include "coap-engine.h"
#include "coap-blocking-api.h"
#include "os/lib/json/jsonparse.h"
#include "sys/log.h"
#include <string.h>
#include <stdio.h>

// Logging configuration
#define LOG_MODULE "Dev:CoAP"
#define LOG_LEVEL LOG_LEVEL_APP

#define CLOUD_SERVER_EP "coap://[fd00::1]"

// Internal CoAP state variables
static bool is_registered = false;
static bool stl_job_available = false;
static int print_time_seconds = 0;

// Internal CoAP connection variables
static coap_endpoint_t server_ep;
static coap_message_t request[1];
static coap_callback_request_state_t coap_req_state;

extern struct process smart_printer_process; // Reference to wake up main process

// CoAP Server: Handler for incoming STL Job via POST
static void res_job_post_handler(coap_message_t *request, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
    const uint8_t *payload = NULL;
    int len = coap_get_payload(request, &payload);

    if(len > 0){
        LOG_INFO("Received CoAP POST with STL JSON payload: %.*s\n", len, (char *)payload);

        // Parse JSON to extract 'time' parameter
        struct jsonparse_state js;
        jsonparse_setup(&js, (const char *)payload, len);
        int type;
        
        while((type = jsonparse_next(&js)) != 0){
            if(type == JSON_TYPE_PAIR_NAME){
                if(jsonparse_strcmp_value(&js, "time") == 0){
                    jsonparse_next(&js); 
                    print_time_seconds = jsonparse_get_value_as_int(&js);
                }
            }
        }

        LOG_INFO("STL Job parsed. Estimated time: %d seconds.\n", print_time_seconds);
        stl_job_available = true;
        
        coap_set_status_code(response, CREATED_2_01);
        process_poll(&smart_printer_process); // Wake up main loop
    } 
    else{
        coap_set_status_code(response, BAD_REQUEST_4_00);
    }
}

// Define the CoAP Resource (URI: /printer/job)
RESOURCE(res_printer_job,
         "title=\"STL Job Receiver\";rt=\"Control\"",
         NULL,
         res_job_post_handler,
         NULL,
         NULL);

// CoAP Client: Callback for Registration Response
static void registration_callback(coap_callback_request_state_t *req_state){
    coap_message_t *response = req_state->state.response;

    if(response != NULL){
        const uint8_t *payload = NULL;
        int len = coap_get_payload(response, &payload);
        
        // Handle the incoming registration message payload
        if(len > 0){
            LOG_INFO("Cloud Server replied: %.*s\n", len, (char *)payload);
            
            // You can add logic here to check if the server explicitly accepted it
            if(strstr((char *)payload, "Success") != NULL) {
                LOG_INFO("CoAP Registration explicitly successful!\n");
                is_registered = true;
            } else {
                LOG_WARN("Registration recognized, but no Success string found.\n");
                is_registered = true; // Still register, or set to false to reject
            }
        } else {
            LOG_INFO("CoAP Registration successful (no payload provided)!\n");
            is_registered = true;
        }
    } 
    else{
        LOG_ERR("CoAP Registration timed out.\n");
        is_registered = false;
    }
    
    process_poll(&smart_printer_process); // Wake up main loop to evaluate state
}

/* Public API Implementations */
void coap_network_init(void){
    LOG_INFO("Initializing CoAP Network module...\n");
    coap_activate_resource(&res_printer_job, "printer/job");
    is_registered = false;
    stl_job_available = false;
}

void coap_network_register(void){
    LOG_INFO("Sending CoAP Registration request...\n");
    coap_endpoint_parse(CLOUD_SERVER_EP, strlen(CLOUD_SERVER_EP), &server_ep);
    coap_init_message(request, COAP_TYPE_CON, COAP_POST, 0);
    coap_set_header_uri_path(request, "/register");
    
    const char msg[] = "{\"device_id\":\"printer_01\"}";
    coap_set_payload(request, (uint8_t *)msg, strlen(msg));
    
    coap_send_request(&coap_req_state, &server_ep, request, registration_callback);
}

bool is_coap_registered(void){
    return is_registered;
}

bool has_new_stl_job(void){
    return stl_job_available;
}

int get_print_time(void){
    return print_time_seconds;
}

void clear_stl_job(void){
    stl_job_available = false;
}

void reset_coap_state(void){
    is_registered = false;
    stl_job_available = false;
    print_time_seconds = 0;
}