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

# Construct Graffiti Data File Path/Name

graffiti_path="/${facility}/${beamline}/${ipts}/graffiti"
echo -e "\ngraffiti_path = [${graffiti_path}]"

graffiti_name="${beamline}_${run_number}.dat"
echo -e "\ngraffiti_name = [${graffiti_name}]"

scratch_dir="/tmp"

scratch="${scratch_dir}/${graffiti_name}.$$"

touch "${scratch}"

echo
ls -l "${scratch}"

# Clean Up Scratch Graffiti File (If Not Used)

/bin/rm "${scratch}"

# Do Nothing and Exit...

echo -e "\nDo Nothing and Exit with Status 0."

exit 0

