#!/usr/bin/env python3
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
import os
import errno
import re
import time
import subprocess
import traceback
import requests
import pathlib


def split_leading_directory(file_path):
	"""
	Splits away leading directory of path.

	Presumes '/' path delimiter.
	"""
	delim = '/'
	path_split = file_path.split(delim)
	if len(path_split) > 0:
		lead_dir = path_split[0]
	else:
		lead_dir = ''
	if len(path_split) > 1:
		rest = delim.join(path_split[1:])
	else:
		rest = ''
	return lead_dir, rest


def remove_drive(file_path):
	"""
	Remove drive from specified Windows path.
	"""
	if ':' in file_path:
		path_parts = pathlib.PureWindowsPath(file_path.replace('/', '\\')).parts
		if len(path_parts) > 1:
			driveless_path = '\\' + '\\'.join(path_parts[1:])
		else:
			driverless_path = '\\'
	else:
		driveless_path = file_path
	return driveless_path


def determine_subdirectories(file_path):
	"""
	Determines image file subdirectory underneath IPTS directory.
	"""
	head_tail = os.path.split(file_path.replace('\\', '/'))
	driveless_path = remove_drive(head_tail[0]).replace('\\', '/') 
	subdir = driveless_path.replace('/data/','')
	lead_dir, subdir = split_leading_directory(subdir)
	if subdir:
		new_subdir = subdir
	else:
		new_subdir = 'SubdirectoryUnspecified'
	# Validate path
	path_valid = re.search('^/data/IPTS-\d+', driveless_path.strip()) is not None
	print('\n\nhead_tail: {}\ndriveless_path: {}\nlead_dir: {}\nsubdir: {}\nnew_subdir: {}\npath_valid: {}\n\n'.format(
		head_tail, driveless_path, lead_dir, subdir, new_subdir, path_valid))
	return subdir, new_subdir, lead_dir, path_valid


def determine_source_and_target_directories(source_dir, lead_dir, target_dir, proposal, subdir, new_subdir, run_number):
	"""
	Determines source and target directories for copying.
	"""
	if source_dir is not None:
		# expand away tilde if present.
		source_dir = os.path.expanduser(source_dir)
	else:
		source_dir = '/mcp'

	# Check lead directory for match with proposal.
	if not lead_dir == proposal:
		print(f'\n\nWARNING: Unexpected input: lead directory ({lead_dir}) does not match current proposal ({proposal}).\n\n')
	
	initial_image_dir = "{}/{}/{}".format(source_dir, lead_dir, subdir)
	if target_dir is not None:
		# expand away tilde if present.
		target_dir = os.path.expanduser(target_dir)
	else:
		target_dir = '/SNS/SNAP'
	new_image_dir = "{}/{}/images/mcp/{}/Run_{}".format(target_dir, proposal, new_subdir, run_number) 
	
	print('\n\ninitial_image_dir: {}\nnew_image_dir: {}\n\n'.format(initial_image_dir, new_image_dir))
	return initial_image_dir, new_image_dir


def get_files_to_copy(initial_image_dir, run_number):
	"""
	Gets files for the specified run that need to be copied.
	"""
	files_to_copy_ini = os.listdir(initial_image_dir)
	# print('\n\nfiles_to_copy_ini:\n{}\n\n'.format('\n'.join(str(f) for f in files_to_copy_ini)))
	files_to_copy = []
	for file_in_dir in files_to_copy_ini:
		if re.search('Run_{}'.format(run_number), file_in_dir):
			files_to_copy.append(os.path.join(initial_image_dir, file_in_dir))

	# print('\n\nNumber of files to copy:\n{}\n\n'.format(len(files_to_copy)))
	# Temporarily only print last 15 characters of file name to reduce log file load.	
	# print('\n\nfiles_to_copy (last 15 chars):\n{}\n\n'.format('\n'.join(str(f[-15:]) for f in files_to_copy)))
	# print('\n\nfiles_to_copy:\n{}\n\n'.format('\n'.join(str(f) for f in files_to_copy)))
	# print('\n\nfiles_to_copy:\n{}\n\n'.format(files_to_copy))
	return files_to_copy


def assure_directory_exists(directory):
	"""
	Assure the specified directory exists.
	"""
	try:
		os.makedirs(directory)
	except OSError as e:
		if e.errno != errno.EEXIST:
			raise


