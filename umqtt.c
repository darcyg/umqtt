/******************************************************************************
 * umqtt.c - MQTT packet client for microcontrollers.
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "umqtt.h"

/**
 * @addtogroup umqtt_client MQTT client packet processing for microcontrollers
 *
 * This library provides a set of function for processing MQTT client
 * packets.  It is intended primarily for use with microcontrollers.
 *
 * The design goals:
 * - written to be portable, not targeting any specific platform
 * - completely isolate the packet processing from packet transmission
 *   and reception
 * - not dependent on any particular RTOS, does not require RTOS but could
 *   be used with on
 * - not tied to any particular networking hardware or stack
 * - have well documented, consistent and understandable API
 *
 * The library uses instance handles so the same code can be used for
 * multiple client connections to one or more MQTT servers.
 *
 * __Sending__
 *
 * - Populate options structure as needed, depending on type of packet
 *   (CONNECT, SUBSCRIBE, etc)
 * - Initialize a data block, allocating buffer space as needed (can be
 *   static or dynamic).  The data block will hold the outgoing packet.
 * - Call packet-building function
 * - Check return error code
 * - Pass returned data block (buffer and length) to your network transmission
 *   function.
 *
 * ~~~~~~~~.c
 * // simple example to subscribe to a single topic with QoS 0
 *
 * ~~~~~~~~
 *
 * @{
 */

// TODO: const on parameters

/*
 * MQTT packet types
 */
#define UMQTT_TYPE_CONNECT 1
#define UMQTT_TYPE_CONNACK 2
#define UMQTT_TYPE_PUBLISH 3
#define UMQTT_TYPE_PUBACK 4
#define UMQTT_TYPE_SUBSCRIBE 8
#define UMQTT_TYPE_SUBACK 9
#define UMQTT_TYPE_UNSUBSCRIBE 10
#define UMQTT_TYPE_UNSUBACK 11
#define UMQTT_TYPE_PINGREQ 12
#define UMQTT_TYPE_PINGRESP 13
#define UMQTT_TYPE_DISCONNECT 14

/*
 * MQTT fixed header flags
 */
#define UMQTT_FLAG_DUP 0x08
#define UMQTT_FLAG_RETAIN 0x01
#define UMQTT_FLAG_QOS 0x06
#define UMQTT_FLAG_QOS_SHIFT 1

/*
 * flags used in connect packet (different from fixed header flags)
 */
#define UMQTT_CONNECT_FLAG_USER 0x80
#define UMQTT_CONNECT_FLAG_PASS 0x40
#define UMQTT_CONNECT_FLAG_WILL_RETAIN 0x20
#define UMQTT_CONNECT_FLAG_WILL_QOS 0x18
#define UMQTT_CONNECT_FLAG_WILL 0x04
#define UMQTT_CONNECT_FLAG_CLEAN 0x02
#define UMQTT_CONNECT_FLAG_QOS_SHIFT 3

// error handling convenience
#define RETURN_IF_ERR(c,e) do{if(c){return (e);}}while(0)

/** @internal
 *
 * Encode length into MQTT remaining length format
 *
 * @param length length to encode
 * @param pEncodeBuf buffer to store encoded length
 *
 * @return the count of bytes that hold the length
 *
 * Does not validate parameters.  Assumes length is valid
 * and buffer is large enough to hold encoded length.
 */
static uint32_t
umqtt_EncodeLength(uint32_t length, uint8_t *pEncodeBuf)
{
    uint8_t encByte;
    uint32_t idx = 0;
    do
    {
        encByte = length % 128;
        length /= 128;
        if (length > 0)
        {
            encByte |= 0x80;
        }
        pEncodeBuf[idx] = encByte;
        ++idx;;
    } while (length > 0);
    return idx; // return count of bytes
}

/** @internal
 *
 * Decode remaining length field from MQTT packet.
 *
 * @param pLength storage to location of decoded length
 * @param pEncodedLength buffer holding the encoded length field
 *
 * @return count of bytes of the encoded length
 *
 * The caller supplies storage for the decoded length through the
 * _pLength_ parameter.  The number of bytes used to hold the encoded
 * length in the packet is returned to the caller.  No parameter
 * validation is performed.
 */
