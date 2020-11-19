#!/usr/bin/env python3

import logging
import time
import struct
import signal
import asyncio

_log = logging.getLogger(__name__)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('endpoint', help='Bind to address:port')
    P.add_argument('-v', '--verbose', action='store_const',
                   const=logging.DEBUG, default=logging.INFO,
                   help='Make some noise')
    P.add_argument('-U', '--udp', action='store_true')
    P.add_argument('-T', '--tcp', action='store_false', dest='udp')
    return P

def build_msg(msgid, body):
    blen = len(body)
    return struct.pack('>2sHI', b'PS', msgid, blen) + body

async def tcp_keepalive(writer):
    peer = writer.get_extra_info('peername')
    while True:
        _log.debug('%s ping', peer)
        writer.write(build_msg(42, b"hello world!"))
        await writer.drain()
        await asyncio.sleep(1.0)

async def handle_tcp_client(reader, writer):
    loop = asyncio.get_running_loop()
    peer = writer.get_extra_info('peername')
    _log.info('New client %s', peer)

    pinger = loop.create_task(tcp_keepalive(writer))
    try:
        writer.write(build_msg(100, b'\x00\x00\x124\x00\x00\x00*'))
        writer.write(build_msg(100, b'\x00\x00\x12>\x00\x00\x00+'))
        await writer.drain()

        while True:
            P, S, msgid, blen = struct.unpack('>ccHI', await reader.readexactly(8))
            _log.debug('RX %s msgid %d blen %d', peer, msgid, blen)

            if P!=b'P' or S!=b'S':
                raise RuntimeError('Framing error')

            body = await reader.readexactly(blen)

            # Echo back with a different ID (ID+10)
            if msgid>=1000:
                # Echo back with a different ID w/ timestamp prepended
                SEC, NS = divmod(time.time(), 1.0)
                body = struct.pack('>II', int(SEC), int(NS*1e9)) + body

            msgid += 10

            writer.write(build_msg(msgid, body))
            await writer.drain()

    except asyncio.IncompleteReadError:
        pass # normal on close
    except:
        _log.exception('Error client %s', peer)
    finally:
        pinger.cancel()
        try:
            await pinger
        except asyncio.CancelledError:
            pass
    _log.info('Lost client %s', peer)

class UDPProto(object):
    def connection_made(self, transport):
        self.transport = transport
        self.peer = None
        _log.info('Listening on %s', transport.get_extra_info('sockname'))

    def datagram_received(self, data, addr):
        P, S, msgid, blen = struct.unpack('>ccHI', data[:8])
        _log.debug('RX %s msgid %d blen %d', addr, msgid, blen)

        if P!=b'P' or S!=b'S':
            _log.error('Ignore invalid RX %s msgid %d blen %d', addr, msgid, blen)
            return

        body = data[8:8+blen]

        if addr != self.peer:
            _log.info('New peer %s -> %s', self.peer, addr)
            self.peer = addr

            self.transport.sendto(build_msg(100, b'\x00\x00\x124\x00\x00\x00*'), addr)
            self.transport.sendto(build_msg(100, b'\x00\x00\x12>\x00\x00\x00+'), addr)

        # Echo back with a different ID (ID+10)
        if msgid>=1000:
            # Echo back with a different ID w/ timestamp prepended
            SEC, NS = divmod(time.time(), 1.0)
            body = struct.pack('>II', int(SEC), int(NS*1e9)) + body

        msgid += 10

        self.transport.sendto(build_msg(msgid, body), addr)

async def main(args):
    loop = asyncio.get_running_loop()
    logging.basicConfig(level=args.verbose)

    host, _sep, port = args.endpoint.partition(':')
    port = int(port or '1234')
    _log.info('Binding to %s:%s', host, port)

    if args.udp:
        sock, proto = await loop.create_datagram_endpoint(lambda: UDPProto(), local_addr=(host, port))

    else:
        sock = await asyncio.start_server(handle_tcp_client, host=host, port=port)

    done = asyncio.Event()
    loop.add_signal_handler(signal.SIGINT, done.set)

    _log.info('Running')
    await done.wait()
    _log.info('Done')

if __name__=='__main__':
    asyncio.run(main(getargs().parse_args()))
