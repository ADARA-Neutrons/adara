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
BC="/usr/bin/bc"
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
beamline_prefix=""
ipts=""
run_number=""

proposal=""
samplename=""
sampletype=""
users=""
local_contact=""

PVList=""

verbose=0

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
	elif [[ "${key}" == "beamline_prefix" ]]; then
		echo "Setting Beamline Prefix to [${value}]."
		beamline_prefix="${value}"
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
	elif [[ "${key}" == "verbose" ]]; then
		echo "Setting Verbose Mode to [${value}]."
		verbose="${value}"
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
echo "beamline_prefix = [${beamline_prefix}]"
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

FLOAT_COMPARE()
{
	local _fv1="$1"
	shift

	local _fv2="$1"
	shift

	local _debug="$1"
	shift

	diff=`echo "${_fv1} - ${_fv2}" | ${BC}`

	if [[ ${_debug} == "debug" ]]; then
		echo "FLOAT_COMPARE(): FV1=[${_fv1}] FV2=[${_fv2}] diff=[${diff}]."
	fi

	# FV1 Less Than FV2...
	if [[ "${diff:0:1}" == "-" ]]; then
		if [[ ${_debug} == "debug" ]]; then
			echo -e "   ${_fv1} LESS THAN ${_fv2}"
		fi
		echo "-1"
		
	# FV1 Equals FV2...
	elif [[ "${diff:0:1}" == "0" ]]; then
		if [[ ${_debug} == "debug" ]]; then
			echo -e "   ${_fv1} EQUALS ${_fv2}"
		fi
		echo "0"

	# FV1 Greater Than FV2 (".dddd" or "d.dddd")
	else
		if [[ ${_debug} == "debug" ]]; then
			echo -e "   ${_fv1} GREATER THAN ${_fv2}"
		fi
		echo "1"
	fi
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

# Duration (for No Scan Index Runs, Multi-Column Data Timestamp...)
duration=`GET_NEXUS_VAL "duration" "Run Duration (Seconds)"`

# Experiment Title
experiment_title=`GET_NEXUS_STR "experiment_title" "Experiment Title"`

# Experiment Number
experiment_number="No Such Thing in EPICS/ADARA..."

# SpICE Command
spice_command="Not_Used"

# BuiltIn Command
builtin_command="Not_Used"

# Scan (Run) Title
scan_title=`GET_NEXUS_STR "title" "Scan (Run) Title"`

# Monochromator
# Type of Monochromator, e.g. pg002, lookup chart... (d-spacing)
monochromator=`GET_NEXUS_STR \
	"DASlogs/monochromator/value_strings" \
	"Monochromator"`

# Analyzer
# TODO Type of Analyzer, choose from drop-down list...
# Extract from NeXus... (hb3-Galil1)
analyzer="TODO Type of Analyzer, choose from drop-down list..."

# Sense (Ain't Got None)
# Beamline Config, 3 +/- characters concatenated "(+/-,+/-,+/-)"
sense=""
monochromator_PlusMinus=`GET_NEXUS_VAL \
	"DASlogs/monochromator_PlusMinus/value" \
	"Monochromator PlusMinus"`
if [[ ${monochromator_PlusMinus} == "-1" ]]; then
	sense="${sense}-"
else
	sense="${sense}+"
fi
sample_PlusMinus=`GET_NEXUS_VAL \
	"DASlogs/sample_PlusMinus/value" \
	"Sample PlusMinus"`
if [[ ${sample_PlusMinus} == "-1" ]]; then
	sense="${sense}-"
else
	sense="${sense}+"
fi
analyzer_PlusMinus=`GET_NEXUS_VAL \
	"DASlogs/analyzer_PlusMinus/value" \
	"Analyzer PlusMinus"`
if [[ ${analyzer_PlusMinus} == "-1" ]]; then
	sense="${sense}-"
else
	sense="${sense}+"
fi

# Collimation
# TODO Beamline Config...
# Extract from NeXus...
collimation="TODO Beamline Config..."

# Sample Mosaic
# TODO Relevant _Only_ for Single Crystal, depends on how good the sample is
# "spread"... (in degrees/minutes of arc)... ["30.0 minutes")
# Extract from NeXus...
samplemosaic="TODO Single Crystal Only, Spread in degrees/minutes of arc..."

# Lattice Constants
latticeconstants=""
latticeA=`GET_NEXUS_VAL \
	"DASlogs/LatticeA/value" \
	"Lattice A Constant"`
latticeAfmt=`printf "%.6f" "${latticeA}"`
latticeconstants="${latticeAfmt}"
latticeB=`GET_NEXUS_VAL \
	"DASlogs/LatticeB/value" \
	"Lattice B Constant"`
latticeBfmt=`printf "%.6f" "${latticeB}"`
latticeconstants="${latticeconstants},${latticeBfmt}"
latticeC=`GET_NEXUS_VAL \
	"DASlogs/LatticeC/value" \
	"Lattice C Constant"`
latticeCfmt=`printf "%.6f" "${latticeC}"`
latticeconstants="${latticeconstants},${latticeCfmt}"
latticeAlpha=`GET_NEXUS_VAL \
	"DASlogs/LatticeAlpha/value" \
	"Lattice Alpha Constant"`
latticeAlphafmt=`printf "%.6f" "${latticeAlpha}"`
latticeconstants="${latticeconstants},${latticeAlphafmt}"
latticeBeta=`GET_NEXUS_VAL \
	"DASlogs/LatticeBeta/value" \
	"Lattice Beta Constant"`
latticeBetafmt=`printf "%.6f" "${latticeBeta}"`
latticeconstants="${latticeconstants},${latticeBetafmt}"
latticeGamma=`GET_NEXUS_VAL \
	"DASlogs/LatticeGamma/value" \
	"Lattice Gamma Constant"`
latticeGammafmt=`printf "%.6f" "${latticeGamma}"`
latticeconstants="${latticeconstants},${latticeGammafmt}"

# UB Matrix
# (Assuming "Row Major" Ordering For Now... ;-D)
ubmatrix=""
ubmatrixR1C1=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR1C1/value" \
	"UBMatrix R1C1 Value"`
ubmatrixR1C1fmt=`printf "%.6f" "${ubmatrixR1C1}"`
ubmatrix="${ubmatrixR1C1fmt}"
ubmatrixR1C2=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR1C2/value" \
	"UBMatrix R1C2 Value"`
ubmatrixR1C2fmt=`printf "%.6f" "${ubmatrixR1C2}"`
ubmatrix="${ubmatrix},${ubmatrixR1C2fmt}"
ubmatrixR1C3=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR1C3/value" \
	"UBMatrix R1C3 Value"`
ubmatrixR1C3fmt=`printf "%.6f" "${ubmatrixR1C3}"`
ubmatrix="${ubmatrix},${ubmatrixR1C3fmt}"
ubmatrixR2C1=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR2C1/value" \
	"UBMatrix R2C1 Value"`
ubmatrixR2C1fmt=`printf "%.6f" "${ubmatrixR2C1}"`
ubmatrix="${ubmatrix},${ubmatrixR2C1fmt}"
ubmatrixR2C2=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR2C2/value" \
	"UBMatrix R2C2 Value"`
ubmatrixR2C2fmt=`printf "%.6f" "${ubmatrixR2C2}"`
ubmatrix="${ubmatrix},${ubmatrixR2C2fmt}"
ubmatrixR2C3=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR2C3/value" \
	"UBMatrix R2C3 Value"`
ubmatrixR2C3fmt=`printf "%.6f" "${ubmatrixR2C3}"`
ubmatrix="${ubmatrix},${ubmatrixR2C3fmt}"
ubmatrixR3C1=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR3C1/value" \
	"UBMatrix R3C1 Value"`
ubmatrixR3C1fmt=`printf "%.6f" "${ubmatrixR3C1}"`
ubmatrix="${ubmatrix},${ubmatrixR3C1fmt}"
ubmatrixR3C2=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR3C2/value" \
	"UBMatrix R3C2 Value"`
ubmatrixR3C2fmt=`printf "%.6f" "${ubmatrixR3C2}"`
ubmatrix="${ubmatrix},${ubmatrixR3C2fmt}"
ubmatrixR3C3=`GET_NEXUS_VAL \
	"DASlogs/UBMatrixR3C3/value" \
	"UBMatrix R3C3 Value"`
ubmatrixR3C3fmt=`printf "%.6f" "${ubmatrixR3C3}"`
ubmatrix="${ubmatrix},${ubmatrixR3C3fmt}"

# Mode
# TODO Related to "preset", maybe "normal" or -> "0"...?
mode="TODO Related to Preset, normal or 0..."

# Plane Normal
# 3-Vector of Real Numbers, Plane Perpendicular to Beamline Plane,
# Related to UB Matrix...
plane_normal=""
plane_normal_H=`GET_NEXUS_VAL \
	"DASlogs/PlaneNormalH/value" \
	"Plane Normal H Value"`
plane_normal_Hfmt=`printf "%.6f" "${plane_normal_H}"`
plane_normal="${plane_normal_Hfmt}"
plane_normal_K=`GET_NEXUS_VAL \
	"DASlogs/PlaneNormalK/value" \
	"Plane Normal K Value"`
plane_normal_Kfmt=`printf "%.6f" "${plane_normal_K}"`
plane_normal="${plane_normal},${plane_normal_Kfmt}"
plane_normal_L=`GET_NEXUS_VAL \
	"DASlogs/PlaneNormalL/value" \
	"Plane Normal L Value"`
plane_normal_Lfmt=`printf "%.6f" "${plane_normal_L}"`
plane_normal="${plane_normal},${plane_normal_Lfmt}"

# UB Conf
# TODO Config File Associated with UB Matrix
# Need to Capture in NeXus and Extract...
ubconf="TODO Config File Associated with UB Matrix..."

# Def X
# Default Plot X Axis
def_x=`GET_NEXUS_STR "DASlogs/def_x/value" "Default Plot X Axis"`

# Def Y
# Default Plot Y Axis
def_y=`GET_NEXUS_STR "DASlogs/def_y/value" "Default Plot Y Axis"`

# Total Counts
total_counts=`GET_NEXUS_VAL "total_counts" "Sum of (Total) Counts"`

# Center of Mass
# TODO Estimate After Run, Assuming there is a SINGLE Peak
# - Determined during experiment... _Only_ for Alignment Scan?
# Single Weighted X Value vs Y Axis... [Including Error Bar]
# Extract from NeXus...? (Or Omit...?)
center_of_mass="TODO Single Weighted X Value vs Y Axis... SINGLE Peak..."

# Full Width Half Max (With a Twist of Lemon)
# TODO How wide peak is, Max Value / 2, Difference vs 2 Nearest Peaks...
# Data Points Updated After Each Point... Keep Last Value...
# Extract from NeXus...
full_width_half_max="TODO Peak Width, Max Value / 2, Diff vs 2 Nearest..."

#
# Extract Multi-Column Data Section Arrays...
#

# Split PVList into Individual PV Names...

declare -A PVNames

PVList_clean=`echo "${PVList}" | ${SED} "s/,//g"`

echo

i=0

for pv in ${PVList_clean} ; do

	echo "pv = [${pv}]"

	PVNames[${i}]="${pv}"

	# Increment PVNames Index...
	i=$(( i + 1 ))

done

nPVNames="${i}"

# Extract Value and Time Arrays for Each PV...

echo -e "\nPVNames[${nPVNames}] Array =\n\n[${PVNames[@]}]"

# Capture Value and Time Array for Each PVName Instance...

for (( pv=0 ; pv < nPVNames ; pv++ )) ; do

	echo -e "\nPVNames[${pv}] = [${PVNames[${pv}]}]"

	# Value Array...

	valueArr="PVValueArr${pv}"
	eval "declare -A ${valueArr}"

	valueArrSz="PVValueArrSize${pv}"
	eval "declare -A ${valueArrSz}"

	GET_NEXUS_ARRAY "DASlogs/${PVNames[${pv}]}/value" \
		${valueArr} ${valueArrSz} "${PVNames[${pv}]} Value Array"

	eval "size=\${${valueArrSz}}"

	echo -e "\n${valueArrSz} = [${size}]"

	if [[ ${verbose} -gt 1 ]]; then
		echo
		for (( v=0 ; v < size ; v++ )) ; do
			eval "echo \"${valueArr}[${v}] = [\${${valueArr}[${v}]}]\""
		done
	fi

	# Time Array...

	timeArr="PVTimeArr${pv}"
	eval "declare -A ${timeArr}"

	timeArrSz="PVTimeArrSize${pv}"
	eval "declare -A ${timeArrSz}"

	GET_NEXUS_ARRAY "DASlogs/${PVNames[${pv}]}/time" \
		${timeArr} ${timeArrSz} "${PVNames[${pv}]} Value Array"

	eval "size=\${${timeArrSz}}"

	echo -e "\n${timeArrSz} = [${size}]"

	if [[ ${verbose} -gt 1 ]]; then
		echo
		for (( t=0 ; t < size ; t++ )) ; do
			eval "echo \"${timeArr}[${t}] = [\${${timeArr}[${t}]}]\""
		done
	fi

done

# Capture Scan Index (SpICE "Point") Value and Time Arrays...

echo -e "\nCapturing Scan Index Value and Time Arrays..."

# Scan Index Value Array...

valueArr="ScanIndexValueArr"
eval "declare -A ${valueArr}"

valueArrSz="ScanIndexValueArrSize"
eval "declare -A ${valueArrSz}"

GET_NEXUS_ARRAY "DASlogs/scan_index/value" \
	${valueArr} ${valueArrSz} "Scan Index Value Array"

eval "size=\${${valueArrSz}}"

echo -e "\n${valueArrSz} = [${size}]"

if [[ ${verbose} -gt 1 ]]; then
	echo
	for (( v=0 ; v < size ; v++ )) ; do
		eval "echo \"${valueArr}[${v}] = [\${${valueArr}[${v}]}]\""
	done
fi

# Scan Index Time Array...

timeArr="ScanIndexTimeArr"
eval "declare -A ${timeArr}"

timeArrSz="ScanIndexTimeArrSize"
eval "declare -A ${timeArrSz}"

GET_NEXUS_ARRAY "DASlogs/scan_index/time" \
	${timeArr} ${timeArrSz} "Scan Index Value Array"

eval "size=\${${timeArrSz}}"

echo -e "\n${timeArrSz} = [${size}]"

if [[ ${verbose} -gt 1 ]]; then
	echo
	for (( t=0 ; t < size ; t++ )) ; do
		eval "echo \"${timeArr}[${t}] = [\${${timeArr}[${t}]}]\""
	done
fi

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

# (SpICE) Command (Not Used)
echo "# command = ${spice_command}" >> "${scratch}"

# Builtin Command (Not Used)
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

echo "# col_headers = " >> "${scratch}"

echo -n "#   Pt." >> "${scratch}"

printf " %14s" "timestamp" >> "${scratch}"

for (( pv=0 ; pv < nPVNames ; pv++ )) ; do

	# "Clean" the PVName Path to Eliminate the Cruft...
	# - This Needs to be the BLXXX Beamline PV Prefix,
	# Probably _Not_ the Beamline Long Name.
	# (As it happens, this Works for "HB3"! ;-D)
	# Note: This may not really be necessary,
	# given proper PV Aliases in the beamline.xml file... ;-D
	if [[ -n ${beamline_prefix} ]]; then
		pvname=`echo "${PVNames[${pv}]}" \
			| ${SED} -e "s/${beamline_prefix}://" \
				-e "s/Mot://" \
				-e "s/.RBV//"`
	else
		pvname=`echo "${PVNames[${pv}]}" \
			| ${SED} -e "s/${beamline}://" \
				-e "s/Mot://" \
				-e "s/.RBV//"`
	fi

	printf " %14s" "${pvname}" >> "${scratch}"

done

echo >> "${scratch}"

# Step Through Scan Index Time-Series Log and Dump a Line for Each Scan

eval "nScan=\${ScanIndexValueArrSize}"

echo -e "\nFound ${nScan} Scan Index Time-Series Log Value(s)."

scan_index=-1

last_scan_ts=-1

pt=1

for (( scan=0 ; scan < nScan ; scan++ )) ; do

	# Next Scan Index...

	eval "index=\${ScanIndexValueArr[${scan}]}"

	echo -e "\nScan Index #${scan} = ${index}"

	# New Scan Start - (Non-Zero) Scan Index...

	if [[ ${index} -gt 0 ]]; then

		# Save Scan Index
		scan_index=${index}

	# End of Scan (Zero Scan Index Value)

	else

		# Did We Find a Valid Non-Zero Scan Index...?
		if [[ ${scan_index} -gt 0 ]]; then

			eval "scan_ts=\${ScanIndexTimeArr[${scan}]}"

			echo -e "\nScan Index ${scan_index} Complete at ${scan_ts}."

			#FLOAT_COMPARE ${scan_ts} ${last_scan_ts} "debug"

			scan_ts_cmp=`FLOAT_COMPARE ${scan_ts} ${last_scan_ts}`
			#echo "scan_ts_cmp=[${scan_ts_cmp}]"

			# New Scan Timestamp is Less Than or Equal to Last...?
			if [[ ${scan_ts_cmp} -lt 1 ]]; then
				echo -e -n "\nERROR: Scan Timestamp SAWTOOTH:"
				echo -e " ${scan_ts} <= ${last_scan_ts}"
			fi

			# Dump Scan "Point" and Timestamp to Scratch File...

			printf " %6d" "${pt}" >> "${scratch}"

			printf " %14.4f" "${scan_ts}" >> "${scratch}"

			# Now Go Through All Requested PV Logs and
			# Snag Latest Value for Time <= Scan Timestamp

			for (( pv=0 ; pv < nPVNames ; pv++ )) ; do

				echo -e "\nDumping Value for ${PVNames[${pv}]} PV:"

				# Get PV's Time Array Size...

				timeArrSz="PVTimeArrSize${pv}"

				eval "size=\${${timeArrSz}}"

				# Walk PV's Time Array...

				valueArr="PVValueArr${pv}"

				timeArr="PVTimeArr${pv}"

				last_ts=-1
				last_t=-1

				for (( t=0 ; t < size ; t++ )) ; do

					eval "ts=\${${timeArr}[${t}]}"

					#FLOAT_COMPARE ${ts} ${scan_ts} "debug"

					ts_cmp=`FLOAT_COMPARE ${ts} ${scan_ts}`
					#echo "t=${t} ts_cmp=[${ts_cmp}]"

					# PV Value Timestamp is Less Than Scan End...
					if [[ ${ts_cmp} -lt 0 ]]; then

						if [[ ${verbose} -gt 0 ]]; then
							eval "val=\${${valueArr}[${t}]}"
							echo -e -n "Index ${t}: PV Value [${val}]"
							echo -e " at Time ${ts} < Scan End ${scan_ts}"
						fi

						last_ts="${ts}"
						last_t="${t}"

					# This PV Value Timestamp is _After_ Scan End,
					# Use Last PV Value...
					else

						if [[ ${verbose} -gt 0 ]]; then
							eval "val=\${${valueArr}[${t}]}"
							echo -e -n "Index ${t}: PV Value [${val}]"
							echo -e " at Time ${ts} >= Scan End ${scan_ts}"
							echo -e "Done."
						fi

						break

					fi

				done

				# Found a PV Value Before or During Scan
				if [[ ${last_t} != -1 ]]; then

					# Get PV Value from Array...

					eval "val=\${${valueArr}[${last_t}]}"

					echo -e -n "\nGot PV Value Before/During Scan"
					echo -e " at Index ${last_t} = [${val}] @ ${last_ts}"

					printf " %14.4f" "${val}" >> "${scratch}"

				else

					echo -e "\nError: No PV Values Found!"

					printf " %14s" "" >> "${scratch}"

				fi

			done

			# End of Scan "Point" Line...
			echo >> "${scratch}"

			# Save Last Scan Timestamp
			last_scan_ts=${scan_ts}

			# Increment Scan "Point" Index...
			pt=$(( pt + 1 ))

		# No Valid Scan Index (Yet), Ignore...
		else
			# First Scan Index...?
			if [[ ${scan_index} == -1 ]]; then
				echo -e "\nIgnore First Scan Index of ${index}."
			# No Intervening Non-Zero Scan Index...?
			else
				echo -e "\nWarning: No Intervening Non-Zero Scan Index?"
				echo -e "\nOr Redundant Scan Index of Zero (${index})."
			fi
		fi

	fi

done

# Did We Ever Get a Non-Zero Scan Index...?
# If Not, Then Just Dump a Single Final Scan "Point"
# With the Last Value for Each PV...
if [[ ${scan_index} == -1 ]]; then

	echo -e "\nNo Non-Zero Scan Index Found."
	echo -e "\nJust Dump Final Scan \"Point\"."

	# Dump Any Final Scan "Point" and Timestamp to Scratch File...

	printf " %6d" "${pt}" >> "${scratch}"

	# Use Run Duration as Maximum Run Timestamp... ;-D

	echo -e "\nUse Run Duration as Maximum Run Timestamp = [${duration}]."

	run_stop_ts="${duration}"

	printf " %14.4f" "${run_stop_ts}" >> "${scratch}"

	# Now Go Through All Requested PV Logs and
	# Snag Last PV Value...

	for (( pv=0 ; pv < nPVNames ; pv++ )) ; do

		echo -e "\nDumping Value for ${PVNames[${pv}]} PV:"

		# Get PV's Value Array Size...

		valueArrSz="PVValueArrSize${pv}"

		eval "size=\${${valueArrSz}}"

		# Get PV Value from Array...

		if [[ ${size} -gt 0 ]]; then

			valueArr="PVValueArr${pv}"

			v=$(( size - 1 ))

			eval "val=\${${valueArr}[${v}]}"

			echo -e "\nGot Last PV Value [${val}] at Index ${v}."

			printf " %14.4f" "${val}" >> "${scratch}"

		else

			echo -e "\nError: No PV Values Found!"

			printf " %14s" "" >> "${scratch}"

		fi

	done

	echo >> "${scratch}"

fi

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