static uint32_t
umqtt_DecodeLength(uint32_t *pLength, const uint8_t *pEncodedLength)
{
    uint8_t encByte;
    uint32_t count = 0;
    uint32_t length = 0;
    uint32_t mult = 1;
    do
    {
        encByte = pEncodedLength[count];
        length += (encByte & 0x7F) * mult;
        ++count;
        mult *= 128;
    } while (encByte & 0x80);

    *pLength = length;
    return count;
}

/** @internal
 *
 * Encode a data block into an MQTT packet
 *
 * @param pDat pointer to mqtt data block or string to be encoded
 * into a packet
 * @param pBuf pointer to a buffer where the data block will be encoded
 *
 * @return count of bytes that were encoded
 *
 * This function assumes that the buffer pointed at by _pBuf_ is large
 * enough to hold the encoded data block.
 */
static uint32_t
umqtt_EncodeData(umqtt_Data_t *pDat, uint8_t *pBuf)
{
    *pBuf++ = pDat->len >> 8;
    *pBuf++ = pDat->len & 0xFF;
    memcpy(pBuf, pDat->data, pDat->len);
    return pDat->len + 2;
}

/**
 * Build an MQTT CONNECT packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded CONNECT packet
 * @param pOptions points at structure holding the connect options
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * The connect options found in the argument _pOptions_ will be encoded into
 * an MQTT CONNECT packet and stored in the buffer specified by _pOutBuf_.
 * The caller must allocate the buffer space needed to hold the packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * __Length calculation feature__
 *
 * By calling this function with a NULL instance handle (_h_ is NULL), then
 * the required length of the packet will be calculated but no packet data
 * is written to the output buffer.  The caller can use this feature to
 * discover how much space is required for the CONNECT packet before
 * allocating buffer space.  The required space will be returned in the
 * length field of the _pOutBuf_ output buffer argument.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // create and populate basic connect options
 * umqtt_Connection_Options_t opts = CONNECT_OPTIONS_INITIALIZER;
 * opts.keepAlive = 30; // 30 seconds
 * UMQTT_INIT_DATA_STR(opts.clientId, "myClientId"); // client ID
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[BUF_SIZE];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the CONNECT packet
 * umqtt_Error_t err;
 * err = umqtt_BuildConnect(h, &outbuf, &opts);
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
umqtt_Error_t
umqtt_BuildConnect(umqtt_Handle_t h, umqtt_Data_t *pOutBuf,
                   const umqtt_Connect_Options_t *pOptions)
{
    uint8_t connectFlags = 0;
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), UMQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    RETURN_IF_ERR(pOptions->clientId.len == 0, UMQTT_ERR_PARM);
    uint16_t remainingLength = 10 // variable header
                             + 2 + pOptions->clientId.len;
    if (pOptions->willTopic.len)
    {
        connectFlags |= UMQTT_CONNECT_FLAG_WILL;
        remainingLength += 2 + pOptions->willTopic.len;
        // if there is a will topic there should be a will message
        RETURN_IF_ERR(pOptions->willMessage.len == 0, UMQTT_ERR_PARM);
        remainingLength += 2 + pOptions->willMessage.len;
    }
    if (pOptions->username.len)
    {
        connectFlags |= UMQTT_CONNECT_FLAG_USER;
        remainingLength += 2 + pOptions->username.len;
    }
    if (pOptions->password.len)
    {
        connectFlags |= UMQTT_CONNECT_FLAG_PASS;
        remainingLength += 2 + pOptions->password.len;
    }

    // if handle is NULL but other parameters are okay then caller
    // is asking for computed length of packet and no other action
    if (h == NULL)
    {
        // use a dummy buf to encode the remaining length in order
        // to find out how many bytes for the length field
        uint8_t encBuf[4];
        uint32_t byteCount = umqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return UMQTT_ERR_RET_LEN;
    }

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 12), UMQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = umqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provided buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, UMQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = UMQTT_TYPE_CONNECT << 4;
    idx = 1 + lenSize;

    // encode protocol name
    uint8_t protocolNameStr[4] = "MQTT";
    umqtt_Data_t protocolName = { 4, protocolNameStr };
    idx += umqtt_EncodeData(&protocolName, &buf[idx]);

    // protocol level, connect flags and keepalive
    connectFlags |= pOptions->willRetain ? UMQTT_CONNECT_FLAG_WILL_RETAIN : 0;
    connectFlags |= pOptions->cleanSession ? UMQTT_CONNECT_FLAG_CLEAN : 0;
    connectFlags |= (pOptions->qos << UMQTT_CONNECT_FLAG_QOS_SHIFT) & UMQTT_CONNECT_FLAG_WILL_QOS;
    buf[idx++] = 4;
    buf[idx++] = connectFlags;
    buf[idx++] = pOptions->keepAlive >> 8;
    buf[idx++] = pOptions->keepAlive & 0xFF;

    // client id
    RETURN_IF_ERR(pOptions->clientId.data == NULL, UMQTT_ERR_PARM);
    idx += umqtt_EncodeData(&pOptions->clientId, &buf[idx]);

    // will topic and message
    if (pOptions->willTopic.len)
    {
        // check data pointers
        RETURN_IF_ERR(pOptions->willTopic.data == NULL, UMQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->willMessage.data == NULL, UMQTT_ERR_PARM);
        idx += umqtt_EncodeData(&pOptions->willTopic, &buf[idx]);
        idx += umqtt_EncodeData(&pOptions->willMessage, &buf[idx]);
    }

    // username
    if (pOptions->username.len)
    {
        RETURN_IF_ERR(pOptions->username.data == NULL, UMQTT_ERR_PARM);
        idx += umqtt_EncodeData(&pOptions->username, &buf[idx]);
    }

    // password
    if (pOptions->password.len)
    {
        RETURN_IF_ERR(pOptions->password.data == NULL, UMQTT_ERR_PARM);
        idx += umqtt_EncodeData(&pOptions->password, &buf[idx]);
    }

    return UMQTT_ERR_OK;
}

/**
 * Build an MQTT PUBLISH packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded PUBLISH packet
 * @param pOptions points at structure holding the publish options
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * The publish options found in the argument _pOptions_ will be encoded into
 * an MQTT PUBLISH packet and stored in the buffer specified by _pOutBuf_.
 * The caller must allocate the buffer space needed to hold the packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * __Length calculation feature__
 *
 * By calling this function with a NULL instance handle (_h_ is NULL), then
 * the required length of the packet will be calculated but no packet data
 * is written to the output buffer.  The caller can use this feature to
 * discover how much space is required for the PUBLISH packet before
 * allocating buffer space.  The required space will be returned in the
 * length field of the _pOutBuf_ output buffer argument.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // create default publish options
 * umqtt_Publish_Options_t opts = PUBLISH_OPTIONS_INITIALIZER;
 * // set retain flag and set topic and message
 * opts.retain = true;
 * UMQTT_INIT_DATA_STR(opts.topic, "myTopic");
 * UMQTT_INIT_DATA_STR(opts.message, "myMessage");
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[BUF_SIZE];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the PUBLISH packet
 * umqtt_Error_t err;
 * err = umqtt_BuildPublish(h, &outbuf, &opts);
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
umqtt_Error_t
umqtt_BuildPublish(umqtt_Handle_t h, umqtt_Data_t *pOutBuf,
                   const umqtt_Publish_Options_t *pOptions)
{
    uint8_t flags = 0;
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->topic.len == 0, UMQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    uint16_t remainingLength = (pOptions->qos ? 2 : 0) // packet id
                             + 2 + pOptions->topic.len;
    if (pOptions->message.len)
    {
        remainingLength += 2 + pOptions->message.len;
    }

    // if handle is NULL but other parameters are okay then caller
    // is asking for computed length of packet and no other action
    if (h == NULL)
    {
        // use a dummy buf to encode the remaining length in order
        // to find out how many bytes for the length field
        uint8_t encBuf[4];
        uint32_t byteCount = umqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return UMQTT_ERR_RET_LEN;
    }

    // get instance data from handle
    umqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 5), UMQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = umqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, UMQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = UMQTT_TYPE_PUBLISH << 4;
    idx = 1 + lenSize;

    // header flags
    flags |= pOptions->dup ? UMQTT_FLAG_DUP : 0;
    flags |= pOptions->retain ? UMQTT_FLAG_RETAIN : 0;
    flags |= (pOptions->qos << UMQTT_FLAG_QOS_SHIFT) & UMQTT_FLAG_QOS;
    buf[0] |= flags;

    // topic name
    RETURN_IF_ERR(pOptions->topic.data == NULL, UMQTT_ERR_PARM);
    idx += umqtt_EncodeData(&pOptions->topic, &buf[idx]);

    // if QOS then also need packet ID
    if (pOptions->qos != 0)
    {
        ++pInst->packetId;
        if (pInst->packetId == 0)
        {
            pInst->packetId = 1;
        }
        buf[idx++] = pInst->packetId >> 8;
        buf[idx++] = pInst->packetId & 0xFF;
    }

    // payload message
    if (pOptions->message.len)
    {
        // check data pointers
        RETURN_IF_ERR(pOptions->message.data == NULL, UMQTT_ERR_PARM);
        idx += umqtt_EncodeData(&pOptions->message, &buf[idx]);
    }

    return UMQTT_ERR_OK;
}

/**
 * Build an MQTT SUBSCRIBE packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded SUBSCRIBE packet
 * @param pOptions points at structure holding the subscribe options
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * The subscribe options found in the argument _pOptions_ will be encoded into
 * an MQTT SUBSCRIBE packet and stored in the buffer specified by _pOutBuf_.
 * The caller must allocate the buffer space needed to hold the packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * __Length calculation feature__
 *
 * By calling this function with a NULL instance handle (_h_ is NULL), then
 * the required length of the packet will be calculated but no packet data
 * is written to the output buffer.  The caller can use this feature to
 * discover how much space is required for the SUBSCRIBE packet before
 * allocating buffer space.  The required space will be returned in the
 * length field of the _pOutBuf_ output buffer argument.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // create default subscribe options
 * umqtt_Subscribe_Options_t opts = SUBSCRIBE_OPTIONS_INITIALIZER;
 *
 * // subscribe needs an array of topics and array of QoS values
 * // create array for one topic and qos
 * umqtt_Data_t topics[1];
 * uint8_t qoss[1];
 *
 * // set topic and qos, then load into options structure
 * UMQTT_INIT_DATA_STR(topics[0], "mySubscribeTopic");
 * qoss[0] = 0; // QoS level 0
 * opts.pTopics = topics;
 * opts.pQos = qoss;
 * opts.count = 1; // number of topics in array
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[BUF_SIZE];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the SUBSCRIBE packet
 * umqtt_Error_t err;
 * err = umqtt_BuildSubscribe(h, &outbuf, &opts);
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
umqtt_Error_t
umqtt_BuildSubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Subscribe_Options_t *pOptions)
{
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pTopics == NULL, UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pQos == NULL, UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->count == 0, UMQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    uint16_t remainingLength = 2; // packet id
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        RETURN_IF_ERR(pOptions->pTopics[i].len == 0, UMQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pTopics[i].data == NULL, UMQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pQos[i] > 2, UMQTT_ERR_PARM);
        remainingLength += 2 + 1; // topic length field plus qos
        remainingLength += pOptions->pTopics[i].len;
    }

    // if handle is NULL but other parameters are okay then caller
    // is asking for computed length of packet and no other action
    if (h == NULL)
    {
        // use a dummy buf to encode the remaining length in order
        // to find out how many bytes for the length field
        uint8_t encBuf[4];
        uint32_t byteCount = umqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return UMQTT_ERR_RET_LEN;
    }

    // get instance data from handle
    umqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 8), UMQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = umqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, UMQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = (UMQTT_TYPE_SUBSCRIBE << 4) | 0x02;
    idx = 1 + lenSize;

    // packet id
    ++pInst->packetId;
    if (pInst->packetId == 0)
    {
        pInst->packetId = 1;
    }
    buf[idx++] = pInst->packetId >> 8;
    buf[idx++] = pInst->packetId & 0xFF;

    // encode each topic in topic array provided by caller
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        idx += umqtt_EncodeData(&pOptions->pTopics[i], &buf[idx]);
        buf[idx++] = pOptions->pQos[i];
    }

    return UMQTT_ERR_OK;
}

/**
 * Build an MQTT UNSUBSCRIBE packet.
 *
 * @param h the umqtt instance handle
 * @param pOutBuf points at the data buffer to hold the encoded UNSUBSCRIBE packet
 * @param pOptions points at structure holding the unsubscribe options
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * The subscribe options found in the argument _pOptions_ will be encoded into
 * an MQTT UNSUBSCRIBE packet and stored in the buffer specified by _pOutBuf_.
 * The caller must allocate the buffer space needed to hold the packet.
 *
 * The encoded packet is returned to the caller through the output buffer
 * _pOutBuf_.  The data will be written to ->data and the length will be
 * written to the ->len field of the caller-supplied _pOutBuf_ output buffer
 * structure.
 *
 * __Length calculation feature__
 *
 * By calling this function with a NULL instance handle (_h_ is NULL), then
 * the required length of the packet will be calculated but no packet data
 * is written to the output buffer.  The caller can use this feature to
 * discover how much space is required for the UNSUBSCRIBE packet before
 * allocating buffer space.  The required space will be returned in the
 * length field of the _pOutBuf_ output buffer argument.
 *
 * __Example__
 *
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // create default unsubscribe options
 * umqtt_Unsubscribe_Options_t opts = UNSUBSCRIBE_OPTIONS_INITIALIZER;
 *
 * // unsubscribe needs an array of topics
 * // create array of two topics
 * umqtt_Data_t topics[2];
 *
 * // set two topics, then load into options structure
 * UMQTT_INIT_DATA_STR(topics[0], "myUnsubscribeTopic1");
 * UMQTT_INIT_DATA_STR(topics[1], "myUnsubscribeTopic2");
 * opts.pTopics = topics;
 * opts.count = 2; // number of topics in array
 *
 * // buffer to store output packet
 * static uint8_t pktbuf[BUF_SIZE];
 * umqtt_Data_t outbuf;
 * UMATT_INIT_DATA_STATIC_BUF(outbuf, pktbuf);
 *
 * // build the UNSUBSCRIBE packet
 * umqtt_Error_t err;
 * err = umqtt_BuildUnsubscribe(h, &outbuf, &opts);
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
umqtt_Error_t
umqtt_BuildUnsubscribe(umqtt_Handle_t h, umqtt_Data_t *pOutBuf, umqtt_Unsubscribe_Options_t *pOptions)
{
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pTopics == NULL, UMQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->count == 0, UMQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    uint16_t remainingLength = 2; // packet id
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        RETURN_IF_ERR(pOptions->pTopics[i].len == 0, UMQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pTopics[i].data == NULL, UMQTT_ERR_PARM);
        remainingLength += 2; // topic length field
        remainingLength += pOptions->pTopics[i].len;
    }

    // if handle is NULL but other parameters are okay then caller
    // is asking for computed length of packet and no other action
    if (h == NULL)
    {
        // use a dummy buf to encode the remaining length in order
        // to find out how many bytes for the length field
        uint8_t encBuf[4];
        uint32_t byteCount = umqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return UMQTT_ERR_RET_LEN;
    }

    // get instance data from handle
    umqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 7), UMQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = umqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, UMQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = (UMQTT_TYPE_UNSUBSCRIBE << 4) | 0x02;
    idx = 1 + lenSize;

    // packet id
    ++pInst->packetId;
    if (pInst->packetId == 0)
    {
        pInst->packetId = 1;
    }
    buf[idx++] = pInst->packetId >> 8;
    buf[idx++] = pInst->packetId & 0xFF;

    // encode each topic in topic array provided by caller
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        idx += umqtt_EncodeData(&pOptions->pTopics[i], &buf[idx]);
    }

    return UMQTT_ERR_OK;
}

/**
 * Decode incoming MQTT packet.
 *
 * @param h the umqtt instance handle
 * @param pIncoming data structure holding the MQTT packet to decode
 *
 * @return UMQTT_ERR_OK if successful, or an error code if an error occurred
 *
 * This function is used to decode incoming MQTT packets from the server.
 * The caller passes the buffer containing the packet through the data
 * structure _pIncoming_.  The packet will be decoded and the appropriate
 * decoded values will be passed back to the caller via the event callback
 * function (provided to umqtt_InitInstance()).  By processing the event data
 * in the callback function, the application can act on MQTT events, for
 * example receiving a new message for a subscribed topic.
 *
 * See the documentation for umqtt_EventCallback() for more details about
 * the callback function.
 *
 * This function performs some sanity checks on the packet to make sure it
 * is self-consistent as much as can be determined.  It also makes sure that
 * the length of the packet that is passed in is consistent with the
 * decoded MQTT length field in the packet.  If any problems are detected
 * with the packet, it returns UMQTT_ERR_PACKET_ERROR.
 *
 * __Example__
 * ~~~~~~~~.c
 * umqtt_Handle_t h; // previously acquired instance handle
 *
 * // umqtt event handler - will be called by umqtt_DecodePacket()
 * // when event occur (such as published topic)
 * void myEventCallback(umqtt_Handle_t h, umqtt_Event_t event)
 * {
 *     // process events that result from decoding packets
 *     switch (event)
 *     {
 *         // ...
 *     }
 * }
 *
 * // data structure to hold packet info
 * umqtt_Data_t incomingPkt;
 *
 * // assuming a network function to receive packets
 * mynet_receive_function(&incomingPkt.data, &incomingPkt.len);
 *
 * umqtt_Error_t err = umqtt_DecodePacket(h, &incomingPkt);
 * if (err != UMQTT_ERR_OK)
 * {
 *     // handle error
 * }
 * ~~~~~~~~
 */
