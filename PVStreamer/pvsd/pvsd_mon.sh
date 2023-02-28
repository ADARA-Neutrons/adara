#!/bin/bash
#
# (Cron) Script to Monitor Local PVSD Executable
#
# Track things like Memory/CPU Usage, as well as Note If/When PVSD Fails!
# (and maybe even notify via email or text message on a crash...! :-)
#
# Also Check AdaraMonitor Status PVs while we're at it... ;-D
#

# Source Controls Environment (for ${BL} env setting):
if [ -e /etc/profile.d/controls.sh ] ; then
	source /etc/profile.d/controls.sh
fi

ADARA_MONITOR_PVS="${BL}:CS:Adara:PVStreamer \
	${BL}:CS:Adara:DASMon \
	${BL}:CS:Adara:MonitorPVs \
	${BL}:CS:Adara:Workflow \
	${BL}:CS:Adara:Catalog \
	${BL}:CS:Adara:Reduction \
	${BL}:CS:Adara:Stat"

#LOG_HOME="/SNS/users/$USER"
LOG_HOME="$HOME"

# Handle Scenario Where ${LOG_HOME} Directory is Missing/Unmounted...!
# -> as needed, just write to /tmp until the Directory Mount returns...
# --> this will ensure we can still do Proper Error Counting...! ;-D
if [[ ! -d ${LOG_HOME} ]]; then
	LOG_HOME="/tmp"
	USING_ALT_LOG_HOME="\n\n[Note: Using Alternate \${LOG_HOME\} ="
	USING_ALT_LOG_HOME="${USING_ALT_LOG_HOME} [${LOG_HOME}]"
else
	USING_ALT_LOG_HOME=""
fi

host=`hostname`
#echo "host=$host"

host_short=`echo $host | awk -F '.' '{print $1}'`
#echo "host_short=$host_short"

PROG="pvsd_mon"

LOG_DIR="${PROG}"

LOGFILE="${LOG_HOME}/${LOG_DIR}/${PROG}.$host_short.log"
LOGLOGFILE="${LOG_HOME}/${LOG_DIR}/${PROG}.$host_short-log.log"

PVSD_USER="root"
PVSD="pvsd"

PVSD_LOG="/var/log/pvsd.log"

TIME="/usr/bin/time" # for Test Harness, duh... ;-b

WAIT="-w 9" # for caget wait... ;-)

S="[[:space:]]"

# The Minute When We Gasp Our Dying Breath and Beg for Help... ;-D
SOS_MIN=0

NL=`date`
NL="${NL}\n\n"

#
# Parse Command Line Options... ;-)
#

SKIP_PVS=0

do_notify=1

for arg in "$@" ; do

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

	if [ "#$key#" == '#--no_notify#' ]; then
		do_notify=0
	elif [ "#$key#" == '#--skip_pvs#' ]; then
		SKIP_PVS=1
	elif [ "#$key#" == '#--pvsd_user#' ]; then
		PVSD_USER="$value"
	elif [ "#$key#" == '#--help#' ]; then
		echo "usage:  ${PROG} [--no_notify] [--skip_pvs] \\"
		echo "             [--pvsd_user=$USER]"
		exit 0
	fi

done

# Get Date (and Hours/Minutes for Sub-Schedule Control...)

date=`date`
#echo "date=$date"

hour=`echo $date | awk '{print $4}' | awk -F ":" '{print $1}' \
	| sed 's/^0//g'`
#echo "hour=$hour"
min=`echo $date | awk '{print $4}' | awk -F ":" '{print $2}' \
	| sed 's/^0//g'`
#echo "min=$min"

