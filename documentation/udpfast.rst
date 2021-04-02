UDP "Fast" Receiver
===================

``udpApp/`` contains a specialized equivalent PSC driver variant intended
for capturing and (optionally) recording to disk PSC UDP traffic from high bandwidth devices.
This was originally developed to continuously consume and record 1.8 Gbps (234 MB/s) of traffic.
This variant uses Linux specific APIs (eg. ``recvmmsg()`` and ``writev()``)
to reduce overheads.

UDP "fast" instances support all the usual :ref:`devsup` as well as those listed below.

See ``iocBoot/iocudpfast/st.cmd`` for an example usage.

Usage
-----

A driver instance needs to be created with ``createPSCUDPFast()`` ::

    # listen on 0.0.0.0:8765
    # for packets from 1.2.3.4:5678
    createPSCUDPFast("test", "1.2.3.4", 5678, 8765)

Control/Status DB
"""""""""""""""""

The ``pscudpfast.db`` database provides control/status of the file writing process,
as well as monitoring of network and file I/O performance. ::

    dbLoadRecords("db/pscudpfast.db", "NAME=test,P=TST:")

Tunable Parameters
""""""""""""""""""

- ``PSCUDPMaxPacketSize`` (default 1024) Size in bytes of packet buffers.  Larger packets will be ignored.
- ``PSCUDPMaxPacketRate`` (default 280000) Estimated maximum packet rate in packets per second.  Used to size pool of pre-allocated packet buffer pool.
- ``PSCUDPBufferPeriod`` (default 1.0) Estimated time in seconds to buffer packets.  Used to size pool of pre-allocated packet buffer pool.
- ``PSCUDPMaxLenMB`` (default 2000) File size at which to rotate to a new/empty file.
- ``PSCUDPSetSockBuf`` (default 0)  If non-zero, attempt to set resize OS socket buffer.
- ``PSCUDPDSyncSizeMB`` (default 0)  If non-zero, `flush()` data files writing this many MBs of data.

Add to IOC
""""""""""

The "fast" receiver is included in a separate DBD and library.  ::

    myioc_DBD += pscUDPFast.dbd
    myioc_LIBS += pscUDPFast

See also :ref:`includinginioc`.

Admin setup
-----------

It is expected that a larger socket buffer size will be needed to avoid sporadic packet loss.
This may be accomplished by raising the limit of socket buffer size. ::

    $ sysctl net.core.rmem_max=3407872

And then setting `PSCUDPSetSockBuf` accordingly. ::

    var PSCUDPSetSockBuf 3407872

This may be made persistent with: ::

    cat <<EOF > /etc/sysctl.d/99-pscdrv.conf
    # raise limit on socket RX buffer
    net.core.rmem_max=3407872
    EOF

It may also be desirable to run IOCs with realtime priorities.
To avoid a certain type of priority inversion, if this is done,
it is recommended to ensure that the priorities of Linux OS
interrupt handling thread have a higher priority than IOC threads.
A quick way to do this: ::

    # for pid in `pgrep '^irq/'`; do readlink /proc/$pid/exe || chrt -f -p 92 $pid; done

One way to make this persistent is ::

    cat <<EOF > /etc/systemd/system/irq-prio.service
    [Unit]
    Description=Raise IRQ thread priority
    [Service]
    Type=oneshot
    ExecStart=/bin/sh -c 'for pid in `pgrep "^irq/"`; do readlink /proc/\$pid/exe || chrt -f -p 92 \$pid; done'
    RemainAfterExit=yes
    [Install]
    WantedBy=multi-user.target
    EOF
    systemctl daemon-reload
    systemctl start irq-prio.service
    systemctl enable irq-prio.service

One way to grant the IOC executable permission to set RT priorities without also granting
full superuser privilege is to add file capabilities.  eg. ::

    setcap cap_ipc_lock,cap_sys_nice+ep bin/linux-x86_64/pscdemo

Note that Linux file capabilities are ignored if a filesystem is mounted with the ``nosuid`` option.

File Writing
------------

File name selection
"""""""""""""""""""

Data file names are formed by concatenating the string PVs ``$(P)FileDir-SP`` and ``$(P)FileBase-SP``
with a slash in between, then appending the current date and time in UTC as "YYYYMMDD-HHMMSS.dat".
So if FileDir is "/data" and FileBase is "run1-", then an output filename might be:
"/data/run1-20210401-034500.dat".
The most recent data filename is indicated by ``$(P)LastFile-I``.

