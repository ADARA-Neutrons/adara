#!/bin/bash

PUT="/home/d3s/epics/bin/linux-x86_64/caput2"
SOFTIOC=/home/d3s/epics/bin/linux-x86_64/softIoc
IOC_CMD_FILE=pvsd_stress.cmd
IOC_DB_FILE=pvsd_stress.db
BEAMLINE_FILE=pvsd_stress.xml
NUM_DEVICES=$1
NUM_PVS=$2
ENVAL=0
IVAL=0
FVAL=0.0
STRVALS[0]="Off"
STRVALS[1]="Low"
STRVALS[2]="High"

function buildIOCDB {
    DEVNO=0

    echo '' > $IOC_DB_FILE

    while [  $DEVNO -lt $NUM_DEVICES ]; do
        let DEVID=DEVNO+1000
        PVID=1
        PVNO=0
        while [  $PVNO -lt $NUM_PVS ]; do
            echo 'record(mbbo,"D'$DEVID'-PV'$PVID'")' >> $IOC_DB_FILE
            echo '{' >> $IOC_DB_FILE
            echo '  field(SDEF,3)' >> $IOC_DB_FILE
            echo '  field(VAL,0)' >> $IOC_DB_FILE
            echo '  field(ZRVL,0)' >> $IOC_DB_FILE
            echo '  field(ONVL,1)' >> $IOC_DB_FILE
            echo '  field(TWVL,2)' >> $IOC_DB_FILE
            echo '  field(ZRST,"Off")' >> $IOC_DB_FILE
            echo '  field(ONST,"Low")' >> $IOC_DB_FILE
            echo '  field(TWST,"High")' >> $IOC_DB_FILE
            echo '  field(PINI,"YES")' >> $IOC_DB_FILE
            echo '}' >> $IOC_DB_FILE

            let PVID=PVID+1

            echo 'record(stringout,"D'$DEVID'-PV'$PVID'")' >> $IOC_DB_FILE
            echo '{' >> $IOC_DB_FILE
            echo '  field(VAL,"Off")' >> $IOC_DB_FILE
            echo '  field(PINI,"YES")' >> $IOC_DB_FILE
            echo '}' >> $IOC_DB_FILE

            let PVID=PVID+1

            echo 'record(longout,"D'$DEVID'-PV'$PVID'")' >> $IOC_DB_FILE
            echo '{' >> $IOC_DB_FILE
            echo '  field(VAL,0)' >> $IOC_DB_FILE
            echo '  field(EGU,"NA")' >> $IOC_DB_FILE
            echo '  field(PINI,"YES")' >> $IOC_DB_FILE
            echo '}' >> $IOC_DB_FILE

            let PVID=PVID+1

            echo 'record(ao,"D'$DEVID'-PV'$PVID'")' >> $IOC_DB_FILE
            echo '{' >> $IOC_DB_FILE
            echo '  field(VAL,4.5)' >> $IOC_DB_FILE
            echo '  field(UDF,1)' >> $IOC_DB_FILE
            echo '  field(EGU,"mm")' >> $IOC_DB_FILE
            echo '  field(PINI,"YES")' >> $IOC_DB_FILE
            echo '}' >> $IOC_DB_FILE

            let PVID=PVID+1
            let PVNO=PVNO+1
        done

        let DEVNO=DEVNO+1
    done
}

function buildSoftIOCCommandFile {
    echo 'epicsEnvSet("ARCH","linux-x86_64")' > $IOC_CMD_FILE
    echo 'epicsEnvSet("IOC","IOC-STRESS")' >> $IOC_CMD_FILE
    echo 'dbLoadRecords("'$IOC_DB_FILE'","")' >> $IOC_CMD_FILE
    echo 'iocInit' >> $IOC_CMD_FILE
}

function buildBeamlineFile {
    echo '<beamline>' > $BEAMLINE_FILE
    echo '  <devices>' >> $BEAMLINE_FILE

    DEVNO=0
    let MAX_PV_ID="($NUM_PVS*4)+1"

    while [  $DEVNO -lt $NUM_DEVICES ]; do
        let DEVID=DEVNO+1000
        PVID=1

        echo '    <device active="true">' >> $BEAMLINE_FILE
        echo '      <name>Device '$DEVID'</name>' >> $BEAMLINE_FILE

        while [  $PVID -lt $MAX_PV_ID ]; do

            echo '        <pv>' >> $BEAMLINE_FILE
            echo '            <name>D'$DEVID'-PV'$PVID'</name>' >> $BEAMLINE_FILE
            echo '            <alias>D'$DEVID'-PV'$PVID'-A</alias>' >> $BEAMLINE_FILE

            sleep 0.3

            echo '            <log/>' >> $BEAMLINE_FILE
            echo '            <scan/>' >> $BEAMLINE_FILE
            echo '        </pv>' >> $BEAMLINE_FILE

            let PVID=PVID+1
        done

        echo '    </device>' >> $BEAMLINE_FILE

        let DEVNO=DEVNO+1
    done

    echo '  </devices>' >> $BEAMLINE_FILE
    echo '</beamline>' >> $BEAMLINE_FILE
}

function startSoftIOC {
  eval $SOFTIOC' -S '$IOC_CMD_FILE' &'
  SIOCPID=$!
}


function stopSoftIOC {
  kill -9 $SIOCPID
}

function startPVSD {
  eval "../pvsd -p 50011 --domain SNS.INST1 -c "$BEAMLINE_FILE" &"
  PVSDPID=$!
}

function stopPVSD {
  kill -9 $PVSDPID
}

function stepPVs {
    let ENVAL="$ENVAL+1"
    if [ $ENVAL -gt 2 ]
    then
        ENVAL=0
    fi

    STRVAL=${STRVALS[$ENVAL]}

    let IVAL="$IVAL+1"
    FVAL=$(bc -l <<< "$FVAL + 0.1")

    DEVNO=0
    let MAX_PV_ID="($NUM_PVS*4)+1"

    while [  $DEVNO -lt $NUM_DEVICES ]; do
        let DEVID=DEVNO+1000
        PVID=1

        #echo 'DEV '$DEVNO

        while [  $PVID -lt $MAX_PV_ID ]; do
            #echo 'PVID '$PVID
            $PUT 'D'$DEVID'-PV'$PVID $ENVAL
            let PVID=PVID+1
            eval $PUT '-s D'$DEVID'-PV'$PVID' '$STRVAL
            let PVID=PVID+1
            $PUT 'D'$DEVID'-PV'$PVID $IVAL
            let PVID=PVID+1
            $PUT 'D'$DEVID'-PV'$PVID $FVAL
            let PVID=PVID+1
        done

        let DEVNO=DEVNO+1
    done

}

function loopPVs {
    RUN=1
    while [ $RUN -eq 1  ]; do
        #echo 'step'
        sleep 1
        stepPVs
    done
}

trap ctrl_c INT

function ctrl_c() {
    echo "Stopping..."
    RUN=0
}

buildIOCDB
buildSoftIOCCommandFile
buildBeamlineFile
startSoftIOC
#startPVSD

loopPVs

#stopPVSD
stopSoftIOC
