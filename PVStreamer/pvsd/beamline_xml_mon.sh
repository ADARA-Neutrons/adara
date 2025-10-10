#!/bin/bash
#
# Beamline XML Monitor
#
# (Cron) Script to Monitor Local Beamline.XML Configuration File
# for Any Uncommitted Changes
#

# Catch Errors Amidst Pipelines... ;-Q
set -o pipefail

# Source Controls Environment (for EPICS Beamline, e.g. $BEAMLINE):
if [ -e /etc/profile.d/controls.sh ] ; then
	source /etc/profile.d/controls.sh
fi

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

PROG="beamline_xml_mon"

LOG_DIR="${PROG}"

LOGFILE="${LOG_HOME}/${LOG_DIR}/${PROG}.$host_short.log"

# The Minute When We Gasp Our Dying Breath and Beg for Help... ;-D
SOS_MIN=0

NL=`date`
NL="${NL}\n\n"

NLL="${NL}"

# Handle "Quiet" Operation Option for Cron Usage...
# (Only output on errors...)
QUIET=0
if [ "$1" == "--quiet" ]; then
	QUIET=1
fi

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
		echo -e "${NL}Error Creating BeamlineXML Monitor Log Sub-Directory!"
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
		echo -e "${NL}Error Creating BeamlineXML Monitor Log File!"
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

		echo -e "\nErrorCount=${ERROR_COUNT}" >> ${LOGFILE}

	# Only Whine Once Per Hour If We've Become
	# Disconnected from the Universe...
	elif [[ $min -eq ${SOS_MIN} ]]; then
		echo -e -n "${NL}Error Writing Error Count (${ERROR_COUNT})"
		echo " to BeamlineXML Monitor Log File!"
		NL="\n"
	fi
}

# We've Just Encountered an Error...!
# Implement Exponential Error Reporting Fall Off...
# (Based on Cron Job Check Every 1 Hours)
CHECK_ERROR_REPORTING()
{
	local _do_report=1

	# Check for Any Saved "Error Count" from Last Invocation...
	GET_ERROR_COUNT

	# After First 3 Days, Report Once Per Week...
	if [[ ${ERROR_COUNT} -gt 73 ]]; then
		if [[ $(( ( ERROR_COUNT - 1 ) % 168 )) != 0 ]]; then
			_do_report=0
		fi
	
	# After First 3 Hours, Report Once Per Day...
	elif [[ ${ERROR_COUNT} -gt 4 ]]; then
		if [[ $(( ( ERROR_COUNT - 1 ) % 24 )) != 0 ]]; then
			_do_report=0
		fi
	
	# Report First 3 Occurrences in a Row Per Hour...
	fi

	if [[ ${_do_report} == 1 ]]; then
		log_error=1
	fi

	return ${_do_report}
}

# Go to proper Git Config Repo Checkout, for easy checking systemwide.
BEAMLINE_GIT_DIR="/home/controls/$BEAMLINE"

BEAMLINE_XML="beamline.xml"

cd "${BEAMLINE_GIT_DIR}"

# Check for Local Uncommitted Changes
CHECK_BEAMLINE_XML_DIFF()
{
	local _err=0

	git diff ${BEAMLINE_XML}
	local _status=$?
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
	fi

	return "${_err}"
}

# Check for Committed But Not Yet Pushed Changes...!
origin_branch=""
CHECK_BEAMLINE_XML_DIFF_ORIGIN()
{
	local _err=0

	# Determine Checked Out Branch for Remote/Origin Comparison...
	local _branch=`git branch -a | grep "^* " | awk '{print $2}'`
	local _status=$?
	local _branch_err=""
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
		_branch_err="Error Determining Current Beamline Branch...!"
		_branch_err="${_branch_err} Defer to \'master\'..."
		# Defer to "master" Branch...
		_branch="master"
	fi
	origin_branch="${_branch}"

	# Now Check the Given Branch at the Remote/Origin...
	git diff origin/${origin_branch} ${BEAMLINE_XML}
	local _status=$?
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
		if [[ -n "${_branch_err}" ]]; then
			echo -e "\n${_branch_err}"
		fi
		echo -e "\nLocal Branch \"${origin_branch}\" Differs from Origin."
	fi

	return "${_err}"
}