umqtt_Error_t
umqtt_DecodePacket(umqtt_Handle_t h, umqtt_Data_t *pIncoming)
{
    umqtt_Error_t err = UMQTT_ERR_OK;

    // basic parameter check
    if ((h == NULL) || (pIncoming == NULL))
    {
        return UMQTT_ERR_PARM;
    }

    // get instance data from handle
    umqtt_Instance_t *pInst = h;

    // start processing the packet if it contains data
    if (pIncoming->len)
    {
        // extract the packet type, flags and length
        uint8_t *pData = pIncoming->data;
        uint8_t type = pData[0] >> 4;
        uint8_t flags = pData[0] & 0x0F;
        uint32_t remainingLen;
        uint32_t lenCount = umqtt_DecodeLength(&remainingLen, &pData[1]);
        // make sure MQTT packet length is consistent with
        // the supplied network packet
        if ((remainingLen + 1 + lenCount) != pIncoming->len)
        {
            return UMQTT_ERR_PACKET_ERROR;
        }

        // process the packet type
        // only client related - not implementing server
        switch (type)
        {
            // CONNACK - pass ack info to callback
            case UMQTT_TYPE_CONNACK:
            {
                if (pInst->EventCb)
                {
                    umqtt_Connect_Result_t result;
                    result.sessionPresent = pData[2] & 1 ? true : false;
                    result.returnCode = pData[3];
                    pInst->EventCb(h, UMQTT_EVENT_CONNECTED, &result);
                }
                break;
            }

            // PUBLISH - extract published topic, payload and options
            case UMQTT_TYPE_PUBLISH:
            {
                umqtt_Publish_Options_t pubdata;
                uint8_t pktId[2];

                // make sure there is a callback function
                if (pInst->EventCb)
                {
                    // extract publish options
                    pubdata.dup = flags & UMQTT_FLAG_DUP ? true : false;
                    pubdata.retain = flags & UMQTT_FLAG_RETAIN ? true : false;
                    pubdata.qos = (flags & UMQTT_FLAG_QOS) >> UMQTT_FLAG_QOS_SHIFT;
                    RETURN_IF_ERR(pubdata.qos > 2, UMQTT_ERR_PACKET_ERROR);

                    // find the topic length and value
                    // make sure remaining packet length is long enough
                    uint32_t idx = 1 + lenCount;
                    uint16_t topicLen = (pData[idx] << 8) + pData[idx + 1];
                    idx += 2;
                    RETURN_IF_ERR((topicLen + 2) > remainingLen, UMQTT_ERR_PACKET_ERROR);

                    // extract the topic length and buf pointer into the
                    // options struct for the callback
                    pubdata.topic.len = topicLen;
                    pubdata.topic.data = &pData[idx];
                    remainingLen -= topicLen + 2;
                    idx += topicLen;

                    // for non-0 QoS, extract the packet id
                    if (pubdata.qos != 0)
                    {
                        if (remainingLen >= 2)
                        {
                            pktId[0] = pData[idx++];
                            pktId[1] = pData[idx++];
                            remainingLen -= 2;
                        }
                        else
                        {
                            return UMQTT_ERR_PACKET_ERROR;
                        }
                    }

                    // continue extracting if there is a topic payload
                    if (remainingLen != 0)
                    {
                        uint16_t msgLen = (pData[idx] << 8) + pData[idx + 1];
                        idx += 2;
                        RETURN_IF_ERR((msgLen + 2) > remainingLen, UMQTT_ERR_PACKET_ERROR);
                        pubdata.message.len = msgLen;
                        pubdata.message.data = &pData[idx];
                        remainingLen -= msgLen + 2;
                    }
                    // check remaining length now.  it should be 0 since all
                    // info has been extracted from the packet
                    RETURN_IF_ERR(remainingLen != 0, UMQTT_ERR_PACKET_ERROR);

                    // callback to provide the publish info to the app
                    pInst->EventCb(h, UMQTT_EVENT_PUBLISH, &pubdata);

                    // if QoS is non-0, prepare a reply packet and
                    // notify through the callback
                    // (note this only works for QoS 1 right now
                    if (pubdata.qos != 0)
                    {
                        umqtt_Data_t puback;
                        uint8_t pubackdat[4];
                        pubackdat[0] = 0x40;
                        pubackdat[1] = 2;
                        pubackdat[2] = pktId[0];
                        pubackdat[3] = pktId[1];
                        puback.len = 4;
                        puback.data = pubackdat;
                        pInst->EventCb(h, UMQTT_EVENT_REPLY, &puback);
                    }
                }

                break;
            }

            // PUBACK - server is acking the client publish, notify client
            case UMQTT_TYPE_PUBACK:
            {
                if (pInst->EventCb)
                {
                    // TODO: something to verify packet id
                    pInst->EventCb(h, UMQTT_EVENT_PUBACK, NULL);
                }
                break;
            }

            // SUBACK - server is acking the client subscribe,
            // notify client
            case UMQTT_TYPE_SUBACK:
            {
                // make sure packet length makes sense
                RETURN_IF_ERR(remainingLen < 3, UMQTT_ERR_PACKET_ERROR);
                if (pInst->EventCb)
                {
                    umqtt_Data_t subackData;
                    // TODO: something to verify packet id
                    // pass suback payload back to client
                    subackData.len = remainingLen - 2;
                    subackData.data = &pData[4];
                    pInst->EventCb(h, UMQTT_EVENT_SUBACK, &subackData);
                }
                break;
            }

            // UNSUBACK - notify client
            case UMQTT_TYPE_UNSUBACK:
            {
                if (pInst->EventCb)
                {
                    // TODO: something to verify packet id
                    pInst->EventCb(h, UMQTT_EVENT_UNSUBACK, NULL);
                }
                break;
            }

            // PINGRESP - notify client
            case UMQTT_TYPE_PINGRESP:
            {
                if (pInst->EventCb)
                {
                    pInst->EventCb(h, UMQTT_EVENT_PINGRESP, NULL);
                }
                break;
            }

            // unexpected packet type is an error
            default:
            {
                err = UMQTT_ERR_PACKET_ERROR;
                break;
            }
        }
        return err;
    }

    // packet length 0 is an error
    else
    {
        return UMQTT_ERR_PACKET_ERROR;
    }
}

/**
 * Initialize the umqtt instance structure.
 *
 * @param pInst instance data structure that is provided by the caller
 * @param pfnEvent callback function used to notify client about
 * MQTT events
 *
 * @return _umqtt_ instance handle that should be used for all other
 * function calls, or NULL if there is an error.
 *
 * The client uses this function to initialize the instance structure before
 * using any other _umqtt_ functions.  The client allocates the space needed
 * for the structure.
 *
 * __Example__
 * ~~~~~~~~.c
 * umqtt_Instance_t theInstance;
 * umqtt_Handle_t h;
 * void myEventCallback(umqtt_Handle_t h, umqtt_Event_t event);
 *
 * h = umqtt_InitInstance(&theInstance, myEventCallback);
 * if (h == NULL)
 * {
 *     // handle error
 * }
 * ~~~~~~~~
 */
umqtt_Handle_t
umqtt_InitInstance(umqtt_Instance_t *pInst, void (*pfnEvent)(umqtt_Handle_t, umqtt_Event_t, void *))
{
    if (pInst == NULL)
    {
        return NULL;
    }
    pInst->EventCb = pfnEvent;
    pInst->packetId = 0;
    return pInst;
}

/**
 * @}
 */

