TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ifneq ($(LWIP),)
ifneq ($(LWIP_CONTRIB),)

LWIPDIR=$(LWIP)/src
LWIPARCH=$(LWIP_CONTRIB)/ports/unix


TESTPROD_HOST += testpscmsg
testpscmsg_SRCS += testpscmsg.c
testpscmsg_LIBS += Com
TESTS += testpscmsg

PROD_HOST += pscapp
USR_CFLAGS += -I$(LWIPDIR)/include -I$(LWIPARCH)/include -I$(LWIP_CONTRIB)/apps/snmp_private_mib
pscapp_SYS_LIBS += pthread

pscapp_SRCS += pscapp.c
pscapp_SRCS += listener.c
pscapp_SRCS += pscmsg.c

# LWIP Core
pscapp_SRCS += def.c dhcp.c dns.c inet_chksum.c init.c mem.c memp.c netif.c pbuf.c raw.c stats.c sys.c
pscapp_SRCS += tcp.c tcp_in.c tcp_out.c timers.c udp.c
# LWIP IPv4
pscapp_SRCS += autoip.c icmp.c igmp.c ip_frag.c ip4.c ip4_addr.c
pscapp_SRCS += etharp.c sys_arch.c
# SNMP
pscapp_SRCS += asn1_dec.c asn1_enc.c mib2.c mib_structs.c msg_in.c msg_out.c
pscapp_SRCS += lwip_prvmib.c
# Sockets API
pscapp_SRCS += api_lib.c api_msg.c err.c netbuf.c netdb.c netifapi.c sockets.c tcpip.c

pscapp_SRCS += tapif.c

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================
endif
endif

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

ifneq ($(LWIP),)
ifneq ($(LWIP_CONTRIB),)
vpath %.c $(LWIPDIR)/core $(LWIPDIR)/core/ipv4 $(LWIPDIR)/core/snmp $(LWIPDIR)/netif $(LWIPDIR)/api
vpath %.c $(LWIPARCH) $(LWIPARCH)/netif $(LWIP_CONTRIB)/apps/snmp_private_mib
endif
endif
