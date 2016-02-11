/******************************************************************************
 * umqtt.h - MQTT packet client for microcontrollers.
 *
 * Copyright (c) 2016, Joseph Kroesche (tronics.kroesche.io)
 * All rights reserved.
 *
 * This software is released under the FreeBSD license, found in the
 * accompanying file LICENSE.txt and at the following URL:
 *      http://www.freebsd.org/copyright/freebsd-license.html
 *
 * This software is provided as-is and without warranty.
 */

#ifndef __UMQTT_H__
#define __UMQTT_H__

/**
 * @addtogroup umqtt_client
 * @{
 */

/**
 * Return codes for umqtt functions.
 */
typedef enum
{
    UMQTT_ERR_OK,           ///< normal return code, no errors
    /// calculated length is returned, this is not an error
    UMQTT_ERR_RET_LEN,
    UMQTT_ERR_PACKET_ERROR, ///< detected error in packet during decoding
    UMQTT_ERR_BUFSIZE,      ///< buffer not big enough to hold packet
    UMQTT_ERR_PARM,         ///< problem with function input parameter
} umqtt_Error_t;

/**
 * Event codes that are passed to the callback function.  See the
 * documentation for umqtt_EventCallback() for more details.
 */
typedef enum
{
    UMQTT_EVENT_NONE,       ///< no event
    UMQTT_EVENT_CONNECTED,  ///< server acknowledged connection
    UMQTT_EVENT_PUBLISH,    ///< a subscribed topic was published to the client
    UMQTT_EVENT_PUBACK,     ///< a published topic was acknowledged by the server
    UMQTT_EVENT_SUBACK,     ///< a subscribe was acknowledged by the server
    UMQTT_EVENT_UNSUBACK,   ///< an unsubscribe was acknowledged by the server
    UMQTT_EVENT_PINGRESP,   ///< a ping was acknowledged by the server
    UMQTT_EVENT_REPLY,      ///< a reply packet is available to send to the server
} umqtt_Event_t;

/**
 * umqtt instance handle, to be passed to all functions.  Obtained
 * from umqtt_InitInstance().
 */
typedef void * umqtt_Handle_t;

/**
 * umqtt instance data structure.  The client should allocate one of
 * these for each MQTT connection (normally only one).  Even though the
 * fields are exposed here, the client should treat this as an opaque
 * type and not directly access any of the fields.
 */
typedef struct
{
    uint16_t packetId;
    void (*EventCb)(umqtt_Handle_t, umqtt_Event_t, void *);
} umqtt_Instance_t;

/**
 * Structure to hold a block of data.
 *
 * MQTT defines most blocks of data as a length field followed by the
 * data payload.  This applies to topic strings, topic payloads, and
 * other packet data that is of variable length.  This structure is used
 * to represent that kind of data as well as the overall MQTT packets
 * that are passed to and from the client.
 */
typedef struct
{
    uint16_t len;   ///< length of the data block
    uint8_t *data;  ///< pointer to the start of the data block
} umqtt_Data_t;

/**
 * A convenience macro for populating an MQTT data block with a string.
 *
 * @param var the umqtt_Data_t variable to initialize
 * @param str the string to load into the variable
 *
 * __Example__
 * ~~~~~~~~.c
 * umqtt_Data_t myTopic;
 * UMQTT_INIT_DATA_STR(myTopic, "myTopicName");
 * ~~~~~~~~
 */
#define UMQTT_INIT_DATA_STR(var, str) do{(var).data=(uint8_t*)(str);(var).len=strlen(str);}while(0)

/**
 * A convenience macro for populating an MQTT data block with a static buffer.
 *
 * @param v the umqtt_Data_t variable to initialize
 * @param b the buffer to load into the variable
 *
 * __Example__
 * ~~~~~~~~.c
 * // buffer for holding encoded packets
 * umqtt_Data_t packetData;
 * uint8_t storageBuf[128];
 * UMQTT_INIT_DATA_STATIC_BUF(packetData, storageBuf);
 * ~~~~~~~~
 */
#define UMQTT_INIT_DATA_STATIC_BUF(v,b) do{(v).data=(b);(v).len=sizeof(b);}while(0)

/**
 * Options structure for CONNECT packet.  The client populates this
 * and passes to umqtt_BuildConnect().  See MQTT specification for a
 * complete explanation of the meaning of the fields.
 */
typedef struct
{
    bool cleanSession;      ///< server should start with clean session
    bool willRetain;        ///< server should retain the will topic, if used
    uint8_t qos;            ///< QoS to be used for the will topic
    uint16_t keepAlive;     ///< time interval for keepalive (ping) packets
    umqtt_Data_t clientId;  ///< data block holding the client identifier string
    umqtt_Data_t willTopic; ///< data block holding the will topic, if used (can be NULL)
    umqtt_Data_t willMessage;///< data block holding the will message (if used)
    umqtt_Data_t username;  ///< data block holding the optional user name
    umqtt_Data_t password;  ///< data block holding the optional password
} umqtt_Connect_Options_t;

