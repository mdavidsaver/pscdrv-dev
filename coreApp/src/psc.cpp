/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include "psc/device.h"

#include <stdexcept>
#include <memory>
#include <cstring>
#include <cerrno>
#include <cstdio>

#include <errlog.h>
#include <dbAccess.h>

#include <event2/buffer.h>
#include <event2/util.h>

#define HEADER_SIZE 8
/* The minimum high-water mark for the receive buffer.
 * Optimization to buffer lots of small messages.
 */
static const size_t min_max_buf_size = 1024*1024;


PSC::PSC(const std::string &name,
         const std::string &host,
         unsigned short port,
         unsigned int timeoutmask)
    :PSCBase(name, host, port, mask)
    ,timer_active(false)
    ,have_head(false)
    ,header(0)
    ,bodylen(0)
    ,bodyblock(NULL)
    ,expect(HEADER_SIZE)
{
    base = EventBase::makeBase();
    event_base *eb = base->get();
    if(!eb)
        throw std::bad_alloc();
    reconnect_timer = evtimer_new(eb, &bev_reconnect, (void*)this);
    dns = evdns_base_new(eb, 1);
    if(!reconnect_timer || !dns || !sendbuf)
        throw std::bad_alloc();
}

PSC::~PSC()
{
}

void PSC::queueSend(Block* blk, const void* buf, epicsUInt32 buflen)
{
    if(!connected)
        return;

    if(blk->queued)
        throw recAlarm();

    char hbuf[HEADER_SIZE];

    hbuf[0] = 'P';
    hbuf[1] = 'S';
    *(epicsUInt16*)(hbuf+2) = htons(blk->code);
    *(epicsUInt32*)(hbuf+4) = htonl(buflen);

    if(PSCMaxSendBuffer>0 &&
       evbuffer_get_length(sendbuf)>=(size_t)PSCMaxSendBuffer)
        throw std::runtime_error("Enqueuing message would exceed buffer");

    if(evbuffer_expand(sendbuf, HEADER_SIZE+buflen))
        throw std::runtime_error("Unable to enqueue message.  Insufficient memory.");

    int err = evbuffer_add(sendbuf, hbuf, HEADER_SIZE);

    if(!err)
        err = evbuffer_add(sendbuf, buf, buflen);

    // calling evbuffer_expand should ensure the adds never fail
    assert(!err);

    blk->queued = true;
    blk->count++;

    if(PSCDebug>1)
        timefprintf(stderr, "%s: enqueue block %u %lu bytes\n",
                name.c_str(), blk->code, (unsigned long)buflen);
}

/* move contents of send queue to socket send buffer. (aka. actually send) */
void PSC::flushSend()
{
    if(!connected)
        return;
    if(PSCDebug>1)
        timefprintf(stderr, "%s: flush\n", name.c_str());

    BEVGuard g(session);
    evbuffer *tx = bufferevent_get_output(session);

    if(PSCMaxSendBuffer>0 &&
       evbuffer_get_length(tx)>=(size_t)PSCMaxSendBuffer)
        throw std::runtime_error("Sending message would exceed buffer");

    if(evbuffer_add_buffer(tx, sendbuf)) {
        evbuffer_drain(sendbuf, evbuffer_get_length(sendbuf));
        throw std::runtime_error("Unable to send messages!");
    }

    for(block_map::const_iterator it = send_blocks.begin(), end = send_blocks.end();
        it!=end; ++it)
    {
        it->second->queued = false;
    }
}

void PSC::forceReConnect()
{
    if(!connected)
        return;
    if(PSCDebug>1)
        timefprintf(stderr, "%s: force reconnection\n", name.c_str());
    start_reconnect();
}

/* start a new connection */
void PSC::connect()
{
    assert(!connected);
    assert(!session);
    assert(!timer_active);

    have_head = false;
    header = 0;
    bodylen = 0;
    bodyblock = NULL;
    expect = HEADER_SIZE;

    session = bufferevent_socket_new(base->get(), -1,
                                     BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE|
                                     BEV_OPT_DEFER_CALLBACKS|BEV_OPT_UNLOCK_CALLBACKS);
    if(!session)
        throw std::bad_alloc();

    bufferevent_setcb(session, &bev_datacb, NULL, &bev_eventcb, (void*)this);

    if(PSCInactivityTime>0) {
        timeval timo = {0,0};
        int ret = 0;
        timo.tv_sec = PSCInactivityTime;

        if(mask&1)
            ret=bufferevent_set_timeouts(session, &timo, &timo);
        else
            ret=bufferevent_set_timeouts(session, NULL, &timo);
        if(ret) {
            timefprintf(stderr, "%s: Error setting timeout! %d\n", name.c_str(), ret);
        } else if(PSCDebug>0) {
            timefprintf(stderr, "%s: will timeout on: send%s\n", name.c_str(),
                    mask&1?" and recv":"");
        }
    }

    bufferevent_setwatermark(session, EV_READ, expect, min_max_buf_size);

    if(bufferevent_socket_connect_hostname(session, dns, AF_UNSPEC, host.c_str(), port))
    {
        bufferevent_free(session);
        session = NULL;
        timeval timo = {5,0};
        evtimer_add(reconnect_timer, &timo);
        timer_active = true;
        message = "Failed to initiate connection.";
    } else
        message = "Connecting...";
    if(PSCDebug>0)
        timefprintf(stderr, "%s: %s\n", name.c_str(), message.c_str());
    scanIoRequest(scan);
}

/* close the socket and initiate a reconnect.
 * Called for socket and protocol errors.
 */
