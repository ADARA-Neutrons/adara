epicsEnvSet("ARCH","linux-x86_64")
epicsEnvSet("IOC","IOC-A")
dbLoadRecords("ioc_A_part.db","")
iocInit
