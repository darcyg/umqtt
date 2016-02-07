
#ifndef __UMQTT_H__
#define __UMQTT_H__

typedef struct
{
    bool sessionPresent;
    uint8_t returnCode;
} Mqtt_Connect_Result_t;

typedef struct
{
    uint16_t len;
    uint8_t *data;
} Mqtt_Data_t;

#define MQTT_INIT_DATA_STR(var, str) do{(var).data=(uint8_t*)(str);(var).len=strlen(str);}while(0)
#define MQTT_INIT_DATA_STATIC_BUF(v,b) do{(v).data=(b);(v).len=sizeof(b);}while(0)

typedef struct
{
    bool cleanSession;
    bool willRetain;
    uint8_t qos;
    uint16_t keepAlive;
    Mqtt_Data_t clientId;
    Mqtt_Data_t willTopic;
    Mqtt_Data_t willMessage;
    Mqtt_Data_t username;
    Mqtt_Data_t password;
} Mqtt_Connect_Options_t;

#define CONNECT_OPTIONS_INITIALIZER \
{ false, false, 0, 0, \
  { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }}

typedef struct
{
    bool dup;
    bool retain;
    uint8_t qos;
    Mqtt_Data_t topic;
    Mqtt_Data_t message;
} Mqtt_Publish_Options_t;

#define PUBLISH_OPTIONS_INITIALIZER {false, false, 0, {0, NULL}, {0, NULL}}

typedef struct
{
    uint32_t count;
    Mqtt_Data_t *pTopics;
    uint8_t *pQos;
} Mqtt_Subscribe_Options_t;

#define SUBSCRIBE_OPTIONS_INITIALIZER {0, NULL, NULL}
#define MQTT_INIT_SUBSCRIBE(opt, cnt, t, q) do{(opt).count=cnt;(opt).pTopics=t;(opt).pQos=q;}while(0)

typedef struct
{
    uint32_t count;
    Mqtt_Data_t *pTopics;
} Mqtt_Unsubscribe_Options_t;

#define UNSUBSCRIBE_OPTIONS_INITIALIZER {0, NULL}
#define MQTT_INIT_UNSUBSCRIBE(opt, cnt, t) do{(opt).count=cnt;(opt).pTopics=t;}while(0)

typedef enum
{
    MQTT_ERR_OK,
    MQTT_ERR_PACKET_ERROR,
    MQTT_ERR_BUFSIZE,
    MQTT_ERR_PARM,
    MQTT_ERR_RET_LEN,
} Mqtt_Error_t;

typedef void * Mqtt_Handle_t;

typedef enum
{
    MQTT_EVENT_NONE,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_PUBLISH,
    MQTT_EVENT_PUBACK,
    MQTT_EVENT_SUBACK,
    MQTT_EVENT_UNSUBACK,
    MQTT_EVENT_PINGRESP,
    MQTT_EVENT_REPLY,
} Mqtt_Event_t;

typedef struct
{
    uint16_t packetId;
    void (*EventCb)(Mqtt_Handle_t, Mqtt_Event_t, void *);
} Mqtt_Instance_t;

extern Mqtt_Handle_t
Mqtt_InitInstance(Mqtt_Instance_t *pInst,
                  void (*pfnEvent)(Mqtt_Handle_t, Mqtt_Event_t, void *));
extern Mqtt_Error_t
Mqtt_Connect(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Connect_Options_t *pOptions);
extern Mqtt_Error_t
Mqtt_Publish(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Publish_Options_t *pOptions);
extern Mqtt_Error_t
Mqtt_Subscribe(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Subscribe_Options_t *pOptions);
extern Mqtt_Error_t
Mqtt_Unsubscribe(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Unsubscribe_Options_t *pOptions);

static inline Mqtt_Error_t Mqtt_Pingreq(Mqtt_Handle_t h, Mqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xC0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return MQTT_ERR_OK;
}
static inline Mqtt_Error_t Mqtt_Disconnect(Mqtt_Handle_t h, Mqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xE0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return MQTT_ERR_OK;
}
extern Mqtt_Error_t Mqtt_Process(Mqtt_Handle_t h, Mqtt_Data_t *pIncoming);

#endif
