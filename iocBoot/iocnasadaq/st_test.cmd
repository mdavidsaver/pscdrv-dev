#!../../bin/linux-x86_64/pscdemo

dbLoadDatabase("../../dbd/pscdemo.dbd",0,0)
pscdemo_registerRecordDeviceDriver(pdbbase) 

# local port to which UDP stream will be sent
epicsEnvSet("STRM_PORT", "9878")

var(PSCDebug, 5)        #5 full debug
var(PSCUDPMaxLenMB, 20000)

# sudo sysctl net.core.rmem_max=3407872
#var(PSCUDPSetSockBuf, 3407872)

createPSCUDP("control", "127.0.0.1", 9876, 0)
setPSCSendBlockSize("control", 16, 12) # IP + port + chan_mask

createPSCUDPFast("data", "127.0.0.1", 9877, $(STRM_PORT))

dbLoadRecords("../../db/psc-ctrl.db", "NAME=control,P=TST:CTRL:")
dbLoadRecords("../../db/psc-ctrl.db", "NAME=data,P=TST:DATA:")
dbLoadRecords("../../db/pscudpfast.db", "NAME=data,P=TST:DATA:")
dbLoadRecords("nasadaq.db", "CTRL=control,P=TST:,STRM_PORT=$(STRM_PORT)")

iocInit()