# If Log Sub-Directory Doesn't Exist, Try to Create It... ;-D
if [[ ! -d "${LOG_HOME}/${LOG_DIR}" ]]; then

	ckd=`mkdir -p "${LOG_HOME}/${LOG_DIR}" 2>&1`

	# Only Whine Once Per Hour If There's No Log Sub-Directory
	# (And We Can't Create It...)
	if [[ $? != 0 && $min -eq ${SOS_MIN} ]]; then
		echo -e "${NL}Error Creating PVSD Monitor Log Sub-Directory!"
		echo -e "\n${ckd}"
		NL="\n"
	fi

fi

# If Log File Doesn't Exist, Try to Create It... ;-D
if [[ ! -e "${LOGFILE}" ]]; then

	ckt=`touch "${LOGFILE}" 2>&1`

	# Only Whine Once Per Hour If There's No Log File
	# (And We Can't Create It...)
	if [[ $? != 0 && $min -eq ${SOS_MIN} ]]; then
		echo -e "${NL}Error Creating PVSD Monitor Log File!"
		echo -e "\n${ckt}"
		NL="\n"
	fi

fi

# Track Any Error Count Already Retrieved for This Invocation...
ERROR_COUNT=0
log_error=0

# Check Error Count Embedded in Last Line of Log File...
GET_ERROR_COUNT()
{
	# Only Retrieve Error Count from Log File If Not Already Set...!
	if [[ $ERROR_COUNT == 0 ]]; then
		if [[ -r "${LOGFILE}" ]]; then
			local _last=`tail -1 "${LOGFILE}"`
			if [[ "${_last}" =~ ErrorCount= ]]; then
				ERROR_COUNT=`echo "${_last}" | awk -F "=" '{print $2}'`
			fi
		fi
		# Add 1 for This Invocation's New Error...
		ERROR_COUNT=$(( ERROR_COUNT + 1 ))
	fi
}

# Set Error Count, Append as Last Line of Log File...
SET_ERROR_COUNT()
{
	if [[ -w "${LOGFILE}" ]]; then

		echo "ErrorCount=${ERROR_COUNT}" >> ${LOGFILE}

	# Only Whine Once Per Hour If We've Become
	# Disconnected from the Universe...
	elif [[ $min -eq ${SOS_MIN} ]]; then
		echo -e -n "${NL}Error Writing Error Count (${ERROR_COUNT})"
		echo " to PVSD Monitor Log File!"
		echo -e "\n${ckt}"
		NL="\n"
	fi
}

# We've Just Encountered an Error...!
# Implement Exponential Error Reporting Fall Off...
# (Based on Cron Job Check Every 10 Minutes)
CHECK_ERROR_REPORTING()
{
	local _do_report=1

	# Check for Any Saved "Error Count" from Last Invocation...
	GET_ERROR_COUNT

	# After First 3 Days, Report Once Per Week...
	if [[ ${ERROR_COUNT} -gt 433 ]]; then
		if [[ $(( ( ERROR_COUNT - 1 ) % 1008 )) != 0 ]]; then
			_do_report=0
		fi

	# After First 3 Hours, Report Once Per Day...
	elif [[ ${ERROR_COUNT} -gt 19 ]]; then
		if [[ $(( ( ERROR_COUNT - 1 ) % 144 )) != 0 ]]; then
			_do_report=0
		fi

	# Report First 3 Occurrences in a Row, Then Once Per Hour...
	elif [[ ${ERROR_COUNT} -gt 3 ]]; then
		if [[ $(( ( ERROR_COUNT - 1 ) % 6 )) != 0 ]]; then
			_do_report=0
		fi
	fi

	if [[ ${_do_report} == 1 ]]; then
		log_error=1
	fi

	return ${_do_report}
}

#
# Get PVSD process status...
#

status=`ps augxww | grep $PVSD | grep -v -e $PROG -e $TIME -e grep \
	| grep $PVSD_USER`
#echo "status=$status"

