#!/usr/bin/env python3

import asyncio
import logging
import signal
import socket
import struct

from math import pi, sin

import numpy as np

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
                if val not in (1, 5, 25, 250):
                    _log.error('Ignore invalid decimatation factor %u', val)
                else:
                    _log.info('Set decimate %u', val)
                    self.core.rate = 250e3/val

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
    period: float = 1.0
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
        self.rate = 250e3
        self.chan_mask = 0xffffffff # TODO: bug Eric about unsync'd default...

        self.tick_timer = self.loop.call_later(self.period, self.tick)

    async def close(self):
        self.tick_timer.cancel()
        if self.dest_timeout is not None:
            self.dest_timeout.cancel()

    def tick(self):
        "advance simulation"
        self.tick_timer = self.loop.call_later(self.period, self.tick)

        if not self.acq or self.dest is None:
            return

        now = self.loop.time()

        Fsim = np.asarray([
            100, 100, 100, 100,
            100, 100, 100, 100,
            1000, 1000, 1000, 1000,
            1000, 1000, 1000, 1000,
            10e3, 10e3, 10e3, 10e3,
            10e3, 10e3, 10e3, 10e3,
            100e3, 100e3, 100e3, 100e3,
            100e3, 100e3, 100e3, 100e3,
        ])
        assert Fsim.shape==(32,), Fsim.shape

        # output samples per period
        Ntot = int(self.rate * self.period)
        T = np.arange(Ntot) / self.rate # sec
        T = T[:,None].repeat(32, 1) # [#samp, #chan]
        spread = np.linspace(0, 31/16*pi, num=32)[None,:].repeat(T.shape[0], 0)
        Fsim = Fsim[None, :].repeat(T.shape[0], 0)
        S = 0x7fffff * np.sin(self.pha + 2*pi*Fsim*T + spread)
        self.pha += 2*pi/Fsim*Ntot

        # apply channel mask
        mask = [b=='1' for b in f'{self.chan_mask:032b}']
        S = S[:,mask]

        # packetize
        samp_per_pkt = 480//32 # TODO: properly support partial channel mask...
        for i in range(0, Ntot, samp_per_pkt):
            sec, ns = divmod(now + T[i,0], 1.0)
            ns *= 1e9
            sec, ns = int(sec), int(ns)

            self.seq = (self.seq+1)&0xFfffFfffFfffFfff

            body: bytes = _stream_head.pack(
                0, # status
                self.chan_mask,
                self.seq,
                sec,
                ns,
            ) + bytes(S[i:i+samp_per_pkt, :].astype('>i4').flatten().view('u1').reshape((-1,4))[:,1:4].flatten())

            assert len(body) <= 1464, (len(body), body[:40])
            pkt = _head.pack(b'PS', 20033, len(body)) + body

            #_log.debug('Stream %s <- %r', self.dest, pkt[:20])
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
