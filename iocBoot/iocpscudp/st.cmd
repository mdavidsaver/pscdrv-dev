#!../../bin/linux-x86_64-debug/psc

#< envPaths

## Register all support components
dbLoadDatabase("../../dbd/pscdemo.dbd",0,0)
pscdemo_registerRecordDeviceDriver(pdbbase) 

var("PSCDebug", "5")

# Listen on 0.0.0.0:1234  (pass zero for random port)
# for messages coming from "device" localhost:8765
createPSCUDP("test", "localhost", 8765, 1234)
setPSCSendBlockSize("test", 32, 20)

## Load record instances
dbLoadRecords("../../db/pscudpsim.db","P=TST:")

iocInit()
