/**
 ******************************************************************************
 * @file           : 
 * @brief          : 
 ******************************************************************************
 */

/* Private includes ----------------------------------------------------------*/
#include "mqtt_client.h"
#include "tcp_conn_mgr.h"
#include "mem.h"
#include "debug.h"
#include "osapi.h"

#define UTF_STR_LEN_BYTES_NO        2
#define KEEP_ALIVE_BYTES_NO         2
#define PROTOCOL_VER_BYTES_NO       1
#define MSG_ID_BYTES_NO             2
#define SUB_REQUESTED_QOS_BYTES_NO  1

#define PROTOCOL_NAME               "MQTT"
#define PROTOCOL_VER_311            4
#define KEEP_ALIVE                  60

typedef enum{
    MQTT_MSG_RESERVED,
    MQTT_MSG_CONNECT,   /*var header req*/  /*payload*/
    MQTT_MSG_CONNACK,
    MQTT_MSG_PUBLISH,   /*var header req*/  /*optional*/
    MQTT_MSG_PUBACK,
    MQTT_MSG_PUBREC,
    MQTT_MSG_PUBREL,
    MQTT_MSG_PUBCOMP,
    MQTT_MSG_SUBSCRIBE, /*var header req*/  /*payload*/
    MQTT_MSG_SUBACK,    /*var header req*/  /*payload*/ 
    MQTT_MSG_UNSUB,
    MQTT_MSG_UNSUBACK,
    MQTT_MSG_PINGREQ,
    MQTT_MSG_PINGRESP,
    MQTT_MSG_DISCONNECT
}tMqttMsg;

typedef enum{
    MQTT_CONN_ACCEPTED,
    MQTT_CONN_REFUSED_INVALID_PROT_VER,
    MQTT_CONN_REFUSED_ID_REJECTED,
    MQTT_CONN_REFUSED_SERVER_UNAVAIL,
    MQTT_CONN_REFUSED_BAD_USR_PSSWD,
    MQTT_CONN_REFUSED_NOT_AUTHORIZED,
    MQTT_CONN_RESERVED
}tMqttConnRetCode;

typedef struct{
    uint8_t retain :1;
    uint8_t qos :2;
    uint8_t is_duplicate :1;
    uint8_t message_type :4; /*Most significant nibble*/
}tMqttCtrlField;

typedef struct{
    uint8_t reserved :1;
    uint8_t clean_session :1;
    uint8_t will_flag :1;
    uint8_t will_qos :2;
    uint8_t will_retain :1;
    uint8_t password_flag :1;
    uint8_t user_name_flag :1;
}tMqttConnFlags;

typedef union{
    uint16_t value;
    struct{
        uint8_t LSB;
        uint8_t MSB;
    }value_bytes;
}tMqttKeepAlive;

typedef struct{
    char host_ip[30];
    uint16_t port;
    char client_id[23];  /*It must be unique across all clients connecting to a single server, 
    and is the key in handling Message IDs messages with QoS levels 1 and 2*/
    mqtt_callback cb;
}tMqttClient_Config;

/* Private variables ---------------------------------------------------------*/
static tMqttClient_Config config;
static struct espconn *tcp_conn;
static uint8_t is_connected = 0;
static os_event_t	*initQueue;
static tMqttClient_Res res;
static uint8_t out_msg_buffer[MAX_MSG_LEN];
static uint16_t curr_out_msg_id;

/* Private function prototypes -----------------------------------------------*/
static void MqttClient_ConnectionCb(void* arg);
static void MqttClient_DisconnectionCb(void* arg);
static void MqttClient_SentCb(void* arg);
static void MqttClient_ReceiveCb(void* arg,char *tcp_payload_ptr,unsigned short tcp_payload_len);
static void MqttClient_ReconnCb(void* arg,sint8 error_code);
static uint8_t MqttClient_DecodeRemainingLength(char* remaining_len_field_start,uint32_t *p_rem_len);
static uint8_t MqttClient_EncodeRemainingLength(uint32_t len,char* buffer);
static void MqttClient_PubAck(const char* topic);
static uint8_t ICACHE_FLASH_ATTR MqttClient_BuildMsgHeader(tMqttCtrlField ctrl_field,uint16_t var_len_header_len,uint16_t payload_len,char *msg);
static void ICACHE_FLASH_ATTR MqttClient_BuildUtfString(const char* in_string,char *out_buffer);