CHECK_BEAMLINE_XML()
{
	local _err=0

	# Check for Local Uncommitted Changes
	# (DON'T Save Results in Local Variable, Eats Up Return Status!)
	diffCk=`CHECK_BEAMLINE_XML_DIFF 2>&1`
	local _status=$?
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
	fi
	if [[ -n "${diffCk}" ]]; then
		echo -e "\nUncommitted Local Changes:\n"
		echo "${diffCk}"
	fi

	# Also Check for Committed But Not Yet Pushed Changes...!
	# (DON'T Save Results in Local Variable, Eats Up Return Status!)
	diffCkOrigin=`CHECK_BEAMLINE_XML_DIFF_ORIGIN 2>&1`
	local _status=$?
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
	fi
	if [[ "${diffCkOrigin}" != "${diffCk}" ]]; then
		echo -e "\nCommitted Local Changes Not Yet Pushed to Origin:"
		echo "${diffCkOrigin}"
	fi

	return "${_err}"
}

doCkBeamlineXML=`CHECK_BEAMLINE_XML 2>&1`
_status=$?

if [[ "${_status}" != 0 || -n "${doCkBeamlineXML}" ]]; then

	CHECK_ERROR_REPORTING
	do_report="$?"

	# Report Error...
	if [[ $do_report == 1 ]]; then
		echo -e "${NL}Beamline XML Configuration Change Check:"
		NL="\n"
	fi

fi

# Log It...
echo -e "${NLL}Beamline XML Configuration Change Check:" >> $LOGFILE
NLL="\n"

if [[ "${_status}" != 0 ]]; then

	CHECK_ERROR_REPORTING
	do_report="$?"

	# Log It...
	echo -e -n "${NLL}*** Error Checking " >> ${LOGFILE}
	echo -e "Beamline XML for Config Changes!\n" >> ${LOGFILE}
	/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
		| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@" \
			>> ${LOGFILE}
	NLL="\n"

	# Report Error...
	if [[ $do_report == 1 || $QUIET == 0 ]]; then
		echo -e -n "${NL}*** Error Checking "
		echo -e "Beamline XML for Config Changes!\n"
		/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
			| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@"
		NL="\n"
	fi

elif [[ -n "${doCkBeamlineXML}" ]]; then

	CHECK_ERROR_REPORTING
	do_report="$?"

	# Log It...
	echo -e -n "${NLL}*** Uncommitted or Unpushed " >> ${LOGFILE}
	echo -e "Beamline XML Config Changes Found!\n" >> ${LOGFILE}
	/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
		| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@" \
			>> ${LOGFILE}
	NLL="\n"

	# Report Error...
	if [[ $do_report == 1 || $QUIET == 0 ]]; then
		echo -e -n "${NL}*** Uncommitted or Unpushed "
		echo -e "Beamline XML Config Changes Found!\n"
		/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
			| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@"
		NL="\n"
	fi

else

	# Log It...
	echo -e "${NLL}OK." >> ${LOGFILE}
	NLL="\n"

	if [[ $QUIET == 0 ]]; then
		echo -e "${NL}OK."
		NL="\n"
	fi

fi

if [[ "${_status}" != 0 || -n "${doCkBeamlineXML}" || $QUIET == 0 ]]; then

	if [[ -n "${doCkBeamlineXML}" ]]; then

		# Log It...
		echo -e "$doCkBeamlineXML" >> ${LOGFILE}

		# (Inherit $log_error from above...)

		# Report Error...
		if [[ ${log_error} != 0 || $QUIET == 0 ]]; then
			echo -e "$doCkBeamlineXML"
		fi

	fi

	# Log It...
	echo -e "${NLL}Status = ${_status}" >> ${LOGFILE}
	NLL="\n"

	# (Inherit $log_error from above...)

	# Report Error...
	if [[ ${log_error} != 0 || $QUIET == 0 ]]; then
		echo -e "${NL}Status = ${_status}"
		NL="\n"
	fi

fi

# Log If Using Alternate Log Home Directory...
if [[ ${USING_ALT_LOG_HOME} != "" ]]; then
	# Log It...
	echo -e "${NLL}${USING_ALT_LOG_HOME}" >> ${LOGFILE}
	NLL="\n"
	if [[ ${log_error} != 0 || $QUIET == 0 ]]; then
		echo -e "${NL}${USING_ALT_LOG_HOME}"
		NL="\n"
	fi
fi

# Set Error Count for Next Invocation
if [[ ${ERROR_COUNT} -gt 0 ]]; then
	SET_ERROR_COUNT
	if [[ ${log_error} != 0 || $QUIET == 0 ]]; then
		echo -e "${NL}ErrorCount=${ERROR_COUNT}\n"
		NL="\n"
	fi
	exit -1
elif [[ $QUIET == 0 ]]; then
	echo >> ${LOGFILE}
	echo
fi