if [ "#$status#" == '##' ]; then

	if [ $do_notify == 1 ]; then

		CHECK_ERROR_REPORTING
		do_report="$?"

		# Report Error...
		if [[ $do_report == 1 ]]; then

			echo -e -n "${NL}Error on ${host}:"
			echo " PVSD $PVSD Not Running as User $PVSD_USER!"
			NL="\n"

		fi

		# Set Error Count for Next Invocation,
		if [[ ${ERROR_COUNT} -gt 0 ]]; then
			SET_ERROR_COUNT
			if [[ ${log_error} != 0 ]]; then
				echo -e "${NL}ErrorCount=${ERROR_COUNT}"
			fi
		fi

		exit -1

	else
		# Just Exit Cleanly, Nothing to log here... ;-D
		exit 0
	fi
fi

#
# Check ADARA Monitor Status PVs...
#

if [[ $do_notify == 1 && ${SKIP_PVS} == 0 ]]; then

	for pv in ${ADARA_MONITOR_PVS} ; do

		#echo "pv=[${pv}]"

		# Try 3 Times to Get the Status PV, as Needed...

		cnt=0

		while [[ $cnt -lt 3 ]]; do

			# Ignore Odd Channel Access
			# "Duplicate EPICS CA Address" Warnings...
			pvstat=`caget ${WAIT} "${pv}" 2>&1`

			# Did We Time Out Connecting to the Status PV...?
			if [[ "$pvstat" =~ Channel${S}connect${S}timed${S}out ]]; then

				CHECK_ERROR_REPORTING
				do_report="$?"

				# Report Error...
				if [[ $do_report == 1 ]]; then
					echo "${NL}Channel Connect Timed Out on: ${pv}..."
					NL="\n"
				fi

				cnt=$(( $cnt + 1 ))

			else
				break
			fi

		done

		if [[ "$pvstat" =~ Channel${S}connect${S}timed${S}out ]]; then

			CHECK_ERROR_REPORTING
			do_report="$?"

			# Report Error...
			if [[ $do_report == 1 ]]; then

				echo -e -n "${NL}Error on ${host}: Timed Out $cnt Times"
				echo -e " on ADARA Monitor Status PV!"
				echo -e "   [${pv}]"
				echo -e "      -> ${pvstat}"
				NL="\n"

				caget ${WAIT} "${pv}"

			fi

		else

			# Strip Off Actual PV Status Value...
			pvstat=`echo "${pvstat}" \
				| grep -v "Warning" | awk '{print $2}'`

			if [[ "${pvstat}" != "OK" ]]; then

				CHECK_ERROR_REPORTING
				do_report="$?"

				# Report Error...
				if [[ $do_report == 1 ]]; then

					echo -e -n "${NL}Error on ${host}:"
					echo -e " ADARA Monitor Status Not OK!"
					echo -e "   [${pv}] -> ${pvstat}"
					NL="\n"

					caget ${WAIT} "${pv}"

				fi

			fi

		fi

	done

fi

#
# Extract & log statistics of interest
#

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

if [[ -w "${LOGFILE}" ]]; then

	echo -e "$log" >> ${LOGFILE}

# Only Whine Once Per Hour If We've Become Disconnected from the Universe...
elif [[ $min -eq ${SOS_MIN} ]]; then

	echo -e "${NL}Error: PVSD Monitor Log File Unwritable!"
	echo -e "\n$log"
	NL="\n"

fi

#
# Check on the PVSD Log File size/usage...
#

