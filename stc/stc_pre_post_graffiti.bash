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

GET_NEXUS_ARRAY()
{
	local _path="$1"
	shift

	local _arr_name="$1"
	shift

	local _arr_size="$1"
	shift

	local _label="$*"

	# Extract Array Values from NeXus...

	local _val_arr=`GET_NEXUS_VAL "${_path}" "${_label}"`

	if [[ -z ${_val_arr} || ${val_arr} =~ ^Error ]]; then
		echo "Error Extracting ${_label} Array from NeXus...!"
		return
	fi

	# Append Values to Array...

	local _val=0

	local _i=0

	for _val in ${_val_arr} ; do

		eval "${_arr_name}[${_i}]=\"${_val}\""

		# Increment Value Array Index...
		_i=$(( _i + 1 ))

	done

	# Capture Size of Array

	local _size="${_i}"

	eval "${_arr_size}=\"${_size}\""

	echo -e "\nSuccess Extracting ${_label} Array from NeXus."
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
# Extract Multi-Column Data Section Arrays...
#

# Split PVList into Individual PV Names...

declare -A PVNames

PVList_clean=`echo "${PVList}" | ${SED} "s/,//g"`

i=0

for pv in ${PVList_clean} ; do

	echo "pv = [${pv}]"

	PVNames[${i}]="${pv}"

	# Increment PVNames Index...
	i=$(( i + 1 ))

done

nPVNames="${i}"

# Test GET_NEXUS_ARRAY...

i=0

GET_NEXUS_ARRAY "DASlogs/${PVNames[${i}]}" \
	Array1 ArraySize1 "Test Array Get"

echo -e "\nArray1 = [${Array1[@]}]"

echo
for (( i=0 ; i < ArraySize1 ; i++ )) ; do
	echo "Array1[${i}] = [${Array1[${i}]}]"
done

echo -e "\nArraySize1 = [${ArraySize1}]"

# Extract Value and Time Arrays for Each PV...

echo -e "\nPVNames[${nPVNames}] Array =\n\n[${PVNames[@]}]\n"

# Capture Value and Time Array for Each PVName Instance...

for (( i=0 ; i < nPVNames ; i++ )) ; do

	echo -e "\nPVNames[${i}] = [${PVNames[${i}]}]"

	# Value Array...

	valueArr="PVValueArr${i}"
	eval "declare -A ${valueArr}"

	valueArrSz="PVValueArrSize${i}"
	eval "declare -A ${valueArrSz}"

	GET_NEXUS_ARRAY "DASlogs/${PVNames[${i}]}/value" \
		${valueArr} ${valueArrSz} "${PVNames[${i}]} Value Array"

	eval "echo -e \"\\n${valueArr} = [\${${valueArr}[@]}]\""

	eval "size=\${${valueArrSz}}"

	echo -e "\n${valueArrSz} = [${size}]"

	echo
	for (( j=0 ; j < size ; j++ )) ; do
		eval "echo \"${valueArr}[${j}] = [\${${valueArr}[${j}]}]\""
	done

	# Time Array...

	timeArr="PVTimeArr${i}"
	eval "declare -A ${timeArr}"

	timeArrSz="PVTimeArrSize${i}"
	eval "declare -A ${timeArrSz}"

	GET_NEXUS_ARRAY "DASlogs/${PVNames[${i}]}/time" \
		${timeArr} ${timeArrSz} "${PVNames[${i}]} Value Array"

	eval "echo -e \"\\n${timeArr} = [\${${timeArr}[@]}]\""

	eval "size=\${${timeArrSz}}"

	echo -e "\n${timeArrSz} = [${size}]"

	echo
	for (( j=0 ; j < size ; j++ )) ; do
		eval "echo \"${timeArr}[${j}] = [\${${timeArr}[${j}]}]\""
	done

done

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

# Dump Column Labels...

echo -n "#   Pt." >> "${scratch}"

### Later... printf " %12s" "time" >> "${scratch}"

for (( i=0 ; i < nPVNames ; i++ )) ; do

	# "Clean" the PVName Path to Eliminate the Cruft...
	pvname=`echo "${PVNames[${i}]}" \
		| ${SED} -e "s/${beamline}://" \
			-e "s/Mot://" \
			-e "s/.RBV//"`

	printf " %12s" "${pvname}" >> "${scratch}"

	# For Now... ;-D
	printf " %12s" "(time)" >> "${scratch}"

done

echo >> "${scratch}"

# Determine Max Number of Elements...

num=0

for (( i=0 ; i < nPVNames ; i++ )) ; do

	# Value Array...

	valueArrSz="PVValueArrSize${i}"

	eval "size=\${${valueArrSz}}"

	if [[ ${size} -gt ${num} ]]; then
		num=${size}
	fi

	# Time Array...

	timeArrSz="PVTimeArrSize${i}"

	eval "size=\${${timeArrSz}}"

	if [[ ${size} -gt ${num} ]]; then
		num=${size}
	fi

done

echo -e "\nMax Number of Multi-Column Elements = ${num}."

# For Now Just Dump Time and Value Array for Each PVName Instance...

for (( pt=0 ; pt < num ; pt++ )) ; do

	ptp1=$(( pt + 1 ))

	printf " %6d" "${ptp1}" >> "${scratch}"

	for (( i=0 ; i < nPVNames ; i++ )) ; do

		# Value Array...

		valueArr="PVValueArr${i}"

		valueArrSz="PVValueArrSize${i}"

		eval "size=\${${valueArrSz}}"

		if [[ ${pt} -lt ${size} ]]; then

			eval "value=\${${valueArr}[${pt}]}"

			printf " %12g" "${value}" >> "${scratch}"

		else
			printf " %12s" "" >> "${scratch}"
		fi

		# Time Array...

		timeArr="PVTimeArr${i}"

		timeArrSz="PVTimeArrSize${i}"

		eval "size=\${${timeArrSz}}"

		if [[ ${pt} -lt ${size} ]]; then

			eval "time=\${${timeArr}[${pt}]}"

			printf " %12d" "${time}" >> "${scratch}"

		else
			printf " %12s" "" >> "${scratch}"
		fi

	done

	echo >> "${scratch}"

done

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

