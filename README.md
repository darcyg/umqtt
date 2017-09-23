uMQTT Client Package
====================

This is a MQTT client implementation, meant to be straightforward and
portable, and targeted for use on a microcontroller.

[![Build Status](https://travis-ci.org/kroesche/umqtt.svg?branch=master)](https://travis-ci.org/kroesche/umqtt)

License
-------

Copyright © 2016, Joseph Kroesche (tronics.kroesche.io). All rights reserved.
This software is released under the FreeBSD license, found in the accompanying
file LICENSE.txt and at the following URL:

http://www.freebsd.org/copyright/freebsd-license.html

This software is provided as-is and without warranty.

Documentation
-------------

API documentation is provided as comments in the source file.  A documentation
set can be generated using Doxygen.  Run doxygen from the `docs` subdirectory
and view the generated index.html.

The generated documentation will also eventually appear here:

http://kroesche.github.io/umqtt/

Usage
-----

The two source files, `umqtt.h` and `umqtt.c` are meant to be compiled into
your application.  Much more detailed information is provided in the
above mentioned documentation.

There are some examples in the [umqtt_test](https://github.com/kroesche/umqtt_test)
repo:

* unit_test - detailed unit test against all umqtt functions
* compliance - system level or compliance test against a real MQTT broker
* mcu_app - simple example running on a real MCU

Support
-------

I created this for use in my own projects and make no promises of support.
However if you find a problem feel free to open an issue and I'll take a
look.  Also free free to submit pull request for fixes although if you plan
on submitting major changes it's best to contact me first.

