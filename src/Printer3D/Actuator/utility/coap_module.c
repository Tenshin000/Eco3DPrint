#include "coap_module.h"

#include "device_conf.h"
#include "sys/log.h"
#include "sys/node-id.h"
#include <string.h>
#include "net/ipv6/uip-ds6.h"
#include "net/ipv6/uiplib.h"

#define LOG_MODULE "CoAP Module"
#define LOG_LEVEL LOG_LEVEL_APP

coap_endpoint_t server_ep;
coap_endpoint_t sensor_ep;
coap_endpoint_t multicast_ep;
coap_message_t request[1];

static char coap_payload[256];
char paired_sensor_ip_str[UIPLIB_IPV6_MAX_STR_LEN] = "";
bool sensor_is_paired = false;

process_event_t event_sensor_paired;
process_event_t event_sensor_unpaired;

extern struct process smart_printer_process;

#ifdef DEV_DONGLE
static const char* known_dongle_ips[3] = {
    "fd00::f6ce:366a:718b:73f2",
    "fd00::f6ce:36fa:435f:f3d6",
    "fd00::f6ce:36cf:5367:3d5a"
};
static uint8_t current_dongle_idx = 0;
#endif

// FIX: Dedicated function to send a direct reciprocal ping without rotating the IP list
static void coap_module_send_direct_ping(coap_endpoint_t *target_ep) {
    coap_init_message(request, COAP_TYPE_NON, COAP_POST, coap_get_mid());
    coap_set_header_uri_path(request, "sensor/discovery");
    
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
static void res_health_get_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  device_state_t current = device_get_state();
  const char* msg = device_get_state_string(current);
  if(current == STATE_OFF) return;

  if(current != STATE_ONLINE && current != STATE_PRINTING){
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }
  coap_set_status_code(response, CONTENT_2_05);
  coap_set_header_content_format(response, TEXT_PLAIN);
  coap_set_payload(response, (uint8_t *)msg, strlen(msg));
}

static void res_print_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  uint32_t block_num = 0; uint8_t more = 0; uint16_t block_size = 0; uint32_t block_offset = 0;
  device_state_t current = device_get_state();
  
  if(current != STATE_ONLINE){
    coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
    return;
  }

  if(coap_get_header_block1(req, &block_num, &more, &block_size, &block_offset)) {
    const uint8_t *payload_ptr = NULL;
    int len = coap_get_payload(req, &payload_ptr);
    device_add_stl_length(len);

    if(more){
      coap_set_status_code(response, CONTINUE_2_31);
      coap_set_header_block1(response, block_num, more, block_size);
    } 
    else{
      coap_set_status_code(response, CHANGED_2_04);
      coap_set_header_block1(response, block_num, more, block_size);
      
      device_trigger_print_simulation();
    }
  } 
  else{
    coap_set_status_code(response, BAD_REQUEST_4_00);
  }
}

static void res_sensor_off_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  LOG_WARN("Sensor sent an OFF notification! Unpairing...\n");
  sensor_is_paired = false;
  memset(paired_sensor_ip_str, 0, sizeof(paired_sensor_ip_str));
  coap_set_status_code(response, CHANGED_2_04);
  
  process_post(&smart_printer_process, event_sensor_unpaired, NULL);
}

static void res_printer_discovery_post_handler(coap_message_t *req, coap_message_t *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset){
  if(device_get_state() == STATE_OFF) {
      coap_set_status_code(response, SERVICE_UNAVAILABLE_5_03);
      return;
  }

  const uint8_t *payload = NULL;
  int len = coap_get_payload(req, &payload);
  
  if(len > 0 && payload != NULL){
      char new_ip[UIPLIB_IPV6_MAX_STR_LEN];
      memset(new_ip, 0, sizeof(new_ip));
      int cp_len = len < sizeof(new_ip) - 1 ? len : sizeof(new_ip) - 1;
      memcpy(new_ip, payload, cp_len);
      new_ip[cp_len] = '\0';
      
      if(strncmp(new_ip, "unknown_ip", 10) == 0) {
          #ifdef DEV_COOJA
          uint16_t sensor_id = node_id + 1;
          uip_ipaddr_t sensor_ip;
          uip_ip6addr(&sensor_ip, 0xfd00, 0, 0, 0, 0x0200 + sensor_id, sensor_id, sensor_id, sensor_id);
          uiplib_ipaddr_snprint(new_ip, sizeof(new_ip), &sensor_ip);
          #elif defined(DEV_DONGLE)
          uiplib_ipaddr_snprint(new_ip, sizeof(new_ip), &req->src_ep->ipaddr);
          #endif
      }
      
      coap_set_status_code(response, CONTENT_2_05);

      if(sensor_is_paired && strcmp(paired_sensor_ip_str, new_ip) == 0) {
          return;
      }

      memset(paired_sensor_ip_str, 0, sizeof(paired_sensor_ip_str));
      strcpy(paired_sensor_ip_str, new_ip);
      
      coap_endpoint_copy(&sensor_ep, req->src_ep);
      sensor_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
      sensor_is_paired = true;
      
      LOG_INFO("Incoming pairing request accepted from Sensor IP: %s\n", paired_sensor_ip_str);
      process_post(&smart_printer_process, event_sensor_paired, NULL);
      
      // FIX: Reply directly to the sensor bypassing the rotation logic
      coap_module_send_direct_ping(&sensor_ep);
      
  } else {
      coap_set_status_code(response, BAD_REQUEST_4_00);
  }
}

