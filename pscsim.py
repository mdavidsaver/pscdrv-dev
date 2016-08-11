#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
PSC protocol simulation

Has both client and server modes

$ ./pscsim.py -v --server localhost:8765

and/or

$ ./pscsim.py -v --client localhost:8765

Must have debian package 'python-twisted-core' installed
"""

import logging
log = logging.getLogger(__name__)

import struct, sys, time

from array import array

from optparse import OptionParser
from twisted.internet import reactor
from twisted.protocols.stateful import StatefulProtocol
from twisted.internet.protocol import Factory, ClientFactory

bswap = sys.byteorder!='big'

header = struct.Struct('>ccHI')
assert header.size==8

msg = struct.Struct('>HH')

TS = struct.Struct('>II')

class PSCProto(StatefulProtocol):
    def getInitialState(self):
        return (self.recv_header, header.size)
    def recv_header(self, data):
        log.debug("Head: %u '%s'", len(data), repr(data))
        P, S, msgid, bodylen = header.unpack(data)
        if (P,S) != ('P','S'):
            log.fatal("Framing Error!")
            self.transport.loseConnection()
            return
        self.msgid = msgid
        if bodylen>0:
            return (self.recv_body, bodylen)
        else:
            self.recv_psc(msgid, '')
    def recv_body(self, data):
        log.debug("Body: %u '%s'", len(data), repr(data))
        self.recv_psc(self.msgid, data)
        return self.getInitialState()

    def send_psc(self, msgid, body):
        log.debug("Send %u %u", msgid, len(body))
        head = header.pack('P', 'S', msgid, len(body))
        self.transport.write(head)
        self.transport.write(body)

    def recv_psc(self, msgid, body):
        print "Recv %u %u"%(msgid, len(body))
	print "  ",repr(body[91696:])

class PSCClient(PSCProto):
    pass

class PSCServer(PSCProto):
    reactor = reactor
    timer = None

    def connectionMade(self):
        log.info("Connection from %s", self.transport.getPeer())
        self.timer = self.reactor.callLater(0.0, self.ping)
        self.val = 1
        self.resync()

    def connectionLost(self, reason=None):
        log.info("Disconnect by %s: %s",
                 self.transport.getPeer(),
                 str(reason))
        if self.timer is not None:
            self.timer.cancel()
            self.timer=None

    def ping(self):
        self.send_psc(42, "hello world!")

        arr = array('H', range(1, 20))
        if bswap:
            arr.byteswap()
        self.send_psc(55, arr.tostring())

        self.timer = self.reactor.callLater(1.0, self.ping)
        self.val = (self.val+1)%0xffff

    def resync(self):
        log.info("Resync")
        self.send_psc(100, TS.pack(4660, 42))
        self.send_psc(100, TS.pack(4670, 43))
        
    def recv_psc(self, msgid, body):
        # Echo back with a different ID (ID+10)
        if msgid>=1000:
            # Echo back with a different ID w/ timestamp prepended
            T = time.time()
            T = TS.pack(int(T), 0)
            body = T+body

        elif msgid==100: # single register write
            addr, val = TS.unpack(body)
            if addr==10 and val!=0:
                self.resync()
            
        log.info("Echo %u %s", msgid, repr(body))
        self.send_psc(msgid+10, body)


_V = {0:logging.WARNING, 1:logging.INFO, 2:logging.DEBUG}

def main():
    P = OptionParser(usage="%prog [-C|-S] <-v> host:port")
    P.add_option('-v','--verbose', action='count', default=0,
                 help='Print more information')
    P.add_option('-C','--client', action='store_true', default=True,
                 dest='dir', help='Act as Client to PSC')
    P.add_option('-S','--server', action='store_false', dest='dir',
                 help='Act as Server to IOC')
    vals, args = P.parse_args()

    if len(args)<1:
        P.usage()
        sys.exit(1)

    host, _, port = args[0].partition(':')
    port = int(port or '6')

    logging.basicConfig(level=_V.get(vals.verbose, 0))

    if vals.dir:
        # Client
        log.info('Connect to %s:%u', host, port)
        fact = ClientFactory()
        fact.protocol = PSCClient
        ep = reactor.connectTCP(host, port, fact)
    else:
        # Server
        log.info('Serve from %s:%u', host, port)
        fact = Factory()
        fact.protocol = PSCServer
        ep = reactor.listenTCP(port, fact, interface=host or '')

    log.info('Run')
    reactor.run()
    log.info('Done')

if __name__=='__main__':
    main()
