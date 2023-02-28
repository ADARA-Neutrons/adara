#!/bin/bash

# Command Script Entry - Announce Our Existence...

echo -e "STC Pre-Post AutoReduction Dummy Command Test Script Entry."

echo -e "   [$0]"

echo -e "   [$@]"

# Dump Command Line Parameters...

for arg in "$@" ; do

	#echo -e "arg=$arg"

	key=`echo "$arg" | awk -F = '{print $1}'`
	value=`echo "$arg" | awk -F = '{print $2}'`

	echo -n "arg=$arg, key=$key, value=$value ; "

done

# Do Nothing and Exit...

echo -e "\nDo Nothing and Exit with Status 0."

exit 0

