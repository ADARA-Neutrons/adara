#!/usr/bin/env python
"""
Python Script for Copying Image Files
from the MCP PC at SNAP/VENUS/CG1D Beamlines
into the IPTS folders in the SNS/HFIR file archive.

Script is triggered by the ADARA/STC as a
"Pre-Post Processing" Command Script,
using proper STC Config File conditions on EPICS PVs
in the data stream, and including select PV values
as command line parameters, of the form:

   stc_pre_post_imaging.py --key1=value1 --key2=value2 . . .

@author Ray Gregory and Jeeem Kohl
"""

import sys

# Command Script Entry - Announce Our Existence...

if __name__ == "__main__":

	print("\nSTC Pre-Post AutoReduction Command Script Entry.\n")

	print("   [%s]\n" % sys.argv[0])

	print("   [%s]" % sys.argv)

	# Dump Command Line Parameters...

	for arg in sys.argv[1:]:

		print("\narg=%s" % arg)

		pargs = arg.split('=')

		key = pargs[0]
		if len(pargs) > 1:
			value = pargs[1]
		else:
			value = ""

		print("key=%s" % key)
		print("value=%s" % value)

	# Do Nothing and Exit (for now)...

	print("\nDo Nothing and Exit with Status 123.\n")

	sys.exit(123)

