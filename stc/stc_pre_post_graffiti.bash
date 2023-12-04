#!/bin/bash

# Globals

NXLS="/usr/local/bin/nxls"

BASENAME="/usr/bin/basename"
MKDIR="/usr/bin/mkdir"
TOUCH="/usr/bin/touch"
GREP="/usr/bin/grep"
AWK="/usr/bin/awk"
CAT="/usr/bin/cat"
SED="/usr/bin/sed"
LS="/usr/bin/ls"
MV="/usr/bin/mv"
RM="/usr/bin/rm"

# Command Script Entry - Announce Our Existence...

echo -e "STC Pre-Post-Processing Graffiti Command Script Entry."

echo -e "   [$0]"

echo -e "   [$@]"

script=`${BASENAME} "$0"`

# Parse Command Line Parameters...

facility=""
beamline=""
ipts=""
run_number=""

proposal=""
samplename=""
sampletype=""
users=""
local_contact=""

PVList=""

for arg in "$@" ; do

	#echo -e "arg=$arg"

	key=`echo "$arg" | ${AWK} -F = '{print $1}'`
	value=`echo "$arg" | ${AWK} -F = '{print $2}'`

	#echo "arg=$arg, key=$key, value=$value"

	if [[ "${key}" == "facility" ]]; then
		echo "Setting Facility to [${value}]."
		facility="${value}"
	elif [[ "${key}" == "beamline" ]]; then
		echo "Setting Beamline to [${value}]."
		beamline="${value}"
	elif [[ "${key}" == "proposal" ]]; then
		echo "Setting IPTS Proposal to [${value}]."
		ipts="${value}"
		proposal="${value/IPTS-}"
	elif [[ "${key}" == "run_number" ]]; then
		echo "Setting Run Number to [${value}]."
		run_number="${value}"
	elif [[ "${key}" == "SampleName" ]]; then
		echo "Setting SampleName to [${value}]."
		samplename="${value}"
	elif [[ "${key}" == "SampleNature" ]]; then
		echo "Setting SampleNature to [${value}]."
		sampletype="${value}"
	elif [[ "${key}" == "Members" ]]; then
		echo "Setting Members to [${value}]."
		users="${value}"
	elif [[ "${key}" == "Contact" ]]; then
		echo "Setting Contact to [${value}]."
		local_contact="${value}"
	elif [[ "${key}" == "PVList" ]]; then
		echo "Setting PVList to [${value}]."
		PVList="${value}"
	fi

done

# Basic Command Line Parameter Check...

if [[ -z "${facility}" || -z "${beamline}" || -z "${proposal}" \
		|| -z "${run_number}" ]]; then
	echo -e "\nError: ${script}: Missing Fundamental Run Parameters...!\n"
	echo "facility = [${facility}]"
	echo "beamline = [${beamline}]"
	echo "ipts = [${ipts}]"
	echo "proposal = [${proposal}]"
	echo "run_number = [${run_number}]"
	exit -1
fi

# Dump Parsed Command Line Parameters...

echo -e "\nParsed Command Line Parameters:\n"

echo "facility = [${facility}]"
echo "beamline = [${beamline}]"
echo "ipts = [${ipts}]"
echo "proposal = [${proposal}]"
echo "run_number = [${run_number}]"
echo "samplename = [${samplename}]"
echo "sampletype = [${sampletype}]"
echo "users = [${users}]"
echo "local_contact = [${local_contact}]"

echo -e "\nPVList =\n\n[${PVList}]"

# Construct NeXus Data File Path/Name

nexus_path="/${facility}/${beamline}/${ipts}/nexus"
echo -e "\nnexus_path = [${nexus_path}]"

nexus_name="${beamline}_${run_number}.nxs.h5"
echo -e "\nnexus_name = [${nexus_name}]"

nexus="${nexus_path}/${nexus_name}"
echo -e "\nNeXus Source Data File = [${nexus}]"

echo
${LS} -l "${nexus}"

#
# Extraction Methods
#

