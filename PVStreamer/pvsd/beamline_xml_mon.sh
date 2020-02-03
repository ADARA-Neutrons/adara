#!/bin/bash
#
# Beamline XML Monitor
#
# (Cron) Script to Monitor Local Beamline.XML Configuration File
# for Any Uncommitted Changes
#

# Source Local EPICS Beamline Environment (e.g. $BEAMLINE):
. /home/controls/share/master/scripts/beamline_profile.sh

cd "/home/controls/$BEAMLINE"

git diff beamline.xml