def run_rsync(arg_list):
	"""
	Run rsync with default options using specified arguments..
	"""
	try:
		# print('\n\nCopying [{}] to [{}].\n\n'.format(source_file, target_file))
		# command = ['rsync', '--list-only' , '-avz']
		command = ['rsync' , '-avz']
		command += arg_list
		p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		stdo, stde = p.communicate()
		if p.returncode != 0:
			raise Exception("Failed to run rsync command {} with return code of {}. {}".format(' '.join(command), p.returncode, stde))
		else:
			print("\n\nDone running rsync command {} with return code of {}. {}\n\n".format(' '.join(command), p.returncode, stdo))
	except Exception as ex:
		raise Exception("Failed to run rsync command {}. {}".format(' '.join(command), ex))


def copy_file(source_file, target_file):
	"""
	Copy a file to a target location.
	"""
	# print('\n\nCopying [{}] to [{}].\n\n'.format(source_file, target_file))
	run_rsync([source_file, target_file])


def copy_files_batch(initial_image_dir, target_dir, run_number):
	"""
	Copy specified source files to specified target directory using a single rsync command.
	"""
	initial_image_dir = initial_image_dir.rstrip('/') + '/'
	arg_list = ["--include=*Run_{}*".format(run_number), "--exclude=*", initial_image_dir, target_dir]
	run_rsync(arg_list)


def copy_files_individually(source_files, target_dir):
	"""
	Copy specified source files to specified target directory.
	"""
	for source_file in source_files:
		head_tail = os.path.split(source_file)
		copy_file(source_file, os.path.join(target_dir, head_tail[1])) 


def get_target_files(initial_image_dir, run_number, target_dir):
	"""
	Construct list of taget files.
	"""
	source_files = get_files_to_copy(initial_image_dir, run_number)
	target_files = []
	for source_file in source_files:
		head_tail = os.path.split(source_file)
		target_files.append(os.path.join(target_dir, head_tail[1]))
	return target_files


def get_target_files_patiently(initial_image_dir, run_number, target_dir, wait_period_sec=30.0, interval_period_sec=5.0):
	"""
	Construct list of taget files, but don't presume they all exist yet.

	Wait until there has been no increase in the number of files for the specified wait period.
	Files will be checked every interval according to the specified interval period.

	"""
	stable_files_count_time = 0.0
	source_files = get_files_to_copy(initial_image_dir, run_number)
	file_count = len(source_files)
	time_val = time.time()
	print('\nWaiting for stable file count. Current number of files to copy:{} stable file count time: {} (sec)\n'.format(file_count, stable_files_count_time))
	while stable_files_count_time < wait_period_sec:
		time.sleep(interval_period_sec)
		source_files_now = get_files_to_copy(initial_image_dir, run_number)
		file_count_now = len(source_files_now)
		time_val_now = time.time()
		if file_count_now == file_count:
			stable_files_count_time += (time_val_now - time_val)
		else:
			stable_files_count_time = 0.0
		time_val = time_val_now
		source_files = source_files_now
		file_count = file_count_now
		print('\nWaiting for stable file count. Current number of files to copy:{} stable file count time: {} (sec)\n'.format(file_count, stable_files_count_time))

	target_files = []
	for source_file in source_files:
		head_tail = os.path.split(source_file)
		target_files.append(os.path.join(target_dir, head_tail[1]))
	return target_files


def copy_images(file_path, proposal, run_number, source_dir, target_dir):
	"""
	Copies image files for the specified run.
	"""
	print('\n\nIn copy_images().\nfile_path: {}\nproposal: {}\nrun_number: {}\n\n'.format(file_path, proposal, run_number))

	# Determine proper subdirectories.
	subdir, new_subdir, lead_dir, path_valid = determine_subdirectories(file_path)

	if path_valid:
		# Determine source and target directories.
		initial_image_dir, new_image_dir = determine_source_and_target_directories(source_dir, lead_dir, target_dir, proposal, subdir, new_subdir, run_number)
				
		# Identify target files (for use in cataloging). Wait for file count to be stable for at least 30.0 seconds.
		# target_files = get_target_files(initial_image_dir, run_number, new_image_dir)
		target_files = get_target_files_patiently(initial_image_dir, run_number, new_image_dir, wait_period_sec=30.0)

		print('\n\nIn copy_images().\ninitial_image_dir: {}\nnew_image_dir: {}\n\n'.format(initial_image_dir, new_image_dir))

		# Assure target directory exists.
		assure_directory_exists(new_image_dir)

		# copy_files_individually(files_to_copy, new_image_dir)
		copy_files_batch(initial_image_dir, new_image_dir, run_number)
	else:
		print('\n\nIn copy_images().\nInvalid Path. Now images copied.\n\n')
		target_files = []

	return target_files, path_valid


