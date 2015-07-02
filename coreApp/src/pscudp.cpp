/*************************************************************************\
* Copyright (c) 2015 Brookhaven Science Assoc. as operator of
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

#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/thread.h>

#define HEADER_SIZE 8

PSCUDP::PSCUDP(const std::string &name,
               const std::string &host,
               unsigned short port,
               unsigned int timeoutmask)
    :PSCBase(name, host, port, mask)
{}

PSCUDP::~PSCUDP() {}

void PSCUDP::senddata(short evt)
{}

void PSCUDP::recvdata(short evt)
{}

void PSCUDP::flushSend() {}
void PSCUDP::queueSend(Block *, const void *, epicsUInt32) {}
void PSCUDP::forceReConnect() {}