File Format
"""""""""""

The .dat file written have a binary format similar to the wire protocol.
With an additional reception timestamp included.
Each file will contain a sequence of such "packets" concatenated together.  ::

          0     1     2     3
       +-----+-----+-----------+
    0  |  P  |  S  |   Msg ID  |
       +-----+-----+-----------+
    4  |      Body Length      |
       +-----------------------+
    C  |      Seconds          |
       +-----------------------+
   10  |      Nano-seconds     |
       +-----------------------+
   14  |      Body bytes ...   |

The ``Seconds`` field is an integer number of seconds since the POSIX epoch (1 Jan 1970 UTC).

Operation
---------

The ``PSCUDPFast`` class uses a multistage buffer based on a fixed size pool of pre-allocated packet buffers.
Statistics are kept of the percentage of total buffers in three states: unallocated (``$(P)PFree-I``), 
filled with packet data (``$(P)PRXe-I``), and being written to disk (``$(P)PWrt-I``).
During normal/stable operation, most buffers should be unallocated.
Buffering is designed to smooth over occasional slowdowns in disk I/O of up to `PSCUDPBufferPeriod` seconds.

If no unallocated/free packet buffers are available, then the driver will stop calling ``recvmmsg()``
and wait for buffers to become available.
eg. when some `writev()` has completed.
If this takes too long, the socket RX buffer will overflow.
Overflows should be indicated by a non-zero value of ``$(P)DrpRate-I`` after the next ``recvmmsg()``.

.. image:: buffering.svg

The Message Cache holds the most recently received packet for each message ID,
and is accessible through the :ref:`devsupreg` and :ref:`devsupblock` device supports.

The "short" buffer is intended to hold a few consecutive packets to facilitate online status and verification.
The buffer depth is control by the largest ``NELM`` of an associated aaiRecord.

"Short" Device Support
""""""""""""""""""""""

"Short" buffer device support is meant to be a single chain (per device)
beginning with a periodic scan, and ending with the "Clear" device support.  ::

    record(aai, "$(P)tbase") {
        field(DTYP, "PSCUDPFast Get Short")
        field(INP , "@test 12345 -1") # message id 12345, difference in RX time from first packet
        field(SCAN, "1 second")
        field(FTVL, "ULONG")
        field(EGU , "ns")
        field(NELM, "16")
        field(TSE , "-2") # TIME <- first packet RX time
        field(FLNK, "$(P)val0")
    }

    record(aai, "$(P)val0") {
        field(DTYP, "PSCUDPFast Get Short")
        field(INP , "@test 12345 0") # message id 12345, offset 0 bytes
        field(FTVL, "ULONG")
        field(NELM, "16")
        field(TSE , "-2")
        field(FLNK, "$(P)val1")
    }

    record(aai, "$(P)val1") {
        field(DTYP, "PSCUDPFast Get Short")
        field(INP , "@test 12345 1") # message id 12345, offset 4 bytes
        field(SCAN, "1 second")
        field(FTVL, "ULONG")
        field(NELM, "16")
        field(TSE , "-2")
        field(FLNK, "$(P)clr")
    }

    record(longin, "$(P)clr") {
        field(DTYP, "PSCUDPFast Clear Short")
        field(INP , "@test")
    }



Troubleshooting
---------------

The diagnostic rates/counters in ``pscudpfast.db`` should be consulted first.
Either the packet RX rate (``$(P)RXRate-I``) or the timeout rate (``$(P)TmoRate-I``) should be non-zero.

The ``PSCDebug`` global log level may be changed.
The default (0) will only print errors.
This can be raised up to 5 to print additional warnings and status.
Note that levels 3 and above are quite verbose.

Packet Loss
"""""""""""

Places where packet loss is known to be possible after packets leave the originating device,
and before being written to disk.

- Ethernet switch
- NIC -> OS input FIFO
- OS socket buffer

Drops by a switch aren't detected automatically.

Drops at the OS input are reported with other network interface statistics. ::

    $ /sbin/ifconfig eno6
    eno6: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
    ...
            RX packets 78734975761  bytes 75113071891586 (68.3 TiB)
            RX errors 0  dropped 855783  overruns 0  frame 0

Drops at the socket buffer are reported by the driver via ``$(P)DrpRate-I``.
