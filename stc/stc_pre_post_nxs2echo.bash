#!/bin/bash

# Globals

#NXS2ECHO="/usr/local/bin/nxs2echo"
NXS2ECHO="/TESTING/SNS/stcdata_devel/nxs2echo.dummy"

BASENAME="/usr/bin/basename"
MKDIR="/usr/bin/mkdir"
CHGRP="/usr/bin/chgrp"
CHMOD="/usr/bin/chmod"
AWK="/usr/bin/awk"
CAT="/usr/bin/cat"
LS="/usr/bin/ls"

# Command Script Entry - Announce Our Existence...

echo -e "STC Pre-Post-Processing NeXus-to-Echo Command Script Entry."

echo -e "   [$0]"

echo -e "   [$@]"

script=`${BASENAME} "$0"`

# Parse Command Line Parameters...

base_path=""
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

verbose=0

for arg in "$@" ; do

	#echo -e "arg=$arg"

	key=`echo "$arg" | ${AWK} -F = '{print $1}'`
	value=`echo "$arg" | ${AWK} -F = '{print $2}'`

	#echo "arg=$arg, key=$key, value=$value"

	if [[ "${key}" == "base_path" ]]; then
		# Note: Base Path should be of the form "/foo", No Trailing Slash
		echo "Setting Data Base Path to [${value}]."
		base_path="${value}"
	elif [[ "${key}" == "facility" ]]; then
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

echo "base_path = [${base_path}]"
echo "facility = [${facility}]"
echo "beamline = [${beamline}]"
echo "beamline_prefix = [${beamline_prefix}]"
echo "ipts = [${ipts}]"
echo "proposal = [${proposal}]"
echo "run_number = [${run_number}]"

# Construct NeXus Data File Path/Name

data_path="${base_path}/${facility}/${beamline}"
echo -e "\nBeamline Data Path = [${data_path}]"

ipts_path="${data_path}/${ipts}"
echo -e "\nIPTS Path = [${ipts_path}]"

nexus_path="${ipts_path}/nexus"
echo -e "\nNeXus Data Path = [${nexus_path}]"

nexus_name="${beamline}_${run_number}.nxs.h5"
echo -e "\nNeXus Data File Name = [${nexus_name}]"

nexus_file="${nexus_path}/${nexus_name}"
echo -e "\nNeXus Source Data File Path = [${nexus_file}]"

echo
${LS} -l "${nexus_file}"

# Construct Echo Data File Path/Name

echo_path="${ipts_path}/echo"
echo -e "\nEcho Data Path = [${echo_path}]"

echo_name="s${run_number}.echo"
echo -e "\nExpected Echo Data File Name = [${echo_name}]"

echo_file="${echo_path}/${echo_name}"
echo -e "\nExpected Echo Destination Data File = [${echo_file}]"

# Ensure Echo Data Path Directory Exists

status=0

if [[ ! -d "${echo_path}" ]]; then

	${MKDIR} -p "${echo_path}"

	if [[ $? != 0 ]]; then
		echo -e "\nError Creating Echo Data Path Directory...!"
		status=1
	else

		echo -e "\nEcho Data Path Successfully Created in Archive:\n"
		ls -ld "${echo_path}"

		# Set Proper Group Ownership...
		${CHGRP} "${ipts}" "${echo_path}"
		chgrp_status=$?
		if [[ ${chgrp_status} != 0 ]]; then
			echo -e "\nWarning: Unable to Set IPTS Group Ownership...!"
			echo -e "\n\t[IPTS = ${ipts}]"
			echo -e "\n\t[Status = ${chgrp_status}]"
		fi

		# Set Proper Group Permissions...
		${CHMOD} g+ws "${echo_path}"
		chmod_status=$?
		if [[ ${chmod_status} != 0 ]]; then
			echo -e "\nWarning: Unable to Set IPTS Group Permissions...!"
			echo -e "\n\t[Status = ${chmod_status}]"
		fi

	fi

else
	echo -e "\nEcho Data File Path Already Exists in Archive:\n"
	${LS} -ld "${echo_path}"
fi

# Echo Data Path in Archive is Ok, Proceed...

if [[ ${status} == 0 ]]; then

	# Construct Nxs2Echo Command Line

	command="${NXS2ECHO} ${nexus_file} --outdir ${echo_path}"
	echo -e "\nNxs2Echo Command Line = [${command}]"

	# Execute Nxs2Echo Python Program

	eval "${command}"
	echo_status=$?

	if [[ ${echo_status} != 0 ]]; then

		echo -e "\nError: Non-Zero Status Code Returned from Nxs2Echo...!"
		echo -e "\n\t[${echo_status}]"

		status=${echo_status}

	elif [[ ! -f ${echo_file} ]]; then

		echo -e "\nError: Echo Data File Does Not Exist/Unreadable...!"
		echo
		${LS} -l "${echo_file}"

		status=-1

	else

		echo -e "\nFound Expected Echo Data File:"
		echo -e "\n\t[${echo_file}]"

		echo
		${LS} -l "${echo_file}"

		echo
		${CAT} "${echo_file}"

	fi

fi


# Do Nothing and Exit...

echo -e "\nExiting with Status = ${status}.\n"

exit ${status}

