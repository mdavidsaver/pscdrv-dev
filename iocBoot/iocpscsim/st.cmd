#!../../bin/linux-x86_64-debug/pscdemo

## You may have to change psc to something else
## everywhere it appears in this file

## Register all support components
dbLoadDatabase("../../dbd/pscdemo.dbd",0,0)
pscdemo_registerRecordDeviceDriver(pdbbase) 

## Load record instances
dbLoadRecords("../../db/pscsim.db","P=test:")

var(PSCDebug, 2)

createPSC("test", "localhost", 8765, 1)
setPSCSendBlockSize("test", 1, 20)

iocInit()
