#!/bin/sh

# Test script 4 for stc_pre_post_imaging.py - Announce Our Existence...

# FileName=C:/data/IPTS-26687/20210802_Run_51945_dinosaur_0020_0549
# SubDir=
# facility=SNS
# beamline=SNAP
# proposal=IPTS-26687
# run_number=51945
# source_dir=~/BL3_SNAP/mcp
# target_dir=~/BL3_SNAP/archive

echo -e "\nTest Script 1 for STC Pre-Post-Processing Imaging.\n"

python stc_pre_post_imaging.py FileName=C:/data/IPTS-26687/20210802_Run_51945_dinosaur_0020_0549 SubDir= facility=SNS beamline=SNAP proposal=IPTS-26687 run_number=51945 source_dir=~/BL3_SNAP/mcp target_dir=~/BL3_SNAP/archive
