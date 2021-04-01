.. _devsup:

Device Supports
===============

A number of EPICS Device Supports are available which move data
to/from message buffers, or provide driver internal information (eg. TCP connection status).

.. note::
   All Device Supports use the INST_IO Link format (prefixed with "@") for INP and OUT fields.

Internal
--------

All Supports expect a link with only the name of a Device instance.
Typically this will be a macro (eg. "@$(DEVICE)").

PSC Ctrl Connected
""""""""""""""""""

A binary status indicating if the TCP connection is currently active.::

    record(bi, "$(P)Conn-Sts") {
        field(DTYP, "PSC Ctrl Connected")
        field(DESC, "Connection status")
        field(INP , "@$(NAME)")
        field(SCAN, "I/O Intr")
        field(ZNAM, "Disconnected")
        field(ONAM, "Connected")
        field(ZSV , "MAJOR")
    }

When SCAN="I/O Intr" this record will update immediately.
A record with this support can be combined with a calcoutRecord with OOPT="Transition to Non-zero"
to process some records each time the connection is (re)esstablish.
This might be used to (re)synchronize the device to match the IOC state when recovering from a power loss.

PSC On Connect
""""""""""""""

A binary "status" meant to SCAN="I/O Intr" each time a device becomes connected.
Intended to trigger scans to synchronize IOC and device after (re)connect. ::

    record(bi, "$(P)onconn") {
        field(DTYP, "PSC On Connect")
        field(INP , "@$(NAME)")
        field(SCAN, "I/O Intr")
        field(FLNK, "$(P)onconn:1")
    }
    record(fanout, "$(P)onconn:1") {
        ... # eg. write some registers
        field(LNKF, "$(P)Send") # flush
    }


PSC Ctrl Message
""""""""""""""""

A stringin record which contains the last informational log messages from the Driver.
The messages are sent at various times based on indicate internal state changes in the Driver
(eg. connection timeout).::

    record(stringin, "$(P)Msg-I") {
        field(DTYP, "PSC Ctrl Message")
        field(DESC, "Last message")
        field(INP , "@$(NAME)")
        field(SCAN, "I/O Intr")
    }

PSC Conn Count
""""""""""""""

A longin record which increments each time a TCP connection is established.

PSC Unknown Msg Count
"""""""""""""""""""""

A longin record which increments each time a message is received with a message ID
which is not associated with any Records and will be discarded.  This may be useful
during development.

PSC Ctrl Send
"""""""""""""

A longout record which queues the buffer for a single message ID to be sent.
See Driver Data Flow.
The message ID to be queued is taken from the VAL field, which must be in the range [0,65535].
This can either be set in the database file, or at runtime by modifiying the VAL field. ::

    record(longout, "$(P)Send:42") {
        field(DTYP, "PSC Ctrl Send")
        field(OUT , "@$(NAME)")
        field(VAL , "42") # message ID
        field(FLNK, "$(P)Send")
    }

PSC Ctrl Send All
"""""""""""""""""

A bo record which flushes the queued send buffer to the socket send buffer.
See Driver Data Flow. ::

    record(bo, "$(P)Send") {
        field(DTYP, "PSC Ctrl Send All")
        field(OUT , "@$(NAME)")
        field(ZNAM, "Send")
        field(ONAM, "Send")
    }

.. _devsupreg:

Register Block Access
---------------------

Individual values may be read/written to messages using DTYP="PSC Reg".
eg. longin reads 4 bytes as a signed 32-bit integer. ::

    record(longin, "$(P)reg1-I") {
        field(DTYP, "PSC Reg")
        field(INP , "@$(NAME) 11 4") # message id 11, offset 4 bytes
        field(SCAN, "I/O Intr")
        field(TSE , "-2") # record TIME <- RX time
    }

When using an output record with DTYP="PSC Reg", keep in mind that record processing stores the value
in an internal scratch buffer.
This may be repeated for multiple offsets, in order to populate a message.
Then the message needs to beto be queued with a DTYP="PSC Ctrl Send" record, as described above. ::

    record(longout, "$(P)reg1-SP") {
        field(DTYP, "PSC Reg")
        field(OUT , "@test 42 4")
        field(FLNK, "$(P)Send:42") # auto-queue
        info(autosaveFields_pass0, "VAL")
    }


longin/longout
""""""""""""""

Operates on a signed 32-bit integer

mbbi/mbbo
"""""""""

