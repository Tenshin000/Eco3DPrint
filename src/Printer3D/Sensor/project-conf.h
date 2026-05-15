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
// #ifndef BUTTON_HAL_CONF_DEBOUNCE_DURATION
// #define BUTTON_HAL_CONF_DEBOUNCE_DURATION (CLOCK_SECOND >> 6)
// #endif

/*---------------------------------------------------------------------------*/
/* MAC Layer Configuration                                                   */
/*---------------------------------------------------------------------------*/
/* Set a custom PAN ID so your network doesn't clash with others */
#define IEEE802154_CONF_PANID 0xABCD
#define IEEE802154_CONF_DEFAULT_CHANNEL 26

/*---------------------------------------------------------------------------*/
/* USB Serial Configuration (nRF52840 Dongle)                                */
/*---------------------------------------------------------------------------*/
/*
 * Enable the USB serial interface and route standard UART output to it.
 * This is strictly required to read LOG_INFO and printf outputs on the 
 * PC terminal (e.g., /dev/ttyACM*) rather than on the physical GPIO pins.
 */
#define NRF_USB_SERIAL_ENABLE 1
#define UART_DEFAULT_TO_USB 1

/*
 * Increase the number of USB serial transmit buffers.
 * This helps prevent buffer overflows and interleaved log outputs
 * when sending long, fast bursts of data (like sensor measurements).
 */
#define USB_SERIAL_CONF_TX_BUFFERS 4

/*---------------------------------------------------------------------------*/
#endif /* PROJECT_CONF_H_ */
/*---------------------------------------------------------------------------*/
