#!/bin/bash
# $Id$
#
# Copyright (c) 2012  Oak Ridge National Laboratory  
#                     All rights reserved.
#
# Licensed under the MIT license,
# See COPYING file for full text.
#
#
# Descr: Simple script to validate XML file using the 'xmllint' utility.
#        (See also: http://xmlsoft.org/xmllint.html)
#

usage () {
    local retval
    retval=$1

    echo "Usage: $0  RunInfo_Schema.xsd RunInfo.xml"

    exit ${retval} 
}

if [ $# -lt 2 ] ; then
    usage 1
fi

if [ ! -f "$1" ] ; then
    echo "Error: '$1' file not found!"
    usage 1
fi

if [ ! -f "$2" ] ; then
    echo "Error: '$2' file not found!"
    usage 1
fi

XSD_FILE="$1"
XML_FILE="$2"

echo "-------------------------------------------------------------------"
echo " >> SCHEMA FILE: '$XSD_FILE' <<"
echo " >>    XML FILE: '$XML_FILE' <<"
echo ""

#XSD_FILE=runInfo.v0.xsd
#XML_FILE=runInfo.xml
xmllint --noout --schema "$XSD_FILE"  "$XML_FILE"
rc=$?

###################################
# From XMLLINT(1) Manual Page
#
# DIAGNOSTICS
#  xmllint return codes provide information that can be used when calling
#  it from scripts.
#
#       0      No error
#       1      Unclassified
#       2      Error in DTD
#       3      Validation error
#       4      Validation error
#       5      Error in schema compilation
#       6      Error writing output
#       7      Error in pattern (generated when --pattern option is used)
#       8      Error in Reader registration (generated when --chkregister
#              option is used)
#       9      Out of memory error
#
###################################

echo ""
if test $rc -eq 0  ; then
    echo "SUCCESS: '$XSD_FILE' validation passed."
else
    echo "ERROR: Validation failed - '$XSD_FILE'"
fi
echo "-------------------------------------------------------------------"

exit $?
