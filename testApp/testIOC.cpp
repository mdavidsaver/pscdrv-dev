/*************************************************************************\
* Copyright (c) 2025 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <map>

#include <osiSock.h>

#include <testMain.h>
#include <epicsUnitTest.h>
#include <dbUnitTest.h>
#include <dbAccess.h>

#include <event2/listener.h>

#include <psc/device.h>

namespace {
struct TestMonitor {
    testMonitor * const mon;
    TestMonitor(const char* pvname, unsigned mask=DBE_VALUE|DBE_ALARM, unsigned opt=0)
        :mon(testMonitorCreate(pvname, mask, opt))
    {
        if(!mon)
            throw std::runtime_error("testMonitorCreate");
    }
    ~TestMonitor() {
        testMonitorDestroy(mon);
    }
    void wait() const {
        testMonitorWait(mon);
    }
};


template<typename C>
struct cleaner {};
template<>
struct cleaner<bufferevent> {
    void operator()(bufferevent *bev) const {
        bufferevent_free(bev);
    }
};
template<>
struct cleaner<evconnlistener> {
    void operator()(evconnlistener *ev) const {
        evconnlistener_free(ev);
    }
};

template<typename C>
using clean_ptr = std::unique_ptr<C, cleaner<C>>;

struct Server;
struct Session {
    Server *serv;
    clean_ptr<bufferevent> sock;
    struct {
        char p, s;
        epicsUInt16 msgid;
        epicsUInt32 msglen;
    } head = {};
    STATIC_ASSERT(sizeof(head)==8);

    void wait_header() {
        testDiag("Test session waiting for header");
        bufferevent_setcb(sock.get(), &have_head, NULL, on_event, this);
        bufferevent_setwatermark(sock.get(), EV_READ, sizeof(head), 0);
    }

    bool operator<(const Session& o) const {
        return sock.get() < o.sock.get();
    }

    static
    void have_head(struct bufferevent *bev, void *ctx) noexcept {
        auto self(reinterpret_cast<Session*>(ctx));
        auto inp(bufferevent_get_input(bev));
        testDiag("Test session %s with %zu", __func__, evbuffer_get_length(inp));

        auto n = evbuffer_remove(inp, &self->head, sizeof(head));
        assert(n==sizeof(head));

        if(self->head.p!='P' || self->head.s!='S') {
            on_event(bev, BEV_EVENT_ERROR, ctx);
            return;
        }
        self->head.msgid = ntohs(self->head.msgid);
        self->head.msglen = ntohl(self->head.msglen);
        testDiag("Test session rx msgid=%u msglen=%u", self->head.msgid, self->head.msglen);

        if(evbuffer_get_length(inp) < self->head.msglen) {
            bufferevent_setcb(self->sock.get(), &have_body, NULL, &on_event, self);
            bufferevent_setwatermark(self->sock.get(), EV_READ, self->head.msglen, 0);
        } else {
            have_body(bev, ctx);
        }
    }

    static
    void have_body(struct bufferevent *bev, void *ctx) noexcept {
        testDiag("Test session %s", __func__);
        auto self(reinterpret_cast<Session*>(ctx));
        auto inp(bufferevent_get_input(bev));
        auto out(bufferevent_get_output(bev));

        auto msglen(self->head.msglen);
        self->head.msgid += 10; // echo back with different ID
        self->head.msgid = htons(self->head.msgid);
        self->head.msglen = htonl(msglen);

        int err;
        err = evbuffer_add(out, &self->head, sizeof(self->head));
        assert(err==0);
        err |= evbuffer_remove_buffer(inp, out, msglen);
        assert(err==msglen);

        self->wait_header();
    }

    static
    void on_event(struct bufferevent *bev, short what, void *ctx) noexcept;
};

struct Server {
    std::map<bufferevent*, Session> clients;
};

void Session::on_event(bufferevent *bev, short what, void *ctx) noexcept
{
    testDiag("Test session event 0x%x", what);
    auto self(reinterpret_cast<Session*>(ctx));
    if(what&(BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
        self->serv->clients.erase(bev);
        // self and bev deleted
    }
}

void new_conn(struct evconnlistener *evl, evutil_socket_t sock, struct sockaddr *peer, int socklen, void *pvt) noexcept
{
    testDiag("Test session connects");
    auto serv(reinterpret_cast<Server*>(pvt));
    auto base = evconnlistener_get_base(evl);

    auto bev(bufferevent_socket_new(base, sock, BEV_OPT_CLOSE_ON_FREE));
    assert(bev);
    auto it(serv->clients.emplace(bev, Session()));
    assert(it.second);
    auto& sess = it.first->second;
    sess.serv = serv;
    sess.sock.reset(bev);

    sess.wait_header();
    bufferevent_enable(bev, EV_READ|EV_WRITE);
}

} // namespace

extern "C"
void testIOC_registerRecordDeviceDriver(struct dbBase*);

MAIN(testIOC)
{
    testPlan(5);
    testdbPrepare();

    testdbReadDatabase("testIOC.dbd", NULL, NULL);
    testIOC_registerRecordDeviceDriver(pdbbase);

    testIOC_registerRecordDeviceDriver(pdbbase);

    PSCDebug = 5;

    {
        osiSockAddr addr = {};
        addr.ia.sin_family = AF_INET;
        addr.ia.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        Server serv;
        auto server_base(EventBase::makeBase());
        clean_ptr<evconnlistener> sockl(evconnlistener_new_bind(server_base->get(),
                                                                &new_conn, &serv,
                                                                LEV_OPT_THREADSAFE,
                                                                1,
                                                                &addr.sa, sizeof(addr)));

        {
            osiSocklen_t slen = sizeof(addr);
            testOk1(0==getsockname(evconnlistener_get_fd(sockl.get()), &addr.sa, &slen));
        }
        testDiag("Listening on port %u", ntohs(addr.ia.sin_port));

        PSC C("dev", "127.0.0.1", ntohs(addr.ia.sin_port), 3);

        testdbReadDatabase("psc-ctrl.db", ".:../../db", "P=tst:,NAME=dev");
        testdbReadDatabase("testioc.db", ".:..", "P=tst:,NAME=dev");

        eltc(0);
        testIocInitOk();
        eltc(1);
        {
            TestMonitor M("tst:Conn-Sts"); // TODO: arm earlier
            testdbGetFieldEqual("tst:Conn-Sts", DBF_LONG, 1);
        }
        {
            TestMonitor M("tst:hear");

            testdbPutFieldOk("tst:say", DBF_STRING, "Testing...");
            testdbPutFieldOk("tst:Send-Cmd", DBF_LONG, 1);
            M.wait();
            testdbGetFieldEqual("tst:hear", DBF_STRING, "Testing...");
        }

        testIocShutdownOk();
    }

    testdbCleanup();
    return testDone();
}
