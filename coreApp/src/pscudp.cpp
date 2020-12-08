/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdexcept>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/thread.h>

#include <dbAccess.h>

#define epicsExportSharedSymbols
#include "psc/device.h"

#define HEADER_SIZE 8

PSCUDP::PSCUDP(const std::string &name,
               const std::string &host,
               unsigned short hostport,
               unsigned short ifaceport,
               unsigned int timeoutmask)
    :PSCBase(name, host, hostport, timeoutmask)
    ,rxscratch(1024) // must be greater than HEADER_SIZE
{
    socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(socket==-1)
        throw std::runtime_error("Failed to allocate socket");

    try{
        evutil_make_socket_nonblocking(socket);
        evutil_make_listen_socket_reuseable(socket);
        evutil_make_socket_closeonexec(socket);

        evt_rx = event_new(base->get(), socket, EV_READ|EV_TIMEOUT|EV_PERSIST, &ev_recv, this);
        evt_tx = event_new(base->get(), socket, EV_WRITE|EV_TIMEOUT, &ev_send, this);
        if(!evt_rx || !evt_tx)
            throw std::bad_alloc();

        {
            char pbuf[INET_ADDRSTRLEN];
            sprintf(pbuf, "%u", hostport);

            evutil_addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            hints.ai_flags = EVUTIL_AI_ADDRCONFIG;

            evutil_addrinfo *answer = NULL;
            printf("lookup %s:%d\n", host.c_str(), hostport);

            int err = evutil_getaddrinfo(host.c_str(), pbuf, &hints, &answer);
            if(err)
                throw std::runtime_error(evutil_gai_strerror(err));
            sockaddr_in *inanswer = (sockaddr_in*)answer->ai_addr;

            assert(answer->ai_family==AF_INET);
            memcpy(&ep, answer->ai_addr, answer->ai_addrlen);
            ep.sin_port = htons(hostport);

            pbuf[0]='\0';
            evutil_inet_ntop(answer->ai_family, &inanswer->sin_addr.s_addr, pbuf, sizeof(pbuf));
            printf("Target address: %s:%d\n", pbuf, hostport);

            evutil_freeaddrinfo(answer);
        }

        {
            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(ifaceport);

            if(::bind(socket, (sockaddr*)&addr, sizeof(addr))==-1) {
                std::string msg("bind() failed: ");
                msg += evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
                throw std::runtime_error(msg);
            }
        }
    }catch(...){
        evutil_closesocket(socket);
        throw;
    }
}

PSCUDP::~PSCUDP() {
    event_free(evt_rx);
    event_free(evt_tx);
}

void PSCUDP::connect()
{
    timeval timeout = {5,0};
    if(event_add(evt_rx, &timeout))
        throw std::runtime_error("Failed to add Rx event");

    connected = true; // UDP socket is always "connected"
    scanIoRequest(onConnect);
    for(size_t i=0, N=procOnConnect.size(); i<N; i++) {
        dbCommon *prec = procOnConnect[i];
        dbScanLock(prec);
        dbProcess(prec);
        dbScanUnlock(prec);
    }

    if(PSCDebug>4)
        timefprintf(stderr, "%s: \"connected\"\n", name.c_str());
}

void PSCUDP::senddata(short evt)
{
    if(PSCDebug>4)
        timefprintf(stderr, "%s: TX wakeup with %u\n", name.c_str(), (unsigned)txbuf.size());

    bool scanme = false;

    if((evt&EV_TIMEOUT) && PSCDebug>0)
        timefprintf(stderr, "%s: TX timeout with %u\n", name.c_str(), (unsigned)txbuf.size());

    while((evt&EV_WRITE) && !txbuf.empty()) {
        buffer_t& scratch = txbuf.front();

        ssize_t ret = sendto(socket, &scratch[0], scratch.size(), 0, (sockaddr*)&ep, sizeof(ep));

        if(ret==-1) {
            if(errno==EAGAIN || errno==EWOULDBLOCK) {
                // no op, just retry
            } else {
                conncount++;
                message = "Tx socket error: ";
                message += evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
                scanme = true;
            }
            break;

        } else if(scratch.size()!=(size_t)ret) {
            conncount++;
            message = "Tx socket truncate";
            scanme = true;
        }

        if(readybuf.size()<64u) {
            // reuse packet buffer
            readybuf.splice(readybuf.end(),
                            txbuf,
                            txbuf.begin());

        } else {
            readybuf.pop_front();
        }
    }

    if(!txbuf.empty()) {
        // try again
        timeval timeout = {5,0};
        if(event_add(evt_tx, &timeout))
            throw std::runtime_error("Failed to add Tx event");
    }

    if(scanme)
        scanIoRequest(scan);
}

