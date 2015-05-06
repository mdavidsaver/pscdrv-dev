#!../../bin/linux-x86_64/psc

## You may have to change psc to something else
## everywhere it appears in this file

## Register all support components
dbLoadDatabase("../../dbd/psc.dbd",0,0)
psc_registerRecordDeviceDriver(pdbbase) 

## Load record instances
dbLoadRecords("../../db/xbpm.db","P=test:,DEV=xbpm1")

var(PSCDebug, 2)

createPSC("xbpm1", "localhost", 5678, 1)

iocInit()