def get_creds():
	creds_path = pathlib.Path(__file__).parent.resolve()
	file_name = 'creds_do_not_persist.txt'
	file_path = os.path.join(creds_path, file_name) 
	with open(file_path, 'r') as file:
		creds = file.read().replace('\n', '')
	return creds


def should_catalog_file(file_path):
	# extension_list = []
	extension_list = ['.tiff', '.fits']
	split_path = os.path.splitext(file_path)
	extension = split_path[1]
	return extension in extension_list


def catalog_images(files_to_catalog, creds=None):
	creds = get_creds()
	for file_path in files_to_catalog:
		should_catalog = should_catalog_file(file_path)
		print('\n\nIn catalog_images(). file_path: {} should_catalog: {}\n\n'.format(file_path, should_catalog))
		if should_catalog:
			response = requests.post(
				"https://oncat.ornl.gov/api/datafiles{}/ingest".format(file_path),
				headers={"Authorization": "Bearer {}".format(creds)},
			)

			try:
				# Raise on any errors.
				response.raise_for_status()
			except requests.exceptions.HTTPError as e:
				# Handle any errors.  Assume that the network could go down,
				# ONCat could go down, the network mount available to ONCat could go
				# down, etc., etc.
				print("\n\nCataloging ERROR for file {}: {}\n\n".format(file_path, e.response.json()))
				raise
		else:
			print('\nNonapplicable file: {}. Skipping catalog.\n'.format(file_path))
			pass

def finish_up(rtn_code):
	"""
	Exit with specified return code.
	"""
	print("\nFinishing up with return code {}\n".format(rtn_code))
	sys.exit(rtn_code)


def process_args(arg_list):
	"""
	Process command line arguments.
	"""
	file_path = None
	proposal = None
	run_number = None
	source_dir = None
	target_dir = None

	# Loop through Command Line Parameters...
	for arg in arg_list:
		print("\narg=%s" % arg)
		pargs = arg.split('=')
		key = pargs[0]
		if len(pargs) > 1:
			value = pargs[1]
		else:
			value = ""
		print("key=%s" % key)
		print("value=%s" % value)
		if key == "FileName":
			file_path = value
		elif key == "proposal":
			proposal = value
		elif key == "run_number":
			run_number = value
		elif key == "source_dir":
			source_dir = value
		elif key == "target_dir":
			target_dir = value
	return file_path, proposal, run_number, source_dir, target_dir


def do_pre_post_imaging(arg_list):
	"""
	Do pre-post-processing for SNAP MCP Imaging.
	"""
	return_code = 0
	try:
		file_path, proposal, run_number, source_dir, target_dir = process_args(arg_list)

		parms_present = all (p is not None for p in [file_path, proposal, run_number])

		if parms_present is not None:
			files_to_catalog, path_valid = copy_images(file_path, proposal, run_number, source_dir, target_dir)
			if files_to_catalog is not None and (len(files_to_catalog) > 0):
				catalog_images(files_to_catalog)
			else:
				if path_valid:
					# Fail on not finding image files. This could help with intermittent mount connection issues.
					print('\n\nERROR: No image files found.\n\n')
					return_code = -3								
		else:
			print('\n\nERROR: Not all parameters present.\n\n')
			return_code = -2
	except Exception as e:
		print('\n\nERROR In do_pre_post_imaging(): {}\n\n'.format(str(e)))
		print(traceback.format_exc())
		return_code = -1
	finally:
		finish_up(return_code)


if __name__ == "__main__":
	# Test parameters: [['/TESTING/SNS/stcdata_devel/stc_pre_post_imaging.py', 'FileName=C:/data/IPTS-26687/DAS_TEST_8_1_21/20210803_Run_51901_sample_file_0001_0605', 'SubDir=DAS_TEST_8_1_21', 'facility=SNS', 'beamline=SNAP', 'proposal=IPTS-26687', 'run_number=51901']]
	print("\nSTC Pre-Post AutoReduction Imaging Command Script Entry.\n")
	print("   [%s]\n" % sys.argv[0])
	print("   [%s]" % sys.argv)

	do_pre_post_imaging(sys.argv[1:])