GET_NEXUS_STR()
{
	local _path="$1"
	shift

	local _label="$*"

	local _str=`${NXLS} ${nexus} -p /entry/${_path} -l -s --terse \
		| ${SED} "s/\"//g"`

	# Replace "%20" White Space Encodings...
	_str=`echo "${_str}" | ${SED} "s/%20/ /g"`

	if [[ -z ${_str} ]]; then
		_date="Error Extracting ${_label} from NeXus"
	fi

	echo "${_str}"
}

GET_NEXUS_VAL()
{
	local _path="$1"
	shift

	local _label="$*"

	local _val=`${NXLS} ${nexus} -p /entry/${_path} -l -s --terse`

	if [[ -z ${_val} ]]; then
		_date="Error Extracting ${_label} from NeXus"
	fi

	echo "${_val}"
}

GET_DATE()
{
	local _date_time="$1"
	shift

	local _label="$*"

	local _date=`echo "${_date_time}" | ${AWK} -F "T" '{print $1}'`

	if [[ -z ${_date} ]]; then
		_date="Error Extracting ${_label} from NeXus"
	fi

	echo "${_date}"
}

GET_TIME()
{
	local _date_time="$1"
	shift

	local _label="$*"

	local _time=`echo "${_date_time}" | ${AWK} -F "[T.]" '{print $2}'`

	if [[ -z ${_time} ]]; then
		_time="Error Extracting ${_label} from NeXus"
	fi

	echo "${_time}"
}

#
# Extract Required Header Fields from NeXus...
#

# Run Start Date and Time
start_time=`GET_NEXUS_STR "start_time" "Run Start Date and Time"`
run_start_date=`GET_DATE "${start_time}" "Run Start Date"`
run_start_time=`GET_TIME "${start_time}" "Run Start Time"`

# Run Stop Date and Time
end_time=`GET_NEXUS_STR "end_time" "Run Stop Date and Time"`
run_stop_date=`GET_DATE "${end_time}" "Run Stop Date"`
run_stop_time=`GET_TIME "${end_time}" "Run Stop Time"`

# Experiment Title
experiment_title=`GET_NEXUS_STR "experiment_title" "Experiment Title"`

# Experiment Number
experiment_number="No Such Thing in EPICS/ADARA..."

# SpICE Command
spice_command="TODO What is this?"

# BuiltIn Command
builtin_command="TODO What is this?"

# Scan (Run) Title
scan_title=`GET_NEXUS_STR "title" "Scan (Run) Title"`

# Monochromator
monochromator="TODO What is this?"

# Analyzer
analyzer="TODO What is this?"

# Sense (Ain't Got None)
sense="TODO What is this?"

# Collimation
collimation="TODO What is this?"

# Sample Mosaic
samplemosaic="TODO What is this?"

# Lattice Constants
latticeconstants="TODO Extract Sample Meta-Data from NeXus..."

# UB Matrix
ubmatrix="TODO Extract UB Matrix from NeXus..."

# Mode
mode="TODO What is this?"

# Plane Normal
plane_normal="TODO What is this?"

# UB Conf
ubconf="TODO What is this?"

# Def X
def_x="TODO What is this?"

# Def Y
def_y="TODO What is this?"

# Total Counts
total_counts=`GET_NEXUS_VAL "total_counts" "Sum of (Total) Counts"`

# Center of Mass
center_of_mass="TODO What is this?"

# Full Width Half Max (With a Twist of Lemon)
full_width_half_max="TODO What is this?"

#
# Construct Graffiti Data File Path/Name
#

graffiti_path="/${facility}/${beamline}/${ipts}/graffiti"
echo -e "\ngraffiti_path = [${graffiti_path}]"

graffiti_name="${beamline}_${run_number}.dat"
echo -e "\ngraffiti_name = [${graffiti_name}]"

scratch_dir="/tmp"

scratch="${scratch_dir}/${graffiti_name}.$$"

echo -e "\nGraffiti Scratch File = [${scratch}]"

${TOUCH} "${scratch}"

echo
${LS} -l "${scratch}"

#
# Populate Graffiti Header...
#

# Scan ("Run Number"?)
echo "# scan = ${run_number}" >> "${scratch}"

# (Run Start) Date
echo "# date = ${run_start_date}" >> "${scratch}"

# (Run Start) Time
echo "# time = ${run_start_time}" >> "${scratch}"