# Only check once or twice per day maybe...?
if [[ ( $hour -eq 10 || $hour -eq 16 ) && $min -eq 0 ]]; then

	log_bytes=`/bin/ls -l $PVSD_LOG | awk '{print $5}'`
	log_hbytes=`/bin/ls -lh $PVSD_LOG | awk '{print $5}'`

	pvsd_top_logdir=`echo $PVSD_LOG | awk -F '/' '{print "/"$2}'`

	log_used=`df $PVSD_LOG | tail -1 | awk '{print $(NF-3)}'`
	#echo "log_used=$log_used"
	log_total=`df $PVSD_LOG | tail -1 | awk '{print $(NF-4)}'`
	#echo "log_total=$log_total"

	pct_log=$(( $log_bytes * 100 / ( $log_used * 1000 ) ))
	#echo "pct_log=$pct_log"

	pct_used=$(( $log_used * 100 / $log_total ))
	#echo "pct_used=$pct_used"

	# If Log Log File Doesn't Exist, Try to Create It... ;-D
	if [[ ! -e "${LOGLOGFILE}" ]]; then

		ckt=`touch "${LOGLOGFILE}" 2>&1`

		# Only Whine Once Per Hour If There's No Log Log File
		# (And We Can't Create It...)
		if [[ $? != 0 && $min -eq ${SOS_MIN} ]]; then
			echo -e "${NL}Error Creating PVSD Monitor Log Log File!"
			echo -e "\n${ckt}"
			NL="\n"
		fi

	fi

	if [[ -w "${LOGLOGFILE}" ]]; then

		echo "$date $host $PVSD_LOG = $log_hbytes" >> ${LOGLOGFILE}

		echo -n "   Log is $log_bytes = $pct_log% of usage," \
			>> ${LOGLOGFILE}
		echo " Disk is $pct_used% full." >> ${LOGLOGFILE}

	# Only Whine Once Per Hour If We've Become
	# Disconnected from the Universe...
	elif [[ $min -eq ${SOS_MIN} ]]; then

		echo -e "${NL}Error: PVSD Monitor Log Log File Unwritable!"

		echo "\n$date $host $PVSD_LOG = $log_hbytes"

		echo -n "   Log is $log_bytes = $pct_log% of usage,"
		echo " Disk is $pct_used% full."

		NL="\n"

	fi

	# Seemed like a good idea at the time... ;-D
	# if [[ $pct_log -gt 80 ]]; then
	#	CHECK_ERROR_REPORTING
	#	do_report="$?"
	#	# Report Error...
	#	if [[ $do_report == 1 ]]; then
	#		echo -e -n "${NL}Error on ${host}:"
	#		echo " PVSD Log is $pct_log% of Usage!"
	#		echo "   $PVSD_LOG $log_bytes ($log_hbytes)"
	#		NL="\n"
	#	fi
	#	# Set Error Count for Next Invocation,
	#	if [[ ${ERROR_COUNT} -gt 0 ]]; then
	#		SET_ERROR_COUNT
	#		if [[ ${log_error} != 0 ]]; then
	#			echo -e "${NL}ErrorCount=${ERROR_COUNT}"
	#		fi
	#	fi
	#	exit -2
	# fi

	if [[ $pct_used -gt 80 ]]; then

		CHECK_ERROR_REPORTING
		do_report="$?"

		# Report Error...
		if [[ $do_report == 1 ]]; then
			echo -e -n "${NL}Error on ${host}:"
			echo " $pvsd_top_logdir is $pct_used% Full!"
			echo "   $PVSD_LOG $log_bytes ($log_hbytes)"
			NL="\n"
		fi

		# Set Error Count for Next Invocation,
		if [[ ${ERROR_COUNT} -gt 0 ]]; then
			SET_ERROR_COUNT
			if [[ ${log_error} != 0 ]]; then
				echo -e "${NL}ErrorCount=${ERROR_COUNT}"
			fi
		fi

		exit -3

	fi

fi

# Log If Using Alternate Log Home Directory...
if [[ ${log_error} != 0 && ${USING_ALT_LOG_HOME} != "" ]]; then
	echo -e "${NL}${USING_ALT_LOG_HOME}"
fi

# Set Error Count for Next Invocation,
if [[ ${ERROR_COUNT} -gt 0 ]]; then
	SET_ERROR_COUNT
	if [[ ${log_error} != 0 ]]; then
		echo -e "${NL}ErrorCount=${ERROR_COUNT}"
	fi
	exit -4
fi

