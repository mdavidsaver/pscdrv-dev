#!../../bin/linux-x86_64-debug/psc

#< envPaths

## Register all support components
dbLoadDatabase("../../dbd/psc.dbd",0,0)
psc_registerRecordDeviceDriver(pdbbase) 

#var("PSCDebug", "5")

# Listen on 0.0.0.0:8764
# for messages coming from localhost:8765
createPSCUDP("test", "localhost", 8765, 8764)

## Load record instances
dbLoadRecords("../../db/pscudpsim.db","P=TST:")

iocInit()