void PSCUDP::recvdata(short evt)
{
    if(evt&EV_TIMEOUT) {
        conncount++;
        message = "Rx timeout";
        scanIoRequest(scan);
        return;

    } else if(!(evt&EV_READ)) {
        timefprintf(stderr, "%s: Unknown event %x\n", name.c_str(), evt);
        return;
    }

    if(PSCDebug>4)
        timefprintf(stderr, "%s: RX wakeup\n", name.c_str());
    unsigned int npkt = 0, nloop = 0;

    bool scanme = false;

    while(true) {
        nloop++;
        // recvfrom() gives one packet at a time regardless of buffer size,
        // loop until no more packets are immediately available.

        sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);

        ssize_t ret = recvfrom(socket, &rxscratch[0], rxscratch.size(), 0, (sockaddr*)&addr, &addrlen);

        if(ret==-1) {
            if(errno==EAGAIN || errno==EWOULDBLOCK) {
                // no op, just retry
            } else {
                conncount++;
                message = "Rx socket error: ";
                message += evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
                scanme = true;
            }
            break;

        } else if(evutil_sockaddr_cmp((sockaddr*)&addr, (sockaddr*)&ep, 1)!=0) {
            // Ignore packet from other than expected source address:port
            if(PSCDebug>4) {
                char buf[40];
                evutil_inet_ntop(addr.sin_family, &addr.sin_addr.s_addr, buf, sizeof(buf));
                timefprintf(stderr, "%s: ignore from %s:%d\n", name.c_str(), buf, ntohs(addr.sin_port));
            }
            continue;

        } else if(ret<8) {
            ukncount++;
            message = "small packet";
            scanme = true;
            continue; // skip to next packet
        }
        npkt++;

        const char *hbuf = &rxscratch[0];

        if(hbuf[0]!='P' || hbuf[1]!='S') {
            /* unrecoverable protocol framing error detected! */
            message = "Corrupt packet!";
            scanme = true;
            timefprintf(stderr, "%s: %s\n", name.c_str(), message.c_str());
            continue; // skip to next packet
        }

        epicsUInt16 header = ntohs(*(epicsUInt16*)(hbuf+2));
        epicsUInt32 bodylen = ntohl(*(epicsUInt32*)(hbuf+4));

        if(bodylen>rxscratch.size()-8) {
            // oops, message truncated
            // resize the buffer to catch the next one
            message = "truncated body";
            scanme = true;
            rxscratch.resize(bodylen+8);
            ukncount++;
            if(PSCDebug>2)
                timefprintf(stderr, "%s: truncated body, resize to %lu\n", name.c_str(),
                            (unsigned long)rxscratch.size());
            continue;
        }

        block_map::const_iterator it=recv_blocks.find(header);
        if(it!=recv_blocks.end()) {
            Block& bodyblock = *it->second;
            try {
                bodyblock.rxtime = epicsTime::getCurrent();
            } catch(...) {
                bodyblock.rxtime = epicsTime();
            }
            bodyblock.count++;

            bodyblock.data.assign(hbuf+8, bodylen);

            scanIoRequest(bodyblock.scan);
            bodyblock.listeners(&bodyblock);
        } else {
            ukncount++;
            /* ignore valid, but uninteresting message body */
            if(PSCDebug>2)
                timefprintf(stderr, "%s: ignore message %u\n", name.c_str(), header);
        }
    }

    if(scanme)
        scanIoRequest(scan);

    if(PSCDebug>3)
        timefprintf(stderr, "%s: recv'd %u packets in %u loops\n",
                    name.c_str(), npkt, nloop);
}

void PSCUDP::flushSend()
{
    if(!connected)
        return;
    if(PSCDebug>1)
        timefprintf(stderr, "%s: flush %u -> %u\n",
                    name.c_str(), (unsigned)sendbuf.size(), (unsigned)txbuf.size());

    if(txbuf.size() >= 64u)
        throw std::runtime_error("Sending message would exceed buffer");

    txbuf.splice(txbuf.end(),
                 sendbuf);

    for(block_map::const_iterator it = send_blocks.begin(), end = send_blocks.end();
        it!=end; ++it)
    {
        it->second->queued = false;
    }

    timeval timeout = {5,0};
    if(event_add(evt_tx, &timeout))
        throw std::runtime_error("Failed to add Tx event");
}

void PSCUDP::queueHeader(Block* blk, epicsUInt16 id, epicsUInt32 buflen)
{
    // arbitrary limit on the number of queued packets
    if(sendbuf.size()>=64u)
        throw std::runtime_error("UDP send queue limit exceeded");

    if(readybuf.empty()) {
        sendbuf.push_back(buffer_t());

    } else {
        // append from free list (reuse buffer_t reservation)
        sendbuf.splice(sendbuf.end(),
                       readybuf,
                       readybuf.begin());
    }

    buffer_t& scratch = sendbuf.back();

    scratch.resize(8u + buflen);

    scratch[0] = 'P';
    scratch[1] = 'S';
    *(epicsUInt16*)(&scratch[2]) = htons(blk->code);
    *(epicsUInt32*)(&scratch[4]) = htonl(buflen);
}

void PSCUDP::queueSend(epicsUInt16 id, const void* buf, epicsUInt32 buflen)
{
    Block *blk = getSend(id);
    queueSend(blk, buf, buflen);
}

void PSCUDP::queueSend(Block* blk, const dbuffer& buf)
{
    queueHeader(blk, blk->code, buf.size());

    buffer_t& scratch = sendbuf.back();
    assert(scratch.size() == 8u + buf.size());

    buf.copyout(&scratch[8], 0, buf.size());

    blk->queued = true;
    blk->count++;

    if(PSCDebug>1)
        timefprintf(stderr, "%s: enqueued block %u %lu bytes\n",
                name.c_str(), blk->code, (unsigned long)buf.size());
}

void PSCUDP::queueSend(Block* blk, const void* buf, epicsUInt32 buflen)
{
    queueHeader(blk, blk->code, buflen);

    buffer_t& scratch = sendbuf.back();
    assert(scratch.size() == 8u + buflen);

    memcpy(&scratch[8], buf, buflen);

    blk->queued = true;
    blk->count++;

    if(PSCDebug>1)
        timefprintf(stderr, "%s: enqueue block %u %lu bytes\n",
                name.c_str(), blk->code, (unsigned long)buflen);
}

void PSCUDP::forceReConnect() {}