/**
    *@brief 
    *@param void
    *@retval void
*/
static uint8_t ICACHE_FLASH_ATTR MqttClient_DecodeRemainingLength(char* remaining_len_field_start,uint32_t *p_rem_len)
{
    uint8_t itr = 0;

    *p_rem_len = 0;

    do{
        *p_rem_len += remaining_len_field_start[itr] & 0x7f;
    }while((remaining_len_field_start[itr++] & (1 << 7)) != 0 && (itr < 4));

    return itr;
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static uint8_t ICACHE_FLASH_ATTR MqttClient_EncodeRemainingLength(uint32_t len,char* buffer)
{
    uint8_t itr = 0;

    while(itr < 4 &&
        len > 0)
    {
        buffer[itr++] = len >= 127 ? (127 | (1 << 7)) : len;

        len -= len >= 127 ? 127 : len;
    }

    return itr;
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_ConnectionCb(void* arg)
{
    uint16_t i;
    uint16_t msg_itr = 0;
    tMqttCtrlField ctrl_field;
    uint16_t var_header_len = UTF_STR_LEN_BYTES_NO + /*UTF-encodded protocol name*/
        os_strlen(PROTOCOL_NAME) +
        PROTOCOL_VER_BYTES_NO + /*version*/
        sizeof(tMqttConnFlags) + /*connect flags*/
        KEEP_ALIVE_BYTES_NO; /*keep alive */
    tMqttConnFlags conn_flags;
    tMqttKeepAlive keep_alive;

    os_printf("TCP Connection established \n");

    ctrl_field.message_type = MQTT_MSG_CONNECT;
    conn_flags.clean_session = 1;
    keep_alive.value = KEEP_ALIVE;

    msg_itr += MqttClient_BuildMsgHeader(ctrl_field,var_header_len,os_strlen(config.client_id) + UTF_STR_LEN_BYTES_NO,out_msg_buffer);
    MqttClient_BuildUtfString(PROTOCOL_NAME,out_msg_buffer + msg_itr);
    msg_itr += os_strlen(PROTOCOL_NAME) + UTF_STR_LEN_BYTES_NO;
    out_msg_buffer[msg_itr++] = PROTOCOL_VER_311;
    out_msg_buffer[msg_itr++] = *((char*)&conn_flags);
    out_msg_buffer[msg_itr++] = *((char*)&keep_alive.value_bytes.MSB);
    out_msg_buffer[msg_itr++] = *((char*)&keep_alive.value_bytes.LSB);
    MqttClient_BuildUtfString(config.client_id,out_msg_buffer + msg_itr);
    msg_itr += os_strlen(config.client_id) + UTF_STR_LEN_BYTES_NO;

    espconn_send((struct espconn *)tcp_conn,out_msg_buffer,msg_itr);

    os_printf("\nBuilt CONN msg :\n");
    for(i = 0 ; i < msg_itr ; i++)
    {
     os_printf("%x ",out_msg_buffer[i]);
    }
    os_printf("\n");
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_DisconnectionCb(void* arg)
{
    os_printf("Disconnect success callback \n");
    
    is_connected = 0;

    res.status = MQTT_CLIENT_STATUS_DISCONN;

    config.cb(&res);
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_SentCb(void* arg)
{
    os_printf("Send next packet if any \n"); 

    os_free(out_msg_buffer);
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_ReceiveCb(void* arg,char *tcp_payload_ptr,unsigned short tcp_payload_len)
{
    /*Parse frame and return response based on msg type*/
    uint16_t received_pub_msg_topic_len = 0;
    char *parsing_ptr = tcp_payload_ptr;

    tMqttCtrlField ctrl_field = *((tMqttCtrlField*)(parsing_ptr++));
    uint32_t rem_len;

    parsing_ptr += MqttClient_DecodeRemainingLength(parsing_ptr,&rem_len);

    os_printf("\nreceived msg type %d\n",ctrl_field.message_type);
    os_printf("\nreceived msg rem len %d\n",rem_len);

    /*processing parsed data*/
    switch(ctrl_field.message_type)
    {
        case MQTT_MSG_CONNACK:
            // If the client does not receive a CONNACK message from the server within a reasonable
            // amount of time, the client should close the TCP/IP socket connection, and restart the
            // session by opening a new socket to the server and issuing a CONNECT message

            if(*(parsing_ptr + 1) == MQTT_CONN_ACCEPTED) /*return code , first byte is reserved*/
            {
                is_connected = 1;

                res.status = MQTT_CLIENT_STATUS_CONN_ACK;
            }
            else
            {
                /*rejected*/
                res.status = MQTT_CLIENT_STATUS_CONN_NACK;
            }
            break;
        case MQTT_MSG_PUBLISH:
            /*received PUB*/
            if(rem_len)
            {
                received_pub_msg_topic_len = (parsing_ptr[0] << 8) | parsing_ptr[1];
                parsing_ptr += UTF_STR_LEN_BYTES_NO; /*UTF encodded topic string length*/
                os_memcpy(res.topic,parsing_ptr,received_pub_msg_topic_len);
                parsing_ptr += received_pub_msg_topic_len;
                if(ctrl_field.qos != 0)
                {
                    parsing_ptr += MSG_ID_BYTES_NO; /*msg id*/
                }
                res.len = tcp_payload_len - (parsing_ptr - tcp_payload_ptr);
                os_memcpy(res.payload,parsing_ptr,res.len);

                res.status = MQTT_CLIENT_STATUS_REC_PUB;
            }

            /*Send PUBACK at Qos 1 or PUBREC at Qos 2*/
            break;
        case MQTT_MSG_SUBACK:
            if(rem_len)
            {
                res.status = MQTT_CLIENT_STATUS_SUB_ACK;
            }
            else
            {
                res.status = MQTT_CLIENT_STATUS_SUB_NACK;
            }
            
            break;
        case MQTT_MSG_DISCONNECT:
            /*post event to call espconn_disconnect*/
            res.status = MQTT_CLIENT_STATUS_DISCONN;
            break;
        default:
            break;
    }

    config.cb(&res);
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_ReconnCb(void* arg,sint8 error_code)
{
    switch(error_code)
    {
        case ESPCONN_TIMEOUT:
            os_printf("Timeout error \n");
            res.status = MQTT_CLIENT_STATUS_TCP_TIMEOUT;
        break;
        case ESPCONN_ABRT:
            os_printf("Abort error \n");
            res.status = MQTT_CLIENT_STATUS_TCP_CONN_ABORT;
        break;
        case ESPCONN_RST:
            os_printf("Connection reset error \n");
            res.status = MQTT_CLIENT_STATUS_TCP_CONN_RESET;
        break;
        case ESPCONN_CLSD:
            os_printf("Connection closed error \n");
            res.status = MQTT_CLIENT_STATUS_TCP_CONN_CLOSED;
        break;
        case ESPCONN_CONN:
            os_printf("Connection failed error \n");
            res.status = MQTT_CLIENT_STATUS_TCP_CONN_FAIL;
        break;
    }

    config.cb(&res);
}

/**
    *@brief 
    *@param void
    *@retval void
*/
void ICACHE_FLASH_ATTR MqttClient_Config(const char*const host_ip,const uint16_t port,const char* client_id,mqtt_callback cb)
{
    config.host_ip[0] = atoi(strtok(host_ip,"."));
    config.host_ip[1] = atoi(strtok(0,"."));
    config.host_ip[2] = atoi(strtok(0,"."));
    config.host_ip[3] = atoi(strtok(0,"."));
    os_printf("\nmqtt host ip : %d : %d : %d : %d",config.host_ip[0],config.host_ip[1],config.host_ip[2],config.host_ip[3]);
    config.port = port;
    os_strcpy(config.client_id,client_id);
    config.cb = cb;
}

/**
    *@brief 
    *@param void
    *@retval void
*/
void ICACHE_FLASH_ATTR MqttClient_Init(void)
{
    struct ip_info ipConfig;

    tcp_conn = TcpMgr_OpenConn();

    if(tcp_conn)
    {
        tcp_conn->proto.tcp->remote_ip[0] = ((uint8*)(&config.host_ip))[0];
        tcp_conn->proto.tcp->remote_ip[1] = ((uint8*)(&config.host_ip))[1];
        tcp_conn->proto.tcp->remote_ip[2] = ((uint8*)(&config.host_ip))[2];
        tcp_conn->proto.tcp->remote_ip[3] = ((uint8*)(&config.host_ip))[3];
        tcp_conn->proto.tcp->remote_port = config.port;

        tcp_conn->proto.tcp->local_port = espconn_port();
        wifi_get_ip_info(STATION_IF, &ipConfig);
        os_memcpy(tcp_conn->proto.tcp->local_ip, (uint8*)&ipConfig.ip.addr, 4);

        espconn_regist_connectcb(tcp_conn,MqttClient_ConnectionCb);
        espconn_regist_disconcb(tcp_conn,MqttClient_DisconnectionCb);
        espconn_regist_sentcb(tcp_conn,MqttClient_SentCb);
        espconn_regist_recvcb(tcp_conn,MqttClient_ReceiveCb);
        espconn_regist_reconcb(tcp_conn,MqttClient_ReconnCb);
    }
}

/**
    *@brief 
    *@param void
    *@retval void
*/
void ICACHE_FLASH_ATTR MqttClient_Connect(void)
{
    if(is_connected == 0)
    {
        espconn_connect(tcp_conn);
    }
}

/**
    *@brief 
    *@param void
    *@retval void
*/
void ICACHE_FLASH_ATTR MqttClient_Sub(const char* topic,uint8_t qos)
{
    uint16_t i;
    uint16_t msg_itr = 0;
    tMqttCtrlField ctrl_field;
    const uint16_t var_header_len = MSG_ID_BYTES_NO; /*2 bytes Message ID*/

    ctrl_field.qos = 1;

    /*Send MQTT sub message*/
    if(is_connected)
    {
        // The Message ID is a 16-bit unsigned integer that must be unique amongst the set of "in
        // flight" messages in a particular direction of communication. It typically increases by
        // exactly one from one message to the next, but is not required to do so.

        ctrl_field.message_type = MQTT_MSG_SUBSCRIBE;

        msg_itr += MqttClient_BuildMsgHeader(ctrl_field,var_header_len,os_strlen(topic) + UTF_STR_LEN_BYTES_NO + SUB_REQUESTED_QOS_BYTES_NO,out_msg_buffer);
        curr_out_msg_id++;
        out_msg_buffer[msg_itr++] = curr_out_msg_id >> 8;  /*MSB*/
        out_msg_buffer[msg_itr++] = curr_out_msg_id;       /*LSB*/
        MqttClient_BuildUtfString(topic,out_msg_buffer + msg_itr);
        msg_itr += os_strlen(topic) + UTF_STR_LEN_BYTES_NO;
        out_msg_buffer[msg_itr++] = qos & 0x03;

        espconn_send((struct espconn *)tcp_conn,out_msg_buffer,msg_itr);

        os_printf("\nBuilt SUB msg :\n");
        for(i = 0 ; i < msg_itr ; i++)
        {
        os_printf("%x ",out_msg_buffer[i]);
        }
        os_printf("\n");
    }
}

/**
    *@brief 
    *@param void
    *@retval void
*/
void ICACHE_FLASH_ATTR MqttClient_Pub(const char* topic,const char* payload,const uint16_t len)
{
    /*Send MQTT pub message*/
    if(is_connected)
    {

    }
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_PubAck(const char* topic)
{
    /*Send MQTT puback message*/
    if(is_connected)
    {

    }
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static uint8_t ICACHE_FLASH_ATTR MqttClient_BuildMsgHeader(tMqttCtrlField ctrl_field,
    uint16_t var_len_header_len,
    uint16_t payload_len,
    char *msg)
{
    uint16_t rem_len = var_len_header_len + payload_len;
    uint8_t rem_len_bfr[4] = {0};
    uint8_t rem_len_bytes_no = MqttClient_EncodeRemainingLength(rem_len,rem_len_bfr);
    uint32_t itr = 0;
    uint32_t i = 0;

    if(msg)
    {
        msg[itr++] = *((char*)&ctrl_field);
        for(i = 0; i < rem_len_bytes_no ; i++)
        {
            msg[itr++] = rem_len_bfr[rem_len_bytes_no - 1 - i];
        }
    }

    return itr;
}

/**
    *@brief 
    *@param void
    *@retval void
*/
static void ICACHE_FLASH_ATTR MqttClient_BuildUtfString(const char* in_string,char *out_buffer)
{
    uint16_t in_string_len = os_strlen(in_string);
    uint16 temp_len = in_string_len;

    out_buffer[1] = temp_len >= 255 ? 255 : temp_len;
    temp_len -= out_buffer[1];
    out_buffer[0] = temp_len >= 255 ? 255 : temp_len;

    os_memcpy(out_buffer + 2,in_string,in_string_len);
}