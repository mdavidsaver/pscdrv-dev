#!../../bin/linux-x86_64-debug/pscdemo

## Register all support components
dbLoadDatabase("../../dbd/pscdemo.dbd",0,0)
pscdemo_registerRecordDeviceDriver(pdbbase) 

dbLoadRecords("../../db/psc-ctrl.db","P=TST:,NAME=thesim")
dbLoadRecords("sim.db","P=TST:,NAME=thesim")
createPSC("thesim", "localhost", 5678, 1)
setPSCSendBlockSize("thesim", 1, 4)

iocInit()
