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

#set_savefile_path("$(PWD)/as","/save/")
#set_requestfile_path("$(PWD)/as","/req/")
#system "install -m 744 -d $(PWD)/as/save"
#system "install -m 744 -d $(PWD)/as/req"

#set_pass0_restoreFile("settings.sav")

iocInit()

#makeAutosaveFileFromDbInfo("$(PWD)/as/req/settings.req", "autosaveFields_pass0")
#create_monitor_set("settings.req", 10 , "")
