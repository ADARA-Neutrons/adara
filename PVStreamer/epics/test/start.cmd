epicsEnvSet("ARCH","linux-x86_64")
epicsEnvSet("IOC","mytestioc")
dbLoadRecords("ioc.db","")
iocInit
