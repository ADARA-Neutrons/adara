epicsEnvSet("ARCH","linux-x86_64")
epicsEnvSet("IOC","IOC-B")
dbLoadRecords("ioc_B2_all.db","")
iocInit
