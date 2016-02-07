
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "umqtt.h"

// TODO: const on parameters

/**
 * MQTT connect, not network connect
 *
 * options:
 *  username/password
 *  clean session
 *  retain
 *  qos
 *  will
 *  will qos
 *  keepalive
 */

#define MQTT_TYPE_CONNECT 1
#define MQTT_TYPE_CONNACK 2
#define MQTT_TYPE_PUBLISH 3
#define MQTT_TYPE_PUBACK 4
#define MQTT_TYPE_SUBSCRIBE 8
#define MQTT_TYPE_SUBACK 9
#define MQTT_TYPE_UNSUBSCRIBE 10
#define MQTT_TYPE_UNSUBACK 11
#define MQTT_TYPE_PINGREQ 12
#define MQTT_TYPE_PINGRESP 13
#define MQTT_TYPE_DISCONNECT 14

#define MQTT_FLAG_DUP 0x08
#define MQTT_FLAG_RETAIN 0x01
#define MQTT_FLAG_QOS 0x06
#define MQTT_FLAG_QOS_SHIFT 1

#define MQTT_CONNECT_FLAG_USER 0x80
#define MQTT_CONNECT_FLAG_PASS 0x40
#define MQTT_CONNECT_FLAG_WILL_RETAIN 0x20
#define MQTT_CONNECT_FLAG_WILL_QOS 0x18
#define MQTT_CONNECT_FLAG_WILL 0x04
#define MQTT_CONNECT_FLAG_CLEAN 0x02
#define MQTT_CONNECT_FLAG_QOS_SHIFT 3

#define RETURN_IF_ERR(c,e) do{if(c){return (e);}}while(0)


