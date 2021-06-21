#!/bin/bash

# Command Script Entry - Announce Our Existence...

echo -e "\nSTC Pre-Post AutoReduction Command Script Entry.\n"

echo -e "   [$0]\n"

echo -e "   [$@]"

# Dump Command Line Parameters...

for arg in "$@" ; do

	echo -e "\narg=$arg"

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

	echo "key=$key"
	echo "value=$value"

done

# Do Nothing and Exit (for now)...

echo -e "\nDo Nothing and Exit with Status 123.\n"

exit 123

