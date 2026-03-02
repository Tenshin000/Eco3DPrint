/*---------------------------------------------------------------------------*/
#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_
/*---------------------------------------------------------------------------*/

/* Application Log Level */
#define LOG_LEVEL_APP LOG_LEVEL_DBG

/*---------------------------------------------------------------------------*/
/* TCP/IP and MQTT Configuration                                             */
/*---------------------------------------------------------------------------*/
/* Enable TCP (Required for MQTT) */
#define UIP_CONF_TCP 1

/* Enable ICMPv6 (Useful for Ping and Network Diagnostics) */
#define UIP_CONF_ICMP6 1

/*
 * The IPv6 address of the MQTT broker to connect to.
 * Ignored if MQTT_CLIENT_CONF_WITH_IBM_WATSON is 1
 */
#define MQTT_CLIENT_CONF_BROKER_IP_ADDR "fd00::1"

/*
 * The MQTT username.
 * Ignored in Watson mode: In this mode the username is always "use-token-auth"
 */
#define MQTT_CLIENT_CONF_USERNAME "mqtt-client-username"

/*
 * The MQTT auth token (password) used when connecting to the MQTT broker.
 * Used with as well as without Watson.
 * Transported in cleartext!
 */
#define MQTT_CLIENT_CONF_AUTH_TOKEN "AUTHTOKEN"

/*---------------------------------------------------------------------------*/
/* CoAP Configuration                                                        */
/*---------------------------------------------------------------------------*/
/* * Increase the maximum CoAP chunk size. 
 * This is CRITICAL because we are sending and receiving JSON strings. 
 * The default size might truncate our payload.
*/
#undef COAP_MAX_CHUNK_SIZE
#define COAP_MAX_CHUNK_SIZE 128

/*---------------------------------------------------------------------------*/
/* Hardware Configuration                                                    */
/*---------------------------------------------------------------------------*/
/* Ensure the button HAL ignores debouncing artifacts for your 3-second long press */
#define BUTTON_HAL_CONF_DEBOUNCE_DURATION (CLOCK_SECOND >> 6)

/*---------------------------------------------------------------------------*/
/* MAC Layer Configuration                                                   */
/*---------------------------------------------------------------------------*/
/* Set a custom PAN ID so your network doesn't clash with others */
#define IEEE802154_CONF_PANID 0xABCD
#define IEEE802154_CONF_DEFAULT_CHANNEL 26

/*---------------------------------------------------------------------------*/
#endif /* PROJECT_CONF_H_ */
/*---------------------------------------------------------------------------*/