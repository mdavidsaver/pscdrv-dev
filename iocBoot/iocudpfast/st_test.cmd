#!./pscdrv/bin/linux-x86_64/pscdemo

## You may have to change psc to something else
## everywhere it appears in this file

## Register all support components
dbLoadDatabase("../../dbd/pscdemo.dbd",0,0)
pscdemo_registerRecordDeviceDriver(pdbbase) 

var(PSCDebug, 2)        #5 full debug
var(PSCUDPMaxLenMB, 20000)

# sudo sysctl net.core.rmem_max=3407872
var(PSCUDPSetSockBuf, 3407872)

createPSCUDPFast("test", "192.168.20.101", 8765, 8764)

dbLoadRecords("../../db/psc-ctrl.db", "NAME=test,P=TST:")
dbLoadRecords("../../db/pscudpfast.db", "NAME=test,P=TST:")
dbLoadRecords("test.db", "NAME=test,P=TST:")

iocInit()