/* ==================================================== */
/* =                    RESOURCES                     = */
/* ==================================================== */ 
RESOURCE(res_health, "title=\"Health Check\";rt=\"Text\"", res_health_get_handler, NULL, NULL, NULL);
RESOURCE(res_print, "title=\"Print Job\";rt=\"Block\"", NULL, res_print_post_handler, NULL, NULL);
RESOURCE(res_sensor_off, "title=\"Sensor OFF\";rt=\"Control\"", NULL, res_sensor_off_post_handler, NULL, NULL);
RESOURCE(res_printer_discovery, "title=\"Printer Discovery\";rt=\"Text\"", NULL, res_printer_discovery_post_handler, NULL, NULL);

/* ==================================================== */
/* =                MODULE FUNCTIONS                  = */
/* ==================================================== */
void coap_module_init(void){
  static bool initialized = false;
  if(!initialized) {
      event_sensor_paired = process_alloc_event();
      event_sensor_unpaired = process_alloc_event();
      coap_activate_resource(&res_health, "health");
      coap_activate_resource(&res_print, "print");
      coap_activate_resource(&res_sensor_off, "printer/sensor_off");
      coap_activate_resource(&res_printer_discovery, "printer/discovery");
      initialized = true;
  }
}

uint16_t coap_module_prepare_request(const char* message, coap_message_type_t type, uint8_t method, const char* uri_path){
  memset(coap_payload, 0, sizeof(coap_payload));
  if(message != NULL) strncpy(coap_payload, message, sizeof(coap_payload) - 1);
  coap_endpoint_parse(CLOUD_SERVER_EP, strlen(CLOUD_SERVER_EP), &server_ep);

  uint16_t mid = coap_get_mid();
  coap_init_message(request, type, method, mid);

  if(uri_path != NULL) coap_set_header_uri_path(request, uri_path);
  if(message != NULL) coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));
  return mid;
}

uint16_t coap_module_prepare_sensor_command(const char* command){
  uint16_t mid = coap_get_mid();
  coap_init_message(request, COAP_TYPE_CON, COAP_POST, mid);
  coap_set_header_uri_path(request, "sensor/print");
  
  if(command != NULL){
    strncpy(coap_payload, command, sizeof(coap_payload) - 1);
    coap_payload[sizeof(coap_payload) - 1] = '\0';
    coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));
  }
  return mid;
}

void coap_module_send_sensor_command_non_blocking(const char* command) {
    uint16_t mid = coap_module_prepare_sensor_command(command);
    coap_transaction_t* transaction = coap_new_transaction(mid, &sensor_ep);
    if(transaction) {
        transaction->message_len = coap_serialize_message(request, transaction->message);
        coap_send_transaction(transaction);
    }
}

uint16_t coap_module_prepare_unpair(void){
  uint16_t mid = coap_get_mid();
  coap_init_message(request, COAP_TYPE_NON, COAP_POST, mid);
  coap_set_header_uri_path(request, "sensor/unpair");
  return mid;
}

void coap_module_prepare_discovery(void) {
  #ifdef DEV_COOJA
  uint16_t sensor_id = node_id + 1;
  uip_ipaddr_t sensor_ip;
  uip_ip6addr(&sensor_ip, 0xfd00, 0, 0, 0, 0x0200 + sensor_id, sensor_id, sensor_id, sensor_id);
  memset(&multicast_ep, 0, sizeof(multicast_ep));
  uip_ipaddr_copy(&multicast_ep.ipaddr, &sensor_ip);
  multicast_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
  multicast_ep.secure = 0;
  
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

  memset(&multicast_ep, 0, sizeof(multicast_ep));
  uip_ipaddr_copy(&multicast_ep.ipaddr, &target_ip);
  multicast_ep.port = UIP_HTONS(COAP_DEFAULT_PORT);
  multicast_ep.secure = 0;
  #else
  coap_endpoint_parse("coap://[ff02::1]", 16, &multicast_ep); 
  #endif
  
  coap_init_message(request, COAP_TYPE_NON, COAP_POST, coap_get_mid());
  coap_set_header_uri_path(request, "sensor/discovery");
  
  strncpy(coap_payload, "unknown_ip", sizeof(coap_payload));
  uip_ds6_addr_t *my_addr = uip_ds6_get_global(ADDR_PREFERRED);
  if(my_addr != NULL) uiplib_ipaddr_snprint(coap_payload, sizeof(coap_payload), &my_addr->ipaddr);
  coap_set_payload(request, (uint8_t *)coap_payload, strlen(coap_payload));
}

void coap_module_send_discovery_async(void) {
    coap_module_prepare_discovery(); 
    coap_transaction_t *t = coap_new_transaction(request->mid, &multicast_ep);
    if(t) {
        t->message_len = coap_serialize_message(request, t->message);
        coap_send_transaction(t);
    }
}

/* ==================================================== */
/* =             RESPONSE HANDLERS                    = */
/* ==================================================== */
void registration_handler(coap_message_t* response){
  if(device_get_state() == STATE_OFF) return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
    device_set_state(STATE_OFFLINE);
    return; 
  } 

  if(response->code == 65 || response->code == 67){ 
      if(device_get_state() != STATE_PRINTING) {
          device_set_state(STATE_ONLINE);
      }
  } else {
      LOG_ERR("Registration Failed with code: %d\n", response->code);
      device_set_state(STATE_OFFLINE);
  }
}

void print_finished_handler(coap_message_t* response){
  if(device_get_state() == STATE_OFF) return;

  if(response == NULL){
    LOG_ERR("No response from server\n");
  } else if(response->code != 68){
      LOG_ERR("Memorization of the print failed with code: %d\n", response->code);
  }

  device_reset_waiting_confirmation(); 
  device_set_state(STATE_ONLINE);
}

void discovery_handler(coap_message_t* response) {
  // Not used natively as handler for Async
}

void sensor_command_handler(coap_message_t* response) {
  if(response == NULL) LOG_WARN("Sensor did not acknowledge the command.\n");
}