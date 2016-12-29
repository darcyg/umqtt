uMQTT Client Package {#mainpage}
====================

This is called _uMQTT_ or __umqtt__ for "micro-MQTT".  The _micro_ refers
to microcontroller.

* * *

This library provides a set of functions for processing MQTT client
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

__License__

BSD-style license, see here @ref license.

__Application Interface__

@ref umqtt_api "Link to API docs"

`umqtt` provides a straightforward set of functions for MQTT operations
such as Connect, Publish, Subscribe, etc.  Internally it keeps track of
the state of communication flow so that it can resend packets when needed
and know when requests have been properly acknowledged.  To do this it
provides a `Run` function that must be called from the application main
loop.  The application only needs to provide a millisecond tick timer and
the `umqtt` library will keep track of all timeouts.

`umqtt` does require some services from the application.  The application
must provide functions to read and write from the network, and functions
to allocate and free memory.  The concept of the network is completely
abstract.  From the point of view of `umqtt` library, it is just a data
pipe. It could be wired or wireless ethernet, or it could be a serial
link.  Anything that can be used to transmit and receive data packets.
The memory allocator can also be implemented in many ways.  The `umatt`
library only needs to receive memory it requests for holding packets.
It doesn't matter how the memory is allocated.  It could just be a
mapping to the run-time _malloc()/free()_ library functions.  Or it could be
a third party allocator such as [bget](http://www.fourmilab.ch/bget/),
or even just a simple list of fixed size buffers.

`umqtt` notifies the application of events with a set of optional
callback functions.  It is possible to use `umqtt` without any callback
functions.

__RTOS and thread safety__

It should be possible to use `umqtt` with an RTOS.  However the API
functions have not been written to be thread-safe.  If used with an
RTOS, either all the functions should be called from within a single
RTOS thread, or the API functions should be wrapped with a semaphore
to serialize access.  None of the API functions are inherently blocking,
although they do call the application-supplied network functions and
these should be implemented to also be non-blocking.

__Network Management__

`umqtt` is implemented to be completely network-agnostic.  It is up to
the application to provide the interface network.  This means that for a
typical TCP/IP network, the application must first establish the network
socket connection to an MQTT server, before calling umqtt_Connect().

__Dynamic memory usage__

Because MQTT protocol uses an acknowledgment packet flow, it requires
that the client track packets that have not been acknowledged and resend
unacknowledged packets.  Even if QoS 0 is used for published topics,
the client still needs to keep track of acks for connect, subscribe,
and unsubscribe packets.  `umqtt` does not make any attempts to throttle
the number of pending packets.  It adds pending packets to an internal
linked list.  When the appropriate ack packet is received from the network
it removes the pending packet from the list and frees it.  If the
application were to perform many requests at once (multiple subscribes, or
publish many topics with QoS 1) then the number of pending packets could
momentarily grow large until all of the ack packets are received back from
the broker.

`umqtt` also allocates memory to hold instance data when umqtt_New() is
called.  Therefore, umqtt_Delete() should always be called if the client
is to be shut down.

__Typical Flow__

- application initializes
- calls umqtt_New() to set up an instance of umqtt client
- establishes a network connection to a MQTT server
- calls umqtt_Connect() to establish MQTT client protocol
- calls umqtt_Run() from the main loop
- wait for Connack callback, or until umqtt_GetConnectedStatus() returns
UMQTT_ERR_CONNECTED
- calls umqtt_Subscribe() to be subscribe to MQTT topics
- calls umqtt_Publish() to publish a topic to MQTT server
- calls umqtt_Disconnect() to cleanly disconnect protocol
- calls umqtt_Delete() to clean up and free `umqtt` instance
- gets notified of subscribed topics via PublishCb_t() callback function

__Error Recovery__

`umqtt` uses a simple minded scheme for tracking network errors and
recovering.  If any network read or write fails, `umqtt` will abort the
current operation and return UMQTT_ERR_NETWORK error code.  It does not
otherwise change any internal state.  In case it is possible to recover
the network connection, the application can perform recovery steps and
resume using the `umqtt` instance and everything should continue as
before.  But usually if network read or write fails, it means that the
connection has been dropped or has some other problem.  In this case the
recovery procedure should be:

- application detects network error with UMQTT_ERR_NETWORK return code
- calls umqtt_Disconnect() to clean up the MQTT client connection
(note, this may return another network error but should still be called)
- perform network disconnection and cleanup steps
- call umqtt_Delete() to clean up and free the client instance
- repeat original steps from beginning to establish network connection,
initialize `umqtt` instance and connect the MQTT client

These steps may seem tedious, but it ensures that the MQTT client is
disconnected cleanly at the protocol level (if possible) and that all
allocated memory is freed.  Since `umqtt` allocates memory to store
packets, this procedure ensures that any pending packets that are still
allocated will be freed and thus avoid memory leaks due to network errors.
