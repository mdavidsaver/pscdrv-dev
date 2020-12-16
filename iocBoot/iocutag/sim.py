#!/usr/bin/env python3

import time
import struct
import logging
import asyncio

from numpy.random import randn
import numpy as np

_log = logging.getLogger(__name__)

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('--bind', default='0.0.0.0:5678')
    P.add_argument('-v', '--verbose', action='store_const', default=logging.INFO, const=logging.DEBUG)
    return P

class Server:
    def __init__(self):
        self.clients = {}
        # bit 0 - beam on
        # bit 1 - source A/B
        # bit 2 - auto toggle source
        self.tag = 0

        self._tick = asyncio.ensure_future(self.tick())

    async def tick(self):
        try:
            while True:
                if self.tag&4:
                    self.tag ^= 2 # auto toggle source

                await asyncio.sleep(0.1)
                sec, nsec = divmod(time.time(), 1.0)
                sec, nsec = int(sec), int(nsec*1e9)

                #_log.debug('Tick')

                T = np.linspace(0.0, 10.0, 1024)
                V = randn(*T.shape)*2.0

                amp = 0.0
                mean = 0.0
                std = 1.0

                if self.tag&1:
                    # beam on
                    if self.tag&2:
                        # source B
                        amp = 10.0
                        mean = 3.0
                        std = 1.0
                    else:
                        # source A
                        amp = 15.0
                        mean = 6.6
                        std = 1.5

                amp += randn()/5.0
                mean += randn()/5.0
                std += randn()/5.0

                V += amp * np.exp(-(T-mean)**2/(2*std**2))

                V = (V*20.0).astype('>i4')

                M = [None, struct.pack('>III', sec, nsec, self.tag), V.tobytes()]

                M[0] = struct.pack('>2sHI', b'PS', 0, len(M[1])+len(M[2]))
                M = b''.join(M)

                for sock in self.clients.values():
                    sock.write(M)
                for peer, sock in self.clients.items():
                    try:
                        await asyncio.wait_for(sock.drain(), 0.5)
                    except asyncio.TimeoutError:
                        _log.error('%s TX stalled', peer)
                        sock.close()

        except:
            _log.exception('Tick fails')

    async def handle(self, reader, writer):
        peer = writer.get_extra_info('peername')
        _log.info('%s connects', peer)
        try:
            self.clients[peer] = writer

            while True:
                H = await reader.readexactly(8)
                PS, msgid, blen = struct.unpack('>2sHI', H)
                if PS!=b'PS':
                    raise RuntimeError('Corrupt header '+repr(H))
                B = await reader.readexactly(blen)
                _log.debug("%s sends %d %s", peer, msgid, B)

                if msgid==1: # set TAG
                    self.tag, = struct.unpack('>I', B[:4])
                    _log.info('%s TAG=%x', peer, self.tag)

        except Exception as e:
            _log.exception('%s error', peer)
        finally:
            _log.info('%s disconnects', peer)
            self.clients.pop(peer)

async def main(args):
    iface, _sep, port = args.bind.partition(':')
    serv = Server()

    listener = await asyncio.start_server(serv.handle, iface, int(port or '5678'))

    _log.info('Serving from %s', listener.sockets[0].getsockname())
    async with listener:
        await listener.serve_forever()

if __name__=='__main__':
    args = getargs().parse_args()
    logging.basicConfig(level=args.verbose)
    asyncio.run(main(args))