/** Convenience macro to initialize connect options to NULL defaults. */
#define CONNECT_OPTIONS_INITIALIZER \
{ false, false, 0, 0, \
  { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }, { 0, NULL }}

/**
 * Structure to hold results of a connection acknowledgment (CONNACK).
 *
 * When umqtt decodes a CONNACK packet from the server, it will notify
 * the client by passing this structure to the callback function.  The client
 * can obtain the results of the connection.
 */
typedef struct
{
    bool sessionPresent;    ///< server already has session state for the client
    uint8_t returnCode;     ///< connect return code, normally 0
} umqtt_Connect_Result_t;

/**
 * Options for a PUBLISH packet.  The client populates this structure
 * and passes it to umqtt_BuildPublish() to create a PUBLISH packet.  See
 * the MQTT specification for a complete explanation of the fields.
 */
typedef struct
{
    bool dup;       ///< DUP flag, true if this is a duplicate PUBLISH
    bool retain;    ///< server should retain the published topic
    uint8_t qos;    ///< QoS for this publish topic (0-2)
    umqtt_Data_t topic;     ///< data block holding the topic to publish
    umqtt_Data_t message;   ///< data block holding the optional topic payload
} umqtt_Publish_Options_t;

/** Convenience macro to initialize publish options to NULL defaults. */
#define PUBLISH_OPTIONS_INITIALIZER {false, false, 0, {0, NULL}, {0, NULL}}

/**
 * Options for a SUBSCRIBE packet.  The client populates this structure
 * and passes it to umqtt_BuildSubscribe() to create a SUBSCRIBE packet.  See
 * the MQTT specification for a complete explanation of the fields.
 */
typedef struct
{
    uint32_t count;         ///< count of the number of topics/QoS in the topic array
    umqtt_Data_t *pTopics;  ///< array of data blocks holding the topics to subscribe
    uint8_t *pQos;          ///< array of QoS value (one for each topic)
} umqtt_Subscribe_Options_t;

/** Convenience macro to initialize subscribe options to NULL defaults. */
#define SUBSCRIBE_OPTIONS_INITIALIZER {0, NULL, NULL}

/**
 * Convenience macro to initialize umqtt_Subscribe_Options_t structure.
 *
 * @param opt subscribe options data structure
 * @param cnt count of topics/QoS in the subscribe
 * @param t array of topic data blocks (at least 1)
 * @param q array of QoS value (must match topics)
 */
#define UMQTT_INIT_SUBSCRIBE(opt, cnt, t, q) do{(opt).count=cnt;(opt).pTopics=t;(opt).pQos=q;}while(0)

/**
 * Options for an UNSUBSCRIBE packet.  The client populates this structure
 * and passes it to umqtt_BuildUnsubscribe() to create an UNSUBSCRIBE packet.
 * See the MQTT specification for a complete explanation of the fields.
 */
typedef struct
{
    uint32_t count;         ///< count of topics in the topic array
    umqtt_Data_t *pTopics;  ///< array of topic data blocks, list to unsubscribe
} umqtt_Unsubscribe_Options_t;

/** Convenience macro to initialize subscribe options to NULL defaults. */
#define UNSUBSCRIBE_OPTIONS_INITIALIZER {0, NULL}

/**
 * Convenience macro to initialize umqtt_Unsubscribe_Options_t structure.
 *
 * @param opt unsubscribe options data structure
 * @param cnt count of topics in the unsubscribe
 * @param t array of topic data blocks (at least 1)
 */
#define UMQTT_INIT_UNSUBSCRIBE(opt, cnt, t) do{(opt).count=cnt;(opt).pTopics=t;}while(0)

/**
 * Build an MQTT PINGREQ packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded PINGREQ packet
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * This function is used to create a PINGREQ packet.  The client must send
 * a PINGREQ to the server at an interval no longer than the keep-alive
 * time that was specified in the connect options.  The caller must allocate
 * the buffer space needed to hold the packet, which is always 2 bytes for
 * this type of packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * @note This function is simple and marked as inline to reduce the
 * overhead of a function call for such a simple operation.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[2];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the PINGREQ packet
 * umqtt_Error_t err;
 * err = umqtt_BuildPingreq(h, &outbuf);
 * if (err == UMQTT_ERR_OK)
 * {
 *     // send packet using your network method
 *     // packet data is in outbuf.data, length is in outbuf.len
 *     mynet_send_function(outbuf.data, outbuf.len);
 * }
 * else
 * {
 *     // handle build error
 * }
 * ~~~~~~~~
 */
static inline umqtt_Error_t umqtt_BuildPingreq(umqtt_Handle_t h, umqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xC0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return UMQTT_ERR_OK;
}

/**
 * Initializer for PINGREQ packet
 *
 * This is a convenience macro that can be (optionally) used to create
 * a PINGREQ packet.  Since the PINGREQ is always the same and is used
 * periodically, it makes sense to create a const array to hold it.  This
 * can be used instead of umqtt_BuildPingreq().
 *
 * __Example__
 * ~~~~~~~~.c
 * // Create a PINGREQ packet in const memory (flash)
 * const uint8_t pingreqPacket[2] = PINGREQ_PACKET_INITIALIZER;
 *
 * ...
 *
 * // send PINGREQ packet using network method
 * mynet_send_function(pingreqPacket, sizeof(pingreqPacket));
 * ~~~~~~~~
 */
