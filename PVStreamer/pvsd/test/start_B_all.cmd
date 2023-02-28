epicsEnvSet("ARCH","linux-x86_64")
epicsEnvSet("IOC","IOC-B")
dbLoadRecords("ioc_B_all.db","")
iocInit
