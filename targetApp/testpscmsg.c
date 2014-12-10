/*************************************************************************\
* Copyright (c) 2014 Brookhaven Science Assoc. as operator of
      Brookhaven National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <epicsUnitTest.h>
#include <testMain.h>

#include "pscmsg.h"

static void testMsg(void)
{
    char inbuf[128];
    char outbuf[128];
    int sock[2];
    uint16_t mid;
    uint32_t mlen;

    testDiag("Test messaging");

    if(socketpair(AF_UNIX, SOCK_STREAM,0,sock)!=0)
        testAbort("socketpair fails %d", errno);

    memset(inbuf, 0, sizeof(inbuf));

    testDiag("Test raw send/recv");

    testOk1(strcpy(outbuf, "hello")!=NULL);
    testOk1(psc_sendall(sock[0], outbuf, 6, 0)==0);
    testOk1(psc_recvall(sock[1], inbuf, 6, 0)==0);
    testOk1(strcmp("hello", inbuf)==0);

    testOk1(strcpy(outbuf, "testing")!=NULL);

    testDiag("Test send/recv message");

    testOk1(psc_sendmsg(sock[0], 42, outbuf, 8, 0)==0);

    mid = 0;
    mlen = sizeof(inbuf);
    memset(inbuf, 0, sizeof(inbuf));

    testOk1(psc_recvmsg(sock[1], &mid, inbuf, &mlen, 0)==0);
    testOk1(mid==42);
    testOk1(mlen==8);
    testOk1(strcmp("testing", inbuf)==0);

    close(sock[0]);
    close(sock[1]);
}

static void testMsgTrucn(void)
{
    char inbuf[128];
    char outbuf[128];
    int sock[2];
    uint16_t mid;
    uint32_t mlen;

    testDiag("Test message truncation");

    if(socketpair(AF_UNIX, SOCK_STREAM,0,sock)!=0)
        testAbort("socketpair fails %d", errno);

    memset(inbuf, 0, sizeof(inbuf));
    testOk1(strcpy(outbuf, "hello")!=NULL);

    testOk1(psc_sendmsg(sock[0], 43, outbuf, 6, 0)==0);

    mid = 0;
    mlen = 4;
    testOk1(psc_recvmsg(sock[1], &mid, inbuf, &mlen, 0)==0);
    testOk1(mid==43);
    testOk1(mlen==4);
    testOk1(strcmp("hell", inbuf)==0);

    close(sock[0]);
    close(sock[1]);

}

static void testMsgFail(void)
{
    char inbuf[128];
    char outbuf[] = "PX\x00\x10\x00\x00\x00\x06hello\x00";
    int sock[2];
    uint16_t mid;
    uint32_t mlen;

    testDiag("Test message framing errors");

    if(socketpair(AF_UNIX, SOCK_STREAM,0,sock)!=0)
        testAbort("socketpair fails %d", errno);

    testOk1(psc_sendall(sock[0], outbuf, sizeof(outbuf), 0)==0);

    mid = 0;
    mlen = 4;
    testOk1(psc_recvmsg(sock[1], &mid, inbuf, &mlen, 0)==EIO);

    close(sock[0]);
    close(sock[1]);
}

MAIN(testpscmsg) {
    testPlan(0);
    testMsg();
    testMsgTrucn();
    testMsgFail();
    return testDone();
}

#define BUILD_HOST
#include "pscmsg.c"
