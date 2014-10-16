.. pscdrv documentation master file, created by
   sphinx-quickstart on Fri Apr 18 15:16:24 2014.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

The PSC Driver
========================

Overview
========

The PSC (Portable Streaming Controller) driver is a general purpose
driver which speaks a TCP/IP based protocol designed to be implemented in custom
ethernet attached devices.  The goal is to provide a robust framework to enable
hardware engineers to create EPICS support for a new device without extensive
knowledge about EPICS driver writing.

The protocol uses TCP to exchange a series a variable length binary messages.
The design emphasizes full duplex operation to fully utilize available bandwidth.
:math:2^16` message identifiers are available.
It is up to the engineer to assign meaning to these messages.
The PSC driver provides the means to extract and assemble messages
to/from values in EPICS records (scalar and array).

Contents:

.. toctree::
   :maxdepth: 2

   protocol
   processing
   devsup


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

