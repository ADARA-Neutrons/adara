#!/bin/bash
#
# (Cron) Script to Monitor Local PVSD Executable
#
# Track things like Memory/CPU Usage, as well as Note If/When PVSD Fails!
# (and maybe even notify via email or text message on a crash...! :-)
#

LOG_HOME="/SNS/users/y8y"

host=`hostname`
#echo "host=$host"

host_short=`echo $host | awk -F '.' '{print $1}'`
#echo "host_short=$host_short"

LOGFILE="$LOG_HOME/pvsd_mon/pvsd_mon.$host_short.log"
LOGLOGFILE="$LOG_HOME/pvsd_mon/pvsd_mon.$host_short-log.log"

PVSD_USER="root"
PVSD="pvsd"

PVSD_LOG="/var/log/pvsd.log"

PROG="pvsd_mon"

TIME="/usr/bin/time" # for Test Harness, duh... ;-b

#
# Parse Command Line Options... ;-)
#

do_notify=1

for arg in "$@" ; do

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

	if [ "#$key#" == '#--no_notify#' ]; then
		do_notify=0
	elif [ "#$key#" == '#--pvsd_user#' ]; then
		PVSD_USER="$value"
	elif [ "#$key#" == '#--help#' ]; then
		echo "usage:  pvsd_mon [--no_notify] [--pvsd_user=y8y]"
		exit 0
	fi

done

#
# Get PVSD process status...
#

date=`date`
#echo "date=$date"

status=`ps augxww | grep $PVSD | grep -v -e $PROG -e $TIME -e grep | grep $PVSD_USER`
#echo "status=$status"

if [ "#$status#" == '##' ]; then

	if [ $do_notify == 1 ]; then
		echo \
		"Error on ${host}: PVSD $PVSD Not Running as User $PVSD_USER!"
		exit -1
	else
		# Just Exit Cleanly, Nothing to log here... ;-D
		exit 0
	fi
fi

# Extract statistics of interest

log="$date $host $PVSD as ${PVSD_USER}:\n  "

pid=`echo "$status" | awk '{print $2}'`
#echo "pid=$pid"
log="$log pid=$pid"

cpuP=`echo "$status" | awk '{print $3}'`
#echo "cpuP=$cpuP"
log="$log cpu=${cpuP}%"

memP=`echo "$status" | awk '{print $4}'`
#echo "memP=$memP"
log="$log mem=${memP}%"

vsz=`echo "$status" | awk '{print $5}'`
#echo "vsz=$vsz"
log="$log vsz=${vsz}K"

rss=`echo "$status" | awk '{print $6}'`
#echo "rss=$rss"
log="$log rss=${rss}K"

start=`echo "$status" | awk '{print $9}'`
#echo "start=$start"
log="$log start=$start"

cpuT=`echo "$status" | awk '{print $10}'`
#echo "cpuT=$cpuT"
log="$log time=$cpuT"

#
# Dump PVSD Status Snapshot into Log file...
#

echo -e "$log" >> $LOGFILE

#
# Check on the PVSD Log File size/usage...
#

hour=`echo $date | awk '{print $4}' | awk -F ":" '{print $1}' \
	| sed 's/^0//g'`
#echo "hour=$hour"
min=`echo $date | awk '{print $4}' | awk -F ":" '{print $2}' \
	| sed 's/^0//g'`
#echo "min=$min"

# Only check once or twice per day maybe...?
if [[ ( $hour -eq 10 || $hour -eq 16 ) && $min -eq 0 ]]; then

	log_bytes=`/bin/ls -l $PVSD_LOG | awk '{print $5}'`
	log_hbytes=`/bin/ls -lh $PVSD_LOG | awk '{print $5}'`

	touch $LOGLOGFILE
	echo "$date $host $PVSD_LOG = $log_hbytes" >> $LOGLOGFILE

	pvsd_top_logdir=`echo $PVSD_LOG | awk -F '/' '{print "/"$2}'`

	log_used=`df $PVSD_LOG | tail -1 | awk '{print $(NF-3)}'`
	#echo "log_used=$log_used"
	log_total=`df $PVSD_LOG | tail -1 | awk '{print $(NF-4)}'`
	#echo "log_total=$log_total"

	pct_log=$(( $log_bytes * 100 / ( $log_used * 1000 ) ))
	#echo "pct_log=$pct_log"

	pct_used=$(( $log_used * 100 / $log_total ))
	#echo "pct_used=$pct_used"

	echo \
	"   Log is $log_bytes = $pct_log% of usage, Disk is $pct_used% full." \
		>> $LOGLOGFILE

	# Seemed like a good idea at the time... ;-D
	# if [[ $pct_log -gt 80 ]]; then
	# 	echo "Error on ${host}: PVSD Log is $pct_log% of Usage!"
	# 	echo "   $PVSD_LOG $log_bytes ($log_hbytes)"
	# 	exit -2
	# fi

	if [[ $pct_used -gt 80 ]]; then
		echo "Error on ${host}: $pvsd_top_logdir is $pct_used% Full!"
		echo "   $PVSD_LOG $log_bytes ($log_hbytes)"
		exit -3
	fi
fi

