#!/bin/bash
#
# Beamline XML Monitor
#
# (Cron) Script to Monitor Local Beamline.XML Configuration File
# for Any Uncommitted Changes
#

# Catch Errors Amidst Pipelines... ;-Q
set -o pipefail

# Source Local EPICS Beamline Environment (e.g. $BEAMLINE):
. /home/controls/share/master/scripts/beamline_profile.sh

# Handle "Quiet" Operation Option for Cron Usage...
# (Only output on errors...)
QUIET=0
if [ "$1" == "--quiet" ]; then
	QUIET=1
fi

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
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
		# Defer to "master" Branch...
		_branch="master"
	fi
	origin_branch="${_branch}"

	# Now Check the Given Branch at the Remote/Origin...
	git diff origin/${_branch} ${BEAMLINE_XML}
	local _status=$?
	if [[ "${_status}" != 0 ]]; then
		_err="${_status}"
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
		echo -e "Uncommitted Local Changes:\n"
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
		echo -e -n "Committed Local Changes Not Yet Pushed to Origin"
		echo -e " (Branch \"${origin_branch}\"):\n"
		echo "${diffCkOrigin}"
	fi

	return "${_err}"
}

doCkBeamlineXML=`CHECK_BEAMLINE_XML 2>&1`
_status=$?

if [[ "${_status}" != 0 || -n "${doCkBeamlineXML}" || $QUIET == 0 ]]; then
	echo -e "\nBeamline XML Configuration Change Check:\n"
fi

if [[ "${_status}" != 0 ]]; then
	echo -n "*** Error Checking "
	echo -e "Beamline XML for Config Changes!\n"
	/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
		| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@"
	echo
elif [[ -n "${doCkBeamlineXML}" ]]; then
	echo -n "*** Uncommitted or Unpushed "
	echo -e "Beamline XML Config Changes Found!\n"
	/bin/ls -l "${BEAMLINE_GIT_DIR}/${BEAMLINE_XML}" \
		| sed "s@ ${BEAMLINE_GIT_DIR}@\n   ${BEAMLINE_GIT_DIR}@"
	echo
fi

if [[ "${_status}" != 0 || -n "${doCkBeamlineXML}" || $QUIET == 0 ]]; then
	if [[ -n "${doCkBeamlineXML}" ]]; then
		echo -e "$doCkBeamlineXML\n"
	fi
	echo -e "Status = ${_status}\n"
fi