# Proposal
echo "# proposal = ${proposal}" >> "${scratch}"

# Experiment (IPTS Title? Or Run Title?)
echo "# experiment = ${experiment_title}" >> "${scratch}"

# Experiment Number (There Isn't Any... ;-b)
echo "# experiment_number = ${experiment_number}" >> "${scratch}"

# (SpICE) Command (What is this? Where is it Stored?)
echo "# command = ${spice_command}" >> "${scratch}"

# Builtin Command (What is this? Where is it Stored?)
echo "# builtin_command = ${builtin_command}" >> "${scratch}"

# Users
echo "# users = ${users}" >> "${scratch}"

# Local Contact
echo "# local_contact = ${local_contact}" >> "${scratch}"

# Scan Title (Run Title)
echo "# scan_title = ${scan_title}" >> "${scratch}"

# Monochromator
echo "# monochromator = ${monochromator}" >> "${scratch}"

# Analyzer
echo "# analyzer = ${analyzer}" >> "${scratch}"

# Sense
echo "# sense = ${sense}" >> "${scratch}"

# Collimation
echo "# collimation = ${collimation}" >> "${scratch}"

# Sample Name
echo "# samplename = ${samplename}" >> "${scratch}"

# Sample Type (Nature)
echo "# sampletype = ${sampletype}" >> "${scratch}"

# Sample Mosaic
echo "# samplemosaic = ${samplemosaic}" >> "${scratch}"

# Lattice Constants
echo "# latticeconstants = ${latticeconstants}" >> "${scratch}"

# UB Matrix
echo "# ubmatrix = ${ubmatrix}" >> "${scratch}"

# Mode
echo "# mode = ${mode}" >> "${scratch}"

# Plane Normal
echo "# plane_normal = ${plane_normal}" >> "${scratch}"

# UB Config
echo "# ubconf = ${ubconf}" >> "${scratch}"

# Def X
echo "# def_x = ${def_x}" >> "${scratch}"

# Def Y
echo "# def_y = ${def_y}" >> "${scratch}"

#
# Dump Multi-Column Data Section
#

# . . .
echo "[Multi-Column Data Section Goes Here...]" >> "${scratch}"

#
# Dump Graffiti Footer...
#

# Sum of Counts (Total Counts)
echo "# Sum of Counts = ${total_counts}" >> "${scratch}"

# Center of Mass
echo "# Center of Mass = ${center_of_mass}" >> "${scratch}"

# Full Width Half-Maximum
echo "# Full Width Half-Maximum = ${full_width_half_max}" >> "${scratch}"

# Scan Completed (Run Stop Time and Date)
echo "# ${run_stop_time}  ${run_stop_date}   scan completed." \
	>> "${scratch}"

# Dump Graffiti Scratch File

echo -e "\nGraffiti Scratch File:\n"

${CAT} "${scratch}"

echo
${LS} -l "${scratch}"

# Move Graffiti Scratch File to File Archive

status=0

if [[ ! -d "${graffiti_path}" ]]; then
	${MKDIR} -p "${graffiti_path}"
	if [[ $? != 0 ]]; then
		echo -e "\nError Creating Graffiti Directory...!"
		status=1
	else
		echo -e "\nGraffiti Archive Path Successfully Created:\n"
		ls -ld "${graffiti_path}"
	fi
else
	echo -e "\nGraffiti Archive Path Exists:\n"
	${LS} -ld "${graffiti_path}"
fi

if [[ ${status} == 0 ]]; then
	${MV} "${scratch}" "${graffiti_path}/${graffiti_name}"
	if [[ $? != 0 ]]; then
		echo -e "\nError Moving Graffiti Data File to File Archive...!"
		status=2
	else
		echo -e "\nGraffiti Data File ${graffiti_name} Moved to Archive:\n"
		${LS} -l "${graffiti_path}/${graffiti_name}"
	fi
fi

# Clean Up Scratch Graffiti File, If Unable to Archive...
if [[ ${status} != 0 ]]; then
	echo -e "\nFailed to Archive Graffiti Data File - Removing Scratch..."
	${RM} "${scratch}"
fi

# Do Nothing and Exit...

echo -e "\nExiting with Status = ${status}.\n"

exit ${status}

