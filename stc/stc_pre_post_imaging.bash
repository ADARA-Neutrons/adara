#!/bin/bash

# Command Script Entry - Announce Our Existence...

echo -e "\nSTC Pre-Post AutoReduction Command Script Entry.\n"

echo -e "Hello, STC."

echo -e "   [$0]\n"

echo -e "   [$@]"

# Dump Command Line Parameters and record important ones.
proposal="0"
SubDir=""
RunNumber="0"
for arg in "$@" ; do

	echo -e "\narg=$arg"

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

	echo "key=$key"
	echo "value=$value"

	case $key in

		"run_number")
			RunNumber=$value
			;;

		"proposal")
			proposal=$value
			;;

		"SubDir")
			SubDir=$value
			;;

		*)
			echo "\nCouldn't find key: ${key}"
			;;
	esac
done < <(find . -type f)

echo "proposal=$proposal"
echo "SubDir=$SubDir"
echo "RunNumber=$RunNumber"

# Determine files in directory.
IMAGE_FILES_DIR="/mcp/${proposal}/${SubDir}"
NEW_IMAGE_FILES_DIR="/SNS/SNAP/${proposal}/images/mcp/${SubDir}"
echo -e "\nImage Directory: ${IMAGE_FILES_DIR}"
echo -e "\nNew Image Directory: ${NEW_IMAGE_FILES_DIR}"

# Ensure new image files directory exists.
mkdir -p $NEW_IMAGE_FILES_DIR

find $IMAGE_FILES_DIR -regex ".*/Run_${RunNumber}_[^/]*" -print0 | while read -d $'\0' file
do
	echo -e "\n$file"
	cp $file "${NEW_IMAGE_FILES_DIR}/"
done

# Do Nothing and Exit (for now)...

echo -e "\nDo Nothing and Exit with Status 123.\n"

exit 123

