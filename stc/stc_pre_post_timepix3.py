#!/usr/bin/env python3
"""
Python Script for Archiving Image Files for Timepix3 at CG1D.

Script is triggered by the ADARA/STC as a
"Pre-Post Processing" Command Script,
using proper STC Config File conditions on EPICS PVs
in the data stream, and including select PV values
as command line parameters, of the form:

   stc_pre_post_timepix3.py --key1=value1 --key2=value2 . . .

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
	source_dir = file_path.replace('/data/','/data-cg1d/')
	lead_dir_1, subdir_1 = split_leading_directory(source_dir)
	lead_dir_2, subdir_2 = split_leading_directory(subdir_1)
	ipts_dir, new_subdir = split_leading_directory(subdir_2)
	print('\n\nsource_dir: {}\nlead_dir_2: {}\nsubdir_2: {}\nipts_dir: {}\n new_subdir: {}\n\n'.format(
		source_dir, lead_dir_2, subdir_2, ipts_dir, new_subdir))
	return source_dir, ipts_dir, new_subdir


def determine_source_and_target_directories(source_dir, ipts_dir, target_dir, proposal, new_subdir, run_number):
	"""
	Determines source and target directories for copying.
	"""
	if source_dir is not None:
		# expand away tilde if present.
		source_dir = os.path.expanduser(source_dir)
	else:
		source_dir = '/mcp'

	# Check lead directory for match with proposal.
	if not ipts_dir == proposal:
		print(f'\n\nWARNING: Unexpected input: ipts directory ({ipts_dir}) does not match current proposal ({proposal}).\n\n')
	
	initial_image_dir = source_dir
	if target_dir is not None:
		# expand away tilde if present.
		target_dir = os.path.expanduser(target_dir)
	else:
		target_dir = '/HFIR/CG1D'
	new_image_dir = "{}/{}/images/timepix3/{}/Run_{}".format(target_dir, proposal, new_subdir, run_number) 
	
	print('\n\ninitial_image_dir: {}\nnew_image_dir: {}\n\n'.format(initial_image_dir, new_image_dir))
	return initial_image_dir, new_image_dir

def determine_raw_tpx3_directories(target_dir, proposal, new_subdir, run_number):
	"""
	Determines source and target directories for copying.
	"""
	tpx3_base = '/mcp-cg1d/tpx3files'
	initial_tpx3_dir = "{}/{}/Run_{}".format(tpx3_base, proposal, run_number) 

	if target_dir is not None:
		# expand away tilde if present.
		target_dir = os.path.expanduser(target_dir)
	else:
		target_dir = '/HFIR/CG1D'
	new_tpx3_dir = "{}/{}/images/timepix3/{}/Run_{}/tpx3".format(target_dir, proposal, new_subdir, run_number) 
	
	print('\n\ninitial_tpx3_dir: {}\nnew_tpx3_dir: {}\n\n'.format(initial_tpx3_dir, new_tpx3_dir))
	return initial_tpx3_dir, new_tpx3_dir

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

def get_tpx3_files_to_copy(initial_image_dir):
	"""
	Gets raw TPX3 files for the specified run that need to be copied.
	"""
	files_to_copy_ini = os.listdir(initial_image_dir)
	# print('\n\nfiles_to_copy_ini:\n{}\n\n'.format('\n'.join(str(f) for f in files_to_copy_ini)))
	files_to_copy = []
	for file_in_dir in files_to_copy_ini:
			files_to_copy.append(os.path.join(initial_image_dir, file_in_dir))

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

def copy_tpx3_files_batch(initial_image_dir, target_dir):
	"""
	Copy specified raw tpx3 files to specified target directory using a single rsync command.
	"""
	initial_image_dir = initial_image_dir.rstrip('/') + '/'
	arg_list = ["--include=*", "--exclude=*", initial_image_dir, target_dir]
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


def get_target_files_patiently(initial_image_dir, run_number, target_dir, wait_period_sec=30.0, interval_period_sec=5.0, for_main_image_files=True):
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
		if for_main_image_files:
			source_files_now = get_files_to_copy(initial_image_dir, run_number)
		else:
			source_files_now = get_tpx3_files_to_copy(initial_image_dir)
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


def copy_images(proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name):
	"""
	Copies image files for the specified run.
	"""
	print('\n\nIn copy_images().\nproposal: {}\nrun_number: {}\n\n'.format(proposal, run_number, tiff_file_path, tiff_file_name))

	# Determine proper subdirectories.
	source_dir, ipts_dir, new_subdir = determine_subdirectories(tiff_file_path)

	# Determine source and target directories.
	initial_image_dir, new_image_dir = determine_source_and_target_directories(source_dir, ipts_dir, target_dir, proposal, new_subdir, run_number)
			
	# Identify target files (for use in cataloging). Wait for file count to be stable for at least 60.0 seconds.
	# target_files = get_target_files(initial_image_dir, run_number, new_image_dir)
	target_files = get_target_files_patiently(initial_image_dir, run_number, new_image_dir, wait_period_sec=60.0)

	print('\n\nIn copy_images().\ninitial_image_dir: {}\nnew_image_dir: {}\n\n'.format(initial_image_dir, new_image_dir))

	# Assure target directory exists.
	assure_directory_exists(new_image_dir)
	copy_files_batch(initial_image_dir, new_image_dir, run_number)

	# ---------------------
	# Handle raw tpx3 files
	initial_tpx3_dir, new_tpx3_dir = determine_raw_tpx3_directories(target_dir, proposal, new_subdir, run_number)
	# Identify target tpx files. Wait for file count to be stable for at least 60.0 seconds.
	target_files = get_target_files_patiently(initial_tpx3_dir, run_number, new_tpx3_dir, wait_period_sec=60.0, for_main_image_files=False)

	print('\n\nIn copy_images(); raw tpx3 portion.\ninitial_tpx3_dir: {}\nnew_tpx3_dir: {}\n\n'.format(initial_tpx3_dir, new_tpx3_dir))

	# Assure target directory exists.
	assure_directory_exists(new_tpx3_dir)
	copy_tpx3_files_batch(initial_tpx3_dir, new_tpx3_dir)

	return target_files


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
			print('\nNonapplicable file: {}. Skipping catalog./n'.format(file_path))
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
		if key == "proposal":
			proposal = value
		elif key == "run_number":
			run_number = value
		elif key == "source_dir":
			source_dir = value
		elif key == "target_dir":
			target_dir = value
		elif key == "TIFFFilePath":
			tiff_file_path = value
		elif key == "TIFFFileName":
			tiff_file_name = value

	return proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name


def do_pre_post_timepix3(arg_list):
	"""
	Do pre-post-processing for CG1D Timepix3 Imaging.
	"""
	return_code = 0
	try:
		print('\n\nPre-Post-Processing for Timepix3.\n\n')

		proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name = process_args(arg_list)

		parms_present = all (p is not None for p in [proposal, run_number, tiff_file_path, tiff_file_name])

		if parms_present is not None:
			files_to_catalog = copy_images(proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name)
			catalog_images(files_to_catalog)
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
	# Test parameters: [['/TESTING/SNS/stcdata_devel/stc_pre_post_timepix3.py', 'FileName=C:/data/IPTS-26687/DAS_TEST_8_1_21/20210803_Run_51901_sample_file_0001_0605', 'SubDir=DAS_TEST_8_1_21', 'facility=SNS', 'beamline=HFIR', 'proposal=IPTS-26687', 'run_number=51901']]
	print("\nSTC Pre-Post AutoReduction Timepix3 Command Script Entry.\n")
	print("   [%s]\n" % sys.argv[0])
	print("   [%s]" % sys.argv)

	do_pre_post_timepix3(sys.argv[1:])
