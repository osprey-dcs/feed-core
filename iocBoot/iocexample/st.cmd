#!../../bin/linux-x86_64-debug/feedioc

dbLoadDatabase("../../dbd/feedioc.dbd",0,0)
feedioc_registerRecordDeviceDriver(pdbbase) 


dbLoadRecords("../../db/feed_base.template","P=TST:,NAME=device,DEBUG=0xffffffef")
dbLoadRecords("example.db","P=TST:,NAME=device")

iocInit()

dbpf "TST:Addr-SP" "127.0.0.1"