Operates on a signed 32-bit integer.
Respects the NOBT (number of bits) and SHFT (shift) fields to operate on bit registers. ::

    record(mbbo, "$(P)reg3:1-Sel") {
        field(DTYP, "PSC Reg")
        field(OUT , "@$(NAME) 1 12")
        field(NOBT, "2")
        field(SHFT, "16") # select bits 16 and 17
        ...
    }

bi/bo
"""""

Operate on an unsigned 32-bit integer.
Respects the MASK field. ::

    record(bo, "$(P)reg3:0-Sel") {
        field(DTYP, "PSC Reg")
        field(OUT , "@$(NAME) 1 12")
        field(MASK, "0x40000") # bit 18
        ...
    }

ai/ao
"""""

DTYP="PSC Reg" operates on a signed 32-bit integer.
Alternately, DTYP="PSC Reg F32" or DTYP="PSC Reg F64"
may be used to operate on 32 or 64-bit IEEE floating point values.

.. _devsupblock:

Waveform Block Access
---------------------

Arrays my be encoded/decoded from PSC messages with a waveform record.
Array element type is determined by the DTYP.
In this example, signed 16-bit integers.
The INP/OUT link contains the device instance name and message ID.
Optionally, an offset and step (in bytes) may be given to operate on interleaved arrays. ::

    record(waveform, "$(P)wfin-I") {
        field(DTYP, "PSC Block I16 In")
        field(INP , "@$(NAME) $(msgid) $(offset=0) $(step=0)")
        field(SCAN, "I/O Intr")
        field(FTVL, "DOUBLE")
        field(NELM, "10")
    }

Sending an array is accomplished by replacing "In" with "Out" in the DTYP. ::

    record(waveform, "$(P)wfout-SP") {
        field(DTYP, "PSC Block I16 Out")
        field(INP , "@$(NAME) $(msgid)")
        field(FTVL, "DOUBLE")
        field(NELM, "10")
        field(FLNK, "$(P)Send-Cmd")
    }

Single Register Writes
----------------------

These Supports send IOC to Device messages with an 8 byte body consisting of an address (4 bytes) and data (4 bytes).
The data can be either a 32-bit integer (unsigned or 2s complement signed), or a 32-bit IEEE-754 single precision floating point number.
All are in big endian byte order.

The following is a complete example message, including 8 bytes of header and 8 bytes of body.::

          0     1     2     3
       +-----+-----+-----------+
    0  |  P  |  S  |   Msg ID  |
       +-----+-----+-----+-----+
    4  |  0  |  0  |  0  |  8  |
       +-----+-----+-----+-----+
    8  |         Address       |
       +-----+-----+-----------+
    C  |         Value         |
       +-----------------------+

The additional of the optional 'info(SYNC, "SAME")' tag enables the output (ao, bo, longout, mbbo, mbboDirect) record
to also listen for a Device to IOC message with the same format.  When such a message is received, the record
value will be updated without sending an IOC to Device message.
This allows an IOC to be synced to a device, enabling multi-master scenarios.

All Supports expect a link with only the name of a Device instance, block number, and register number
(eg. "@$(DEV) 10 20").

PSC Single U32
""""""""""""""

For bo, mbbo, and mbboDirect records, the value is interpreted as an unsigned 32-bit integer.

PSC Single I32
""""""""""""""

For ao and longout records, the value is interpreted as a 2's complement signed 32-bit integer.

The following example will queue a IOC to Device message the record's value as a signed integer along
with the address 128 in a message with ID #4.
The presense of the optional "SYNC" info tag means that if a Device to IOC message with body size >=8 and ID #4 is received,
and the first 4 body bytes match 128, then this record will be updated using the second 4 body bytes.::

    record(longout, "$(P)Reg:0-SP") {
        field(DTYP, "PSC Single I32")
        field(OUT , "@$(DEV) 4 128")
        info(SYNC, "SAME")
    }

PSC Single F32
""""""""""""""

For ao records, the value is interpreted as a 32-bit IEEE-754 single precision floating point number.
