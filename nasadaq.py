#!/usr/bin/env python3

import asyncio
import logging
import signal
import socket
import struct

from math import pi, sin

def logcb(fn):
    def wrapper(*args, **kws):
        try:
            return fn(*args, **kws)
        except:
            _log.exception("Unexpected exception")
    return wrapper

_log = logging.getLogger(__name__)

_head = struct.Struct('!2sHI')
_msg_10 = struct.Struct('!4sII')
_reg_u32 = struct.Struct('!II')
_int32 = struct.Struct('!i')

_stream_head = struct.Struct('!IIQII')

class PSCUDP(asyncio.DatagramProtocol):
    def __init__(self, core: 'DAQCore'):
        self.core = core

    @logcb
    def datagram_received(self, data, src):
        while len(data)>=_head.size:
            ps, msgid, blen = _head.unpack(data[:_head.size])
            body = data[_head.size:_head.size+blen]
            data = data[_head.size+blen:]

            if ps!=b'PS' or blen != len(body):
                _log.error('Malformed packet from %s %r', src, data[:20])
                return

            try:
                self.message_received(body, src, msgid)
            except:
                _log.exception("Error processing msgid=0x%04x", msgid)

        if data:
            _log.warn("Leftovers in msgid=0x%04x : %r", msgid, data[:20])

    @logcb
    def message_received(self, body, src, msgid):
        _log.debug('Ignore Unknown msgid=0x%04x : %r', msgid, data[:20])

class ControlProtocol(PSCUDP):
    @logcb
    def connection_made(self, transport: asyncio.DatagramTransport):
        self.transport = transport

    @logcb
    def message_received(self, body, src, msgid):
        _log.debug('RX ctrl %s -> 0x%04x %r', src, msgid, body[:20])

        if msgid==16951:
            reg, val = _reg_u32.unpack(body[:8])
            if reg==0: # reboot
                pass
            elif reg==5: # PPS align?
                pass
            elif reg==10: # Acq
                # 0 - disable, 1 - enable
                _log.info('Set Acquire %08x', val)
                self.core.acq = val!=0

            elif reg==11: # Chan mask
                _log.info('Set channel mask %08x', val)
                self.core.chan_mask = val

            elif reg==20: # Downsample
                _log.info('Set decimate %08x', val)

            elif reg==21: # filter?
                pass
            else:
                _log.warning('Ignore write to Unimplemented reg %u = %u', reg, val)


        elif msgid==16952 and body[:4]==b'\0\0\0\0': # Trigger system monitor readback
            reply = b'\0'*128 # TODO: actual status
            msg = _head.pack(b'PS', msgid, len(reply))
            self.transport.sendto(msg, src)

        else:
            _log.debug('Ignore Unimplemented msgid=0x%04x', msgid)
            return

        # echo with top msgid bit set
        reply = _head.pack(b'PS', 0x8000 | msgid, len(body)) + body
        _log.debug('TX %s <- %r', src, reply[:20])
        self.transport.sendto(reply, src)

class DataProtocol(PSCUDP):
    @logcb
    def connection_made(self, transport: asyncio.DatagramTransport):
        self.core.data_tr = transport

    @logcb
    def datagram_received(self, data, src):
        self.core.set_stream(src)

class DAQCore:
    period: float = 0.1
    stream_timeout = 10.0
    data_tr: asyncio.DatagramTransport

    def __init__(self):
        self.loop = asyncio.get_running_loop()

        # streaming destination
        self.dest = None
        self.dest_timeout: asyncio.TimerHandle = None

        self.acq = False
        self.seq = 0
        self.pha = 0.0
        self.chan_mask = 0xffffffff # TODO: bug Eric about unsync'd default...

        self.tick_timer = self.loop.call_later(self.period, self.tick)

    async def close(self):
        self.tick_timer.cancel()
        if self.dest_timeout is not None:
            self.dest_timeout.cancel()

    def tick(self):
        "advance simulation"
        self.tick_timer = self.loop.call_later(self.period, self.tick)

        now = self.loop.time()
        sec, ns = divmod(now, 1.0)
        ns *= 1e9
        sec, ns = int(sec), int(ns)

        self.seq = (self.seq+1)&0xFfffFfffFfffFfff

        if self.acq and self.dest is not None:
            pkt = [
                _stream_head.pack(
                    0, # status
                    self.chan_mask,
                    self.seq,
                    sec,
                    ns,
                ),
            ]

            for n in range(480//32):
                for ch in range(32):
                    if self.chan_mask & (1<<ch):
                        # spread 32 channels around phase
                        V = sin(self.pha + ch/16*pi)

                        V = int(V * 0x7fffff) # scale [-1.0, 1.0) to 24-bit signed range

                        pkt.append(_int32.pack(V)[1:]) # truncate to 3 bytes
                        assert len(pkt[-1])==3, pkt[-1]

                self.pha += 2*pi/100

            assert len(pkt)<=481, len(pkt)
            pkt = b''.join(pkt)
            assert len(pkt) <= 1464, (len(pkt), pkt[:40])
            pkt = _head.pack(b'PS', 20033, len(pkt)) + pkt

            _log.debug('Stream %s <- %r', self.dest, pkt[:20])
            self.data_tr.sendto(pkt, self.dest)


    def _stream_timeout(self):
        _log.warn("Stream timeout %s", self.dest)
        self.set_stream(None)

    def set_stream(self, dest):
        prev, self.dest_timeout = self.dest_timeout, None
        if dest is not None:
            self.dest_timeout = self.loop.call_later(self.stream_timeout, self._stream_timeout)
        if prev is not None:
            prev.cancel()

        prev, self.dest = self.dest, dest
        if prev!=self.dest:
            _log.info("Switch stream %s -> %s", prev, self.dest)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument("-v", "--verbose",
                   dest='level', default=logging.INFO,
                   action='store_const', const=logging.DEBUG)
    P.add_argument('--bind', default='127.0.0.1')
    P.add_argument('--cport', type=int, default=54398)
    P.add_argument('--dport', type=int, default=54399)

    return P

async def main(args):
    loop = asyncio.get_running_loop()
    core = DAQCore()
    ctrl_tr, ctrl_p = await loop.create_datagram_endpoint(lambda:ControlProtocol(core),
                                                  (args.bind, args.cport))
    data_tr, data_p = await loop.create_datagram_endpoint(lambda:DataProtocol(core),
                                                  (args.bind, args.dport))

    _log.info('Control addr: %s', ctrl_tr.get_extra_info('sockname'))
    _log.info('Data addr: %s', data_tr.get_extra_info('sockname'))

    done = asyncio.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, done.set)

    _log.info('Running')
    await done.wait()
    _log.info('Stopping')
    await core.close()
    _log.info('Done')

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.level)
    asyncio.run(main(args))
