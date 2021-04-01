High speed UDP packet logger
============================

`udpApp/` contains a specialized equivalent PSC driver variant intended
for capturing and (optionally) recording to disk PSC UDP traffic from high bandwidth devices.
This was originally developed to continuously consume and record 1.8 Gbps (234 MB/s) of traffic.

See `iocBoot/iocudpfast/st.cmd` for an example usage.

This variant uses Linux specific APIs (eg. `recvmmsg()` and `writev()`)
to reduce overheads.

Tunable Parameters
------------------

`PSCUDPMaxPacketSize` (default 1024) Use to size pre-allocated packet buffers in bytes.
Larger packets will be ignored.

`PSCUDPMaxPacketRate` (default 280000) Estimated maximum packet rate in packets per second.
Used to size pool of pre-allocated packet buffers.

`PSCUDPMaxFlushPeriod` (default 1.0) Estimated time in seconds to buffer packets.
Expected to be based on disk write (aka. flush) time.

`PSCUDPMaxLenMB` (default 2000) File size at which to rotate to a new/empty file.

`PSCUDPSetSockBuf` (default 0)  If non-zero, attempt to set resize OS socket buffer.

`PSCUDPDSyncSizeMB` (default 0)  If non-zero, `flush()` data files writing this many
MBs of data.

Admin setup
-----------

It is expected that a larger socket buffer size will be needed to avoid
sporadic packet loss due to other OS tasks.
This may be accomplished by raising the limit of socket buffer size,
and then setting `PSCUDPSetSockBuf` accordingly.

```
sysctl net.core.rmem_max=3407872
```

```
var PSCUDPSetSockBuf 3407872
```

This may be made persistant with:

```
cat <<EOF > /etc/sysctl.d/99-pscdrv.conf
# raise limit on socket RX buffer
net.core.rmem_max=3407872
EOF
```

It may also be desirable to run IOCs with realtime priorities.
To avoid a certain type of priority inversion, if this is done,
it is recommended to ensure that the priorities of Linux OS
interrupt handling thread have a higher priority than IOC threads.
A quick way to do this:

```
# for pid in `pgrep '^irq/'`; do readlink /proc/$pid/exe || chrt -f -p 92 $pid; done
```

One way to make this persistant is

```
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
```

One way to grant the IOC executable permission to set RT priorities without also granting
full superuser privlage is to add file capabilities.  eg.

```
setcap cap_ipc_lock,cap_sys_nice+ep bin/linux-x86_64/pscdemo
```

Control/Status DB
-----------------

```
dbLoadRecords("db/pscudpfast.db", "NAME=test,P=TST:")
```

The `pscudpfast.db` database provides control/status of the file writing process,
as well as monitoring of network and file I/O performance.

### Buffering

The `PSCUDPFast` class uses a multistage buffer based on a fixed size pool of pre-allocated packet buffers.
Statistics are kept of the percentage of total buffers in three states: unallocated (`$(P)PFree-I`), 
filled with packet data (`$(P)PRXe-I`), and being written to disk (`$(P)PWrt-I`).
During normal/stable operation, most buffers should be unallocated.
Buffering is designed to smooth over occasional slowdowns in disk I/O.


Add to IOC
----------


File Format
-----------