#define PINGREQ_PACKET_INITIALIZER { 0xC0, 0 }

/**
 * Build an MQTT DISCONNECT packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded DISCONNECT packet
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * This function is used to create a DISCONNECT packet.  The client must send
 * a DISCONNECT to the server in order to cleanly terminate the MQTT
 * connection.  The caller must allocate the buffer space needed to hold
 * the packet, which is always 2 bytes for this type of packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * @note This function is simple and marked as inline to reduce the
 * overhead of a function call for such a simple operation.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[2];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the DISCONNECT packet
 * umqtt_Error_t err;
 * err = umqtt_BuildDisconnect(h, &outbuf);
 * if (err == UMQTT_ERR_OK)
 * {
 *     // send packet using your network method
 *     // packet data is in outbuf.data, length is in outbuf.len
 *     mynet_send_function(outbuf.data, outbuf.len);
 * }
 * else
 * {
 *     // handle build error
 * }
 * ~~~~~~~~
 */
static inline umqtt_Error_t umqtt_BuildDisconnect(umqtt_Handle_t h, umqtt_Data_t *pOutbuf)
{
    pOutbuf->data[0] = 0xE0;
    pOutbuf->data[1] = 0;
    pOutbuf->len = 2;
    return UMQTT_ERR_OK;
}

/**
 * Initializer for DISCONNECT packet
 *
 * This is a convenience macro that can be (optionally) used to create
 * a DISCONNECT packet.  This can be used instead of umqtt_BuildDisconnect().
 *
 * __Example__
 * ~~~~~~~~.c
 * // Create a DISCONNECT packet in const memory (flash)
 * const uint8_t disconnectPacket[2] = DISCONNECT_PACKET_INITIALIZER;
 *
 * ...
 *
 * // send DISCONNECT packet using network method
 * mynet_send_function(disconnectPacket, sizeof(disconnectPacket));
 * ~~~~~~~~
 */
#define DISCONNECT_PACKET_INITIALIZER { 0xE0, 0 }

/**
 * Client callback for event notifications.
 *
 * @param h umatt instance handle associated with the event
 * @param event notification event
 * @param pArg data associated with the event (see table below)
 *
 * The client must implement a callback function to receive notifications.
 * The callback function can be any name (the name here is just an example
 * for purposes of documentation) and is passed to umqtt using the
 * umqtt_InitInstance() function.
 *
 * The notification callback function is always called from the
 * umqtt_DecodePacket() function.
 *
 * Some event have associated data and others do not.  The following table
 * shows the data associated with each type of event.  See also
 * umqtt_Event_t for a description of each event type.
 *
 * Event                 | Data (pArg)
 * ----------------------|------------
 * UMQTT_EVENT_CONNECTED | umqtt_Connect_Result_t (session present flag, connect return code
 * UMQTT_EVENT_PUBLISH   | umqtt_Publish_Options_t (publish flags, QoS, topic and payload
 * UMQTT_EVENT_PUBACK    | NULL
 * UMQTT_EVENT_SUBACK    | umqtt_Data_t (suback payload as uint8_t array)
 * UMQTT_EVENT_UNSUBACK  | NULL
 * UMQTT_EVENT_PINGRESP  | NULL
 * UMQTT_EVENT_REPLY     | umqtt_Data_t containing MQTT reply packet (see notes)
 *
 * If the client needs to retain the callback data beyond the scope of the
 * callback function, it must make copies of the data.  Once the callback
 * function returns, the data that was passed by _pArg_ does not persist.
 */
void umqtt_EventCallback(umqtt_Handle_t h, umqtt_Event_t event, void *pArg);

/**
 * @}
 */

#ifdef __cplusplus
extern "C" {
#endif

extern umqtt_Handle_t
umqtt_InitInstance(umqtt_Instance_t *pInst,
                  void (*pfnEvent)(umqtt_Handle_t, umqtt_Event_t, void *));
extern umqtt_Error_t umqtt_BuildConnect(umqtt_Handle_t h, umqtt_Data_t *pOutBuf,
                                        const umqtt_Connect_Options_t *pOptions);
extern umqtt_Error_t umqtt_BuildPublish(umqtt_Handle_t h, umqtt_Data_t *pOutBuf,
                                        const umqtt_Publish_Options_t *pOptions);
extern umqtt_Error_t
umqtt_BuildSubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Subscribe_Options_t *pOptions);
extern umqtt_Error_t
umqtt_BuildUnsubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Unsubscribe_Options_t *pOptions);

extern umqtt_Error_t umqtt_DecodePacket(umqtt_Handle_t h, umqtt_Data_t *pIncoming);

#ifdef __cplusplus
extern }
#endif

#endif
