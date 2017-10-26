#!../../bin/linux-x86_64-debug/feedioc

dbLoadDatabase("../../dbd/feedioc.dbd",0,0)
feedioc_registerRecordDeviceDriver(pdbbase) 


epicsEnvSet("EPICS_DB_INCLUDE_PATH", ".:../../db")

dbLoadTemplate("example.substitutions","P=TST:,NAME=device,DEBUG=0xffffffef")

iocInit()

dbpf "TST:ctrl:Addr-SP" "127.0.0.1"
