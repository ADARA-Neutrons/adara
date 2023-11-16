#!/bin/bash

# Command Script Entry - Announce Our Existence...

echo -e "STC Pre-Post-Processing Graffiti Command Script Entry."

echo -e "   [$0]"

echo -e "   [$@]"

script=`basename "$0"`

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

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

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
ls -l "${nexus}"

# Extract Required Header Fields from NeXus...

run_start_date="TODO Extract Run Start Date from NeXus..."

run_start_time="TODO Extract Run Start Time from NeXus..."

run_stop_date="TODO Extract Run Start Date from NeXus..."

run_stop_time="TODO Extract Run Start Time from NeXus..."

experiment_title="TODO Extract IPTS Title from NeXus..."

experiment_number="No Such Thing in EPICS/ADARA..."

spice_command="TODO What is this?"

builtin_command="TODO What is this?"

scan_title="TODO Extract Run Title from NeXus..."

monochromator="TODO What is this?"

analyzer="TODO What is this?"

sense="TODO What is this?"

collimation="TODO What is this?"

samplemosaic="TODO What is this?"

latticeconstants="TODO Extract Sample Meta-Data from NeXus..."

ubmatrix="TODO Extract UB Matrix from NeXus..."

mode="TODO What is this?"

plane_normal="TODO What is this?"

ubconf="TODO What is this?"

def_x="TODO What is this?"

def_y="TODO What is this?"

total_counts="TODO Extract Total Counts from NeXus..."

center_of_mass="TODO What is this?"

full_width_half_max="TODO What is this?"

# Construct Graffiti Data File Path/Name

graffiti_path="/${facility}/${beamline}/${ipts}/graffiti"
echo -e "\ngraffiti_path = [${graffiti_path}]"

graffiti_name="${beamline}_${run_number}.dat"
echo -e "\ngraffiti_name = [${graffiti_name}]"

scratch_dir="/tmp"

scratch="${scratch_dir}/${graffiti_name}.$$"

echo -e "\nGraffiti Scratch File = [${scratch}]"

touch "${scratch}"

echo
ls -l "${scratch}"

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

/bin/cat "${scratch}"

echo
ls -l "${scratch}"

# Move Graffiti Scratch File to File Archive

status=0

if [[ ! -d "${graffiti_path}" ]]; then
	mkdir -p "${graffiti_path}"
	if [[ $? != 0 ]]; then
		echo -e "\nError Creating Graffiti Directory...!"
		status=1
	else
		echo -e "\nGraffiti Archive Path Successfully Created:\n"
		ls -ld "${graffiti_path}"
	fi
else
	echo -e "\nGraffiti Archive Path Exists:\n"
	ls -ld "${graffiti_path}"
fi

if [[ ${status} == 0 ]]; then
	mv "${scratch}" "${graffiti_path}/${graffiti_name}"
	if [[ $? != 0 ]]; then
		echo -e "\nError Moving Graffiti Data File to File Archive...!"
		status=2
	else
		echo -e "\nGraffiti Data File ${graffiti_name} Moved to Archive:\n"
		ls -l "${graffiti_path}/${graffiti_name}"
	fi
fi

# Clean Up Scratch Graffiti File, If Unable to Archive...
if [[ ${status} != 0 ]]; then
	echo -e "\nFailed to Archive Graffiti Data File - Removing Scratch..."
	/bin/rm "${scratch}"
fi

# Do Nothing and Exit...

echo -e "\nExiting with Status = ${status}.\n"

exit ${status}

