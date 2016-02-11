uMQTT Client Package {#mainpage}
====================

This is called _uMQTT_ or __umqtt__ for "micro-MQTT".  The _micro_ refers
to microcontroller.

This library provides a set of function for processing MQTT client
packets.  It is intended primarily for use with microcontrollers.

The design goals:
- written to be portable, not targeting any specific platform
- completely isolate the packet processing from packet transmission
  and reception
- not dependent on any particular RTOS, does not require RTOS but could
  be used with one
- not tied to any particular networking hardware or stack
- have well documented, consistent and understandable API

The library uses instance handles so the same code can be used for
multiple client connections to one or more MQTT servers.

__Sending__

To send a packet, first the packet options are encoded into a buffer.  Then
the buffer is transmitted to the connected MQTT server.  The client
provides the buffer and calls the appropriate _umqtt_ function to encode
the packet.  Here is the general flow for sending a packet:

- Populate options structure as needed, depending on type of packet
  (CONNECT, SUBSCRIBE, etc)
- Initialize a data block, allocating buffer space as needed (can be
  static or dynamic).  The data block will hold the outgoing packet.
- Call packet-building function
- Check return error code
- Pass returned data block (buffer and length) to your network transmission
  function.

~~~~~~~~.c
// simple example to subscribe to a single topic with QoS 0
#include "umqtt.h"


~~~~~~~~