void PSC::start_reconnect()
{
    assert(session && !timer_active);

    bufferevent_free(session);
    session = NULL;

    timeval timo = {5,0};
    evtimer_add(reconnect_timer, &timo);

    connected = false;
    timer_active = true;
}

/* entry point for re-connect timer */
void PSC::reconnect()
{
    assert(!connected);
    assert(!session);
    timer_active = false;

    connect();
    assert(session || timer_active);
}

/* final shutdown and cleanup */
void PSC::stop()
{
    Guard g(lock);
    if(connected) {
        assert(session);
        bufferevent_free(session);
    }
    session=NULL;
    if(timer_active) {
        /*TODO: possible race if timer has expired, but callback hasn't run */
        evtimer_del(reconnect_timer);
    }
    timer_active = false;
    if(PSCDebug>1)
        timefprintf(stderr, "%s: stop\n", name.c_str());
}

void PSC::eventcb(short events)
{
    if(events&BEV_EVENT_CONNECTED)
    {
        bufferevent_enable(session, EV_WRITE|EV_READ);

        connected = true;
        message = "Connected";
        conncount++;

    } else if(events&(BEV_EVENT_ERROR|BEV_EVENT_EOF|BEV_EVENT_TIMEOUT))
    {
        std::string msg;
        if(events&BEV_EVENT_ERROR) {
            msg = "Socket Error: ";
            msg += evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
        } else if (events&BEV_EVENT_TIMEOUT) {
            if (connected) {
                msg.clear();
                if (events&BEV_EVENT_WRITING) msg += "TX ";
                if (events&BEV_EVENT_READING) msg += "RX ";
                msg += "Data Timeout";
            } else
                msg = "Timeout while connecting";
        } else
            msg = "Connection closed by PSC";

        start_reconnect();

        message = msg;
    } else {
        timefprintf(stderr, "%s: eventcb %04x\n", name.c_str(), events);
        return;
    }

    if(PSCDebug>0)
        timefprintf(stderr, "%s: %s\n", name.c_str(), message.c_str());
    scanIoRequest(scan);
}

void PSC::recvdata()
{
    assert(connected && session);

    evbuffer *buf = bufferevent_get_input(session);

    /* remove messages in buffer as long as there are enough bytes
     * for the next stage of processing.
     */
    size_t nbytes;
    while((nbytes=evbuffer_get_length(buf)) >= expect)
    {

        if(!have_head) { /* decode header */
            assert(expect==HEADER_SIZE);

            char hbuf[HEADER_SIZE];

            evbuffer_remove(buf, &hbuf, sizeof(hbuf));

            if(hbuf[0]!='P' || hbuf[1]!='S') {
                /* unrecoverable protocol framing error detected! */
                start_reconnect();
                message = "Framing error!";
                timefprintf(stderr, "%s: %s\n", name.c_str(), message.c_str());
                scanIoRequest(scan);
                return;
            }

            header = ntohs(*(epicsUInt16*)(hbuf+2));
            bodylen = ntohl(*(epicsUInt32*)(hbuf+4));
            have_head = true;

            block_map::const_iterator it=recv_blocks.find(header);
            if(it!=recv_blocks.end()) {
                bodyblock = it->second;
                try {
                    bodyblock->rxtime = epicsTime::getCurrent();
                } catch(...) {
                    bodyblock->rxtime = epicsTime();
                }
                bodyblock->count++;
            } else {
                bodyblock = NULL;
                ukncount++;
            }

            if(PSCDebug>2)
                timefprintf(stderr, "%s: expect block %u with %lu bytes\n",
                        name.c_str(), header, (unsigned long)bodylen);

            expect = bodylen;

        } else { /* decode body */

            if(PSCDebug>2)
                timefprintf(stderr, "%s: recv'd block %u with %lu bytes\n",
                        name.c_str(), header, (unsigned long)bodylen);

            if(bodyblock) {
                if(PSCDebug>2)
                    timefprintf(stderr, "%s: Process message %u\n", name.c_str(), header);

                bodyblock->data.reserve(bodylen);
                bodyblock->data.resize(bodylen);

                evbuffer_remove(buf, &bodyblock->data[0], bodylen);
                scanIoRequest(bodyblock->scan);
                bodyblock->listeners(bodyblock);

            } else {
                /* ignore valid, but uninteresting message body */
                if(PSCDebug>2)
                    timefprintf(stderr, "%s: ignore message %u\n", name.c_str(), header);
                evbuffer_drain(buf, bodylen);
            }

            have_head = false;
            bodyblock = NULL;
            expect = HEADER_SIZE;
        }

        /* must have made some progress */
        assert(nbytes > evbuffer_get_length(buf));
    }

    /* at this point evbuffer_get_length(buf) < expect */

    if(PSCDebug>2)
        timefprintf(stderr, "Wait for %lu bytes\n", (unsigned long)expect);
    bufferevent_setwatermark(session, EV_READ, expect,
                             expect >= min_max_buf_size ? expect+1 : min_max_buf_size);
}

void PSC::report(int lvl)
{
    printf(" Last msg : %s\n", lastMessage().c_str());
    printf(" Decode   : Header:%s %u %u\n",
           have_head?"Yes":"No", header, bodylen);
    printf(" Expecting: %lu bytes\n", (unsigned long)expect);
    if(lvl>=2) {
        if(isConnected()){
            size_t tx, rx;
            {
                BEVGuard H(session);
                tx = evbuffer_get_length(bufferevent_get_output(session));
                rx = evbuffer_get_length(bufferevent_get_input(session));
            }
            printf(" Buffers  : Tx:%lu Rx: %lu\n",
                   (unsigned long)tx, (unsigned long)rx);
        }
    }
}
