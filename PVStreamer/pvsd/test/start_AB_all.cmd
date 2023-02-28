epicsEnvSet("ARCH","linux-x86_64")
epicsEnvSet("IOC","IOC-AB")
dbLoadRecords("ioc_AB_all.db","")
iocInit
