#include "coap_module.h"

#include "device_conf.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "CoAP Module"
#define LOG_LEVEL LOG_LEVEL_APP

coap_endpoint_t server_ep;
coap_message_t request[1];
static char coap_payload[128];

/* ==================================================== */
/* =              RESOURCE HANDLERS                   = */
/* ==================================================== */
// GET handler for the /health resource
static void res_health_get_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  device_state_t current = device_get_state();
  const char* msg = device_get_state_string(current);
  
  if(current == STATE_OFF)
    return;

  // If the device is not online or printing, reject the ping with a 5.03 Service Unavailable
  if(current != STATE_ONLINE && current != STATE_PRINTING){
    LOG_WARN("Health check received but device is not ready (State: %s)\n", msg);
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  // LOG_INFO("Health check ping received! Responding with state: %s\n", msg);

  coap_set_status_code(response, CONTENT_2_05);
  coap_set_header_content_format(response, TEXT_PLAIN);
  coap_set_payload(response, (uint8_t *)msg, strlen(msg));
}

// POST handler for the /print resource (Handles Block-wise transfer)
static void res_print_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  uint32_t block_num = 0;
  uint8_t more = 0;
  uint16_t block_size = 0;
  uint32_t block_offset = 0;
  device_state_t current = device_get_state();
  
  // Refuse incoming files if the node is not ONLINE
  if(current != STATE_ONLINE){
    LOG_WARN("Print job rejected: device is currently in state %s\n", device_get_state_string(current));
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  if(coap_get_header_block1(req, &block_num, &more, &block_size, &block_offset)) {
    const uint8_t *payload_ptr = NULL;
    int len = coap_get_payload(req, &payload_ptr);

    device_add_stl_length(len);
    // LOG_INFO("Received Block %lu (Size: %d bytes)\n", (unsigned long)block_num, len);

    if(more){
      coap_set_status_code(response, CONTINUE_2_31);
      coap_set_header_block1(response, block_num, more, block_size);
    } 
    else{
      // LOG_INFO("Final STL block received!\n");
      
      coap_set_status_code(response, CHANGED_2_04);
      coap_set_header_block1(response, block_num, more, block_size);
      
      device_set_state(STATE_PRINTING);
      device_trigger_print_simulation();
    }
  } 
  else{
    LOG_WARN("Missing Block1 option in /print POST request\n");
    coap_set_status_code(response, BAD_REQUEST_4_00);
  }
}

/* ==================================================== */
/* =                    RESOURCES                     = */
/* ==================================================== */ 
RESOURCE(res_health,
  "title=\"Health Check\";rt=\"Text\"",
  res_health_get_handler,
  NULL,
  NULL,
  NULL);

RESOURCE(res_print,
  "title=\"Print Job\";rt=\"Block\"",
  NULL,
  res_print_post_handler,
  NULL,
  NULL);

/* ==================================================== */
/* =                MODULE FUNCTIONS                  = */
/* ==================================================== */
void coap_module_init(void){
  coap_activate_resource(&res_health, "health");
  coap_activate_resource(&res_print, "print");
}

uint16_t coap_module_prepare_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path){
  memset(coap_payload, 0, sizeof(coap_payload));

  if(message != NULL)
    strncpy(coap_payload, message, sizeof(coap_payload) - 1);

  coap_endpoint_parse(CLOUD_SERVER_EP, strlen(CLOUD_SERVER_EP), &server_ep);

  uint16_t mid = coap_get_mid();
  coap_init_message(request, type, method, mid);

  if(uri_path != NULL)
    coap_set_header_uri_path(request, uri_path);

  if(message != NULL)
    coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));

  return mid;
}

/* ==================================================== */
/* =             RESPONSE HANDLERS                    = */
/* ==================================================== */
void registration_handler(coap_message_t* response){
  const uint8_t* payload_ptr = NULL;
  int len = coap_get_payload(response, &payload_ptr);
  
  if(device_get_state() == STATE_OFF)
    return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
    device_set_state(STATE_OFFLINE);
    return; 
  } 

  if(response->code == 65 || response->code == 67){ 
      // LOG_INFO("%s Successful\n", (response->code == 65) ? "Registration" : "Login");
      device_set_state(STATE_ONLINE);

      // Trigger potential MQTT connection
      device_trigger_mqtt_retry();
  }
  else{
      LOG_ERR("Registration Failed with code: %d\n", response->code);
      if(len > 0){
          char buffer[64]; 
          int copy_len = len < sizeof(buffer) - 1 ? len : sizeof(buffer) - 1;
          memcpy(buffer, payload_ptr, copy_len);
          buffer[copy_len] = '\0'; 
          LOG_ERR("Server Error Message: %s\n", buffer);
      }
      device_set_state(STATE_OFFLINE);
  }
}

void print_finished_handler(coap_message_t* response){
  const uint8_t* payload_ptr = NULL;
  int len = coap_get_payload(response, &payload_ptr);

  if(device_get_state() == STATE_OFF)
    return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
    device_set_state(STATE_ONLINE);
    device_reset_waiting_confirmation(); 
    return; 
  } 

  if(response->code == 68){
      // LOG_INFO("Print Memorized in the Database\n");
  }
  else{
      LOG_ERR("Memorization of the print failed with code: %d\n", response->code);
      if(len > 0){
          char buffer[64]; 
          int copy_len = len < sizeof(buffer) - 1 ? len : sizeof(buffer) - 1;
          memcpy(buffer, payload_ptr, copy_len);
          buffer[copy_len] = '\0'; 
          LOG_ERR("Server Error Message: %s\n", buffer);
      }
  }

  device_reset_waiting_confirmation(); 
  device_set_state(STATE_ONLINE);
}