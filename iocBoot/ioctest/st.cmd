#!../../bin/linux-x86_64-debug/feedioc

dbLoadDatabase("../../dbd/feedioc.dbd",0,0)
feedioc_registerRecordDeviceDriver(pdbbase) 

var(feedNumInFlight, 1)

epicsEnvSet("EPICS_DB_INCLUDE_PATH", ".:../../db")

dbLoadRecords("feed_base.template","PREF=TST:ctrl:,NAME=device,DEBUG=0")
dbLoadRecords("bitmonitor.db", "PREF=TST:one,NAME=device,REG=one")

iocInit()

dbl > records.dbl

dbpf "TST:ctrl:Addr-SP" "127.0.0.1"
