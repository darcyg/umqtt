
#ifndef __UMQTT_H__
#define __UMQTT_H__

typedef struct
{
    bool sessionPresent;
    uint8_t returnCode;
} umqtt_Connect_Result_t;

typedef struct
{
    uint16_t len;
    uint8_t *data;
} umqtt_Data_t;

#define UMQTT_INIT_DATA_STR(var, str) do{(var).data=(uint8_t*)(str);(var).len=strlen(str);}while(0)
#define UMQTT_INIT_DATA_STATIC_BUF(v,b) do{(v).data=(b);(v).len=sizeof(b);}while(0)

typedef struct
{
    bool cleanSession;
    bool willRetain;
    uint8_t qos;
    uint16_t keepAlive;
    umqtt_Data_t clientId;
    umqtt_Data_t willTopic;
    umqtt_Data_t willMessage;
    umqtt_Data_t username;
    umqtt_Data_t password;
} umqtt_Connect_Options_t;

#define CONNECT_OPTIONS_INITIALIZER \
{ false, false, 0, 0, \
  { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }}

typedef struct
{
    bool dup;
    bool retain;
    uint8_t qos;
    umqtt_Data_t topic;
    umqtt_Data_t message;
} umqtt_Publish_Options_t;

#define PUBLISH_OPTIONS_INITIALIZER {false, false, 0, {0, NULL}, {0, NULL}}

typedef struct
{
    uint32_t count;
    umqtt_Data_t *pTopics;
    uint8_t *pQos;
} umqtt_Subscribe_Options_t;

#define SUBSCRIBE_OPTIONS_INITIALIZER {0, NULL, NULL}
#define UMQTT_INIT_SUBSCRIBE(opt, cnt, t, q) do{(opt).count=cnt;(opt).pTopics=t;(opt).pQos=q;}while(0)

typedef struct
{
    uint32_t count;
    umqtt_Data_t *pTopics;
} umqtt_Unsubscribe_Options_t;

#define UNSUBSCRIBE_OPTIONS_INITIALIZER {0, NULL}
#define UMQTT_INIT_UNSUBSCRIBE(opt, cnt, t) do{(opt).count=cnt;(opt).pTopics=t;}while(0)

typedef enum
{
    UMQTT_ERR_OK,
    UMQTT_ERR_PACKET_ERROR,
    UMQTT_ERR_BUFSIZE,
    UMQTT_ERR_PARM,
    UMQTT_ERR_RET_LEN,
} umqtt_Error_t;

typedef void * umqtt_Handle_t;

typedef enum
{
    UMQTT_EVENT_NONE,
    UMQTT_EVENT_CONNECTED,
    UMQTT_EVENT_PUBLISH,
    UMQTT_EVENT_PUBACK,
    UMQTT_EVENT_SUBACK,
    UMQTT_EVENT_UNSUBACK,
    UMQTT_EVENT_PINGRESP,
    UMQTT_EVENT_REPLY,
} umqtt_Event_t;

typedef struct
{
    uint16_t packetId;
    void (*EventCb)(umqtt_Handle_t, umqtt_Event_t, void *);
} umqtt_Instance_t;

extern umqtt_Handle_t
umqtt_InitInstance(umqtt_Instance_t *pInst,
                  void (*pfnEvent)(umqtt_Handle_t, umqtt_Event_t, void *));
extern umqtt_Error_t
umqtt_BuildConnect(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Connect_Options_t *pOptions);
extern umqtt_Error_t
umqtt_BuildPublish(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Publish_Options_t *pOptions);
extern umqtt_Error_t
umqtt_BuildSubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Subscribe_Options_t *pOptions);
extern umqtt_Error_t
umqtt_BuildUnsubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Unsubscribe_Options_t *pOptions);

static inline umqtt_Error_t umqtt_BuildPingreq(umqtt_Handle_t h, umqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xC0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return UMQTT_ERR_OK;
}
static inline umqtt_Error_t umqtt_BuildDisconnect(umqtt_Handle_t h, umqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xE0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return UMQTT_ERR_OK;
}
extern umqtt_Error_t umqtt_DecodePacket(umqtt_Handle_t h, umqtt_Data_t *pIncoming);

#endif