/**
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
Mqtt_EncodeLength(uint32_t length, uint8_t *pEncodeBuf)
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

/**
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
Mqtt_DecodeLength(uint32_t *pLength, uint8_t *pEncodedLength)
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

// assumes valid inputs
static uint32_t
Mqtt_EncodeData(Mqtt_Data_t *pDat, uint8_t *pBuf)
{
    *pBuf++ = pDat->len >> 8;
    *pBuf++ = pDat->len & 0xFF;
    memcpy(pBuf, pDat->data, pDat->len);
    return pDat->len + 2;
}

Mqtt_Error_t
Mqtt_Connect(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Connect_Options_t *pOptions)
{
    uint8_t connectFlags = 0;
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), MQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    RETURN_IF_ERR(pOptions->clientId.len == 0, MQTT_ERR_PARM);
    uint16_t remainingLength = 10 // variable header
                             + 2 + pOptions->clientId.len;
    if (pOptions->willTopic.len)
    {
        connectFlags |= MQTT_CONNECT_FLAG_WILL;
        remainingLength += 2 + pOptions->willTopic.len;
        // if there is a will topic there should be a will message
        RETURN_IF_ERR(pOptions->willMessage.len == 0, MQTT_ERR_PARM);
        remainingLength += 2 + pOptions->willMessage.len;
    }
    if (pOptions->username.len)
    {
        connectFlags |= MQTT_CONNECT_FLAG_USER;
        remainingLength += 2 + pOptions->username.len;
    }
    if (pOptions->password.len)
    {
        connectFlags |= MQTT_CONNECT_FLAG_PASS;
        remainingLength += 2 + pOptions->password.len;
    }

    // if handle is NULL but other parameters are okay then caller
    // is asking for computed length of packet and no other action
    if (h == NULL)
    {
        // use a dummy buf to encode the remaining length in order
        // to find out how many bytes for the length field
        uint8_t encBuf[4];
        uint32_t byteCount = Mqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return MQTT_ERR_RET_LEN;
    }

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 12), MQTT_ERR_PARM);

    // encode the remaining length into the appropriate postion in the buffer
    uint32_t lenSize = Mqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, MQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = MQTT_TYPE_CONNECT << 4;
    idx = 1 + lenSize;

    // encode protocol name
    uint8_t protocolNameStr[4] = "MQTT";
    Mqtt_Data_t protocolName = { 4, protocolNameStr };
    idx += Mqtt_EncodeData(&protocolName, &buf[idx]);

    // protocol level, connect flags and keepalive
    connectFlags |= pOptions->willRetain ? MQTT_CONNECT_FLAG_WILL_RETAIN : 0;
    connectFlags |= pOptions->cleanSession ? MQTT_CONNECT_FLAG_CLEAN : 0;
    connectFlags |= (pOptions->qos << MQTT_CONNECT_FLAG_QOS_SHIFT) & MQTT_CONNECT_FLAG_WILL_QOS;
    buf[idx++] = 4;
    buf[idx++] = connectFlags;
    buf[idx++] = pOptions->keepAlive >> 8;
    buf[idx++] = pOptions->keepAlive & 0xFF;

    // client id
    RETURN_IF_ERR(pOptions->clientId.data == NULL, MQTT_ERR_PARM);
    idx += Mqtt_EncodeData(&pOptions->clientId, &buf[idx]);

    // will topic and message
    if (pOptions->willTopic.len)
    {
        // check data pointers
        RETURN_IF_ERR(pOptions->willTopic.data == NULL, MQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->willMessage.data == NULL, MQTT_ERR_PARM);
        idx += Mqtt_EncodeData(&pOptions->willTopic, &buf[idx]);
        idx += Mqtt_EncodeData(&pOptions->willMessage, &buf[idx]);
    }

    // username
    if (pOptions->username.len)
    {
        RETURN_IF_ERR(pOptions->username.data == NULL, MQTT_ERR_PARM);
        idx += Mqtt_EncodeData(&pOptions->username, &buf[idx]);
    }

    // password
    if (pOptions->password.len)
    {
        RETURN_IF_ERR(pOptions->password.data == NULL, MQTT_ERR_PARM);
        idx += Mqtt_EncodeData(&pOptions->password, &buf[idx]);
    }

    return MQTT_ERR_OK;
}

Mqtt_Error_t
Mqtt_Publish(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Publish_Options_t *pOptions)
{
    uint8_t flags = 0;
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->topic.len == 0, MQTT_ERR_PARM);

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
        uint32_t byteCount = Mqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return MQTT_ERR_RET_LEN;
    }

    Mqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 5), MQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = Mqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, MQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = MQTT_TYPE_PUBLISH << 4;
    idx = 1 + lenSize;

    // header flags
    flags |= pOptions->dup ? MQTT_FLAG_DUP : 0;
    flags |= pOptions->retain ? MQTT_FLAG_RETAIN : 0;
    flags |= (pOptions->qos << MQTT_FLAG_QOS_SHIFT) & MQTT_FLAG_QOS;
    buf[0] |= flags;

    // topic name
    RETURN_IF_ERR(pOptions->topic.data == NULL, MQTT_ERR_PARM);
    idx += Mqtt_EncodeData(&pOptions->topic, &buf[idx]);

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
        RETURN_IF_ERR(pOptions->message.data == NULL, MQTT_ERR_PARM);
        idx += Mqtt_EncodeData(&pOptions->message, &buf[idx]);
    }

    return MQTT_ERR_OK;
}

Mqtt_Error_t
Mqtt_Subscribe(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Subscribe_Options_t *pOptions)
{
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pTopics == NULL, MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pQos == NULL, MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->count == 0, MQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    uint16_t remainingLength = 2; // packet id
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        RETURN_IF_ERR(pOptions->pTopics[i].len == 0, MQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pTopics[i].data == NULL, MQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pQos[i] > 2, MQTT_ERR_PARM);
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
        uint32_t byteCount = Mqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return MQTT_ERR_RET_LEN;
    }

    Mqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 8), MQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = Mqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, MQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = (MQTT_TYPE_SUBSCRIBE << 4) | 0x02;
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
        idx += Mqtt_EncodeData(&pOptions->pTopics[i], &buf[idx]);
        buf[idx++] = pOptions->pQos[i];
    }

    return MQTT_ERR_OK;
}

Mqtt_Error_t
Mqtt_Unsubscribe(Mqtt_Handle_t h, Mqtt_Data_t *pOutBuf, Mqtt_Unsubscribe_Options_t *pOptions)
{
    uint32_t idx = 0;

    // initial parameter check
    RETURN_IF_ERR((pOutBuf == NULL) || (pOptions == NULL), MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->pTopics == NULL, MQTT_ERR_PARM);
    RETURN_IF_ERR(pOptions->count == 0, MQTT_ERR_PARM);

    // calculate the "remaining length" for the packet based on
    // the various input fields.
    uint16_t remainingLength = 2; // packet id
    for (uint32_t i = 0; i < pOptions->count; i++)
    {
        RETURN_IF_ERR(pOptions->pTopics[i].len == 0, MQTT_ERR_PARM);
        RETURN_IF_ERR(pOptions->pTopics[i].data == NULL, MQTT_ERR_PARM);
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
        uint32_t byteCount = Mqtt_EncodeLength(remainingLength, encBuf);

        // return total packet length
        pOutBuf->len = remainingLength + byteCount + 1;
        return MQTT_ERR_RET_LEN;
    }

    Mqtt_Instance_t *pInst = h;

    // make sure valid data buffer
    RETURN_IF_ERR((pOutBuf->data == NULL) || (pOutBuf->len < 7), MQTT_ERR_PARM);

    // encode the remaining length into the appropriate position in the buffer
    uint32_t lenSize = Mqtt_EncodeLength(remainingLength, &pOutBuf->data[1]);

    // check that the caller provide buffer is going to be big enough
    // for the rest of the packet
    RETURN_IF_ERR((1 + lenSize + remainingLength) > pOutBuf->len, MQTT_ERR_BUFSIZE);

    // assign length of returned buffer
    pOutBuf->len = 1 + lenSize + remainingLength;

    // encode the packet type and adjust index ahead to
    // point at variable header
    uint8_t *buf = pOutBuf->data;
    buf[0] = (MQTT_TYPE_UNSUBSCRIBE << 4) | 0x02;
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
        idx += Mqtt_EncodeData(&pOptions->pTopics[i], &buf[idx]);
    }

    return MQTT_ERR_OK;
}

Mqtt_Error_t
Mqtt_Process(Mqtt_Handle_t h, Mqtt_Data_t *pIncoming)
{
    Mqtt_Error_t err = MQTT_ERR_OK;
    if ((h == NULL) || (pIncoming == NULL))
    {
        return MQTT_ERR_PARM;
    }

    Mqtt_Instance_t *pInst = h;

    if (pIncoming->len)
    {
        uint8_t *pData = pIncoming->data;
        uint8_t type = pData[0] >> 4;
        uint8_t flags = pData[0] & 0x0F;
        uint32_t remainingLen;
        uint32_t lenCount = Mqtt_DecodeLength(&remainingLen, &pData[1]);
        if ((remainingLen + 1 + lenCount) != pIncoming->len)
        {
            return MQTT_ERR_PACKET_ERROR;
        }

        switch (type)
        {
            // only client related - not implementing server
            // only QOS 0 and 1
            case MQTT_TYPE_CONNACK:
            {
                if (pInst->EventCb)
                {
                    Mqtt_Connect_Result_t result;
                    result.sessionPresent = pData[2] & 1 ? true : false;
                    result.returnCode = pData[3];
                    pInst->EventCb(h, MQTT_EVENT_CONNECTED, &result);
                }
                break;
            }
            case MQTT_TYPE_PUBLISH:
            {
                Mqtt_Publish_Options_t pubdata;
                uint8_t pktId[2];

                if (pInst->EventCb)
                {
                    pubdata.dup = flags & MQTT_FLAG_DUP ? true : false;
                    pubdata.retain = flags & MQTT_FLAG_RETAIN ? true : false;
                    pubdata.qos = (flags & MQTT_FLAG_QOS) >> MQTT_FLAG_QOS_SHIFT;
                    RETURN_IF_ERR(pubdata.qos > 2, MQTT_ERR_PACKET_ERROR);

                    // find the topic length and value
                    uint32_t idx = 1 + lenCount;
                    uint16_t topicLen = (pData[idx] << 8) + pData[idx + 1];
                    idx += 2;
                    RETURN_IF_ERR((topicLen + 2) > remainingLen, MQTT_ERR_PACKET_ERROR);
                    pubdata.topic.len = topicLen;
                    pubdata.topic.data = &pData[idx];
                    remainingLen -= topicLen + 2;
                    idx += topicLen;

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
                            return MQTT_ERR_PACKET_ERROR;
                        }
                    }
                    if (remainingLen != 0)
                    {
                        uint16_t msgLen = (pData[idx] << 8) + pData[idx + 1];
                        idx += 2;
                        RETURN_IF_ERR((msgLen + 2) > remainingLen, MQTT_ERR_PACKET_ERROR);
                        pubdata.message.len = msgLen;
                        pubdata.message.data = &pData[idx];
                        remainingLen -= msgLen + 2;
                    }
                    RETURN_IF_ERR(remainingLen != 0, MQTT_ERR_PACKET_ERROR);
                    pInst->EventCb(h, MQTT_EVENT_PUBLISH, &pubdata);
                    if (pubdata.qos != 0)
                    {
                        Mqtt_Data_t puback;
                        uint8_t pubackdat[4];
                        pubackdat[0] = 0x40;
                        pubackdat[1] = 2;
                        pubackdat[2] = pktId[0];
                        pubackdat[3] = pktId[1];
                        puback.len = 4;
                        puback.data = pubackdat;
                        pInst->EventCb(h, MQTT_EVENT_REPLY, &puback);
                    }
                }

                break;
            }
            case MQTT_TYPE_PUBACK:
            {
                if (pInst->EventCb)
                {
                    // TODO: something to verify packet id
                    pInst->EventCb(h, MQTT_EVENT_PUBACK, NULL);
                }
                break;
            }
            case MQTT_TYPE_SUBACK:
            {
                RETURN_IF_ERR(remainingLen < 3, MQTT_ERR_PACKET_ERROR);
                if (pInst->EventCb)
                {
                    Mqtt_Data_t subackData;
                    // TODO: something to verify packet id
                    subackData.len = remainingLen - 2;
                    subackData.data = &pData[4];
                    pInst->EventCb(h, MQTT_EVENT_SUBACK, &subackData);
                }
                break;
            }
            case MQTT_TYPE_UNSUBACK:
            {
                if (pInst->EventCb)
                {
                    // TODO: something to verify packet id
                    pInst->EventCb(h, MQTT_EVENT_UNSUBACK, NULL);
                }
                break;
            }
            case MQTT_TYPE_PINGRESP:
            {
                if (pInst->EventCb)
                {
                    pInst->EventCb(h, MQTT_EVENT_PINGRESP, NULL);
                }
                break;
            }
            default:
            {
                err = MQTT_ERR_PACKET_ERROR;
                break;
            }
        }
    }
    return err;
}

Mqtt_Handle_t
Mqtt_InitInstance(Mqtt_Instance_t *pInst, void (*pfnEvent)(Mqtt_Handle_t, Mqtt_Event_t, void *))
{
    if (pInst == NULL)
    {
        return NULL;
    }
    pInst->EventCb = pfnEvent;
    pInst->packetId = 0;
    return pInst;
}


















