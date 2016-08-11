#!../../bin/linux-x86/psc

## You may have to change psc to something else
## everywhere it appears in this file

## Register all support components
dbLoadDatabase("../../dbd/psc.dbd",0,0)
psc_registerRecordDeviceDriver(pdbbase) 

## Load record instances
dbLoadRecords("../../db/pscsim.db","P=test:")

var(PSCDebug, 2)

createPSC("test", "localhost", 8765, 1)
setPSCSendBlockSize("test", 1, 20)

iocInit()
