/**
  ******************************************************************************
  * @file           : 
  * @version        :
  * @brief          :
  ******************************************************************************
  ******************************************************************************
*/

#ifndef __MQTT__
#define __MQTT___

#include "espconn.h"
#include "stdint.h"
#include <user_interface.h>

#define MAX_MSG_LEN         2048

/* Exported types ------------------------------------------------------------*/
typedef enum{
    MQTT_CLIENT_STATUS_DISCONN,
    MQTT_CLIENT_STATUS_TCP_CONN_FAIL,
    MQTT_CLIENT_STATUS_TCP_CONN_ABORT,
    MQTT_CLIENT_STATUS_TCP_CONN_RESET,
    MQTT_CLIENT_STATUS_TCP_CONN_CLOSED,
    MQTT_CLIENT_STATUS_TCP_TIMEOUT,
    MQTT_CLIENT_STATUS_BFR_OVERFLOW,
    MQTT_CLIENT_STATUS_SUB_ACK,
    MQTT_CLIENT_STATUS_SUB_NACK,
    MQTT_CLIENT_STATUS_PUB_ACK,
    MQTT_CLIENT_STATUS_PUB_NACK,
    MQTT_CLIENT_STATUS_REC_PUB,
    MQTT_CLIENT_STATUS_CONN_ACK,
    MQTT_CLIENT_STATUS_CONN_NACK
}tMqttClient_Status;

typedef struct{
    char payload[MAX_MSG_LEN];
    char topic[100];  /*therortically max topic size = 2^16 - 1*/
    uint16_t len;
    tMqttClient_Status status;
}tMqttClient_Response;

typedef void (*mqtt_callback)(tMqttClient_Response*);

/* Exported functions -------------------------------------------------------*/
void MqttClient_Init(const char*const host_ip,const uint16_t port,const char* client_id,mqtt_callback cb);
void MqttClient_Start(void);
void MqttClient_Connect(void);
void ICACHE_FLASH_ATTR MqttClient_Sub(const char* topic,uint8_t qos);
void MqttClient_Pub(const char* topic,const char* payload,const uint16_t len);


#endif