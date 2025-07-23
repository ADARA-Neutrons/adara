#!/usr/bin/env python3
"""
Python Script for Archiving Image Files for Timepix3.

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
    lead_dir_3, subdir_3 = split_leading_directory(subdir_2)
    ipts_dir, new_subdir = split_leading_directory(subdir_3)
    print('source_dir: {}\nlead_dir_2: {}\nsubdir_2: {}\nipts_dir: {}\n new_subdir: {}\n'.format(
        source_dir, lead_dir_3, subdir_3, ipts_dir, new_subdir))
    return source_dir, ipts_dir, new_subdir


def determine_source_and_target_directories(beamline, source_dir, ipts_dir, target_dir, proposal, new_subdir, run_number):
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
        print(f'WARNING: Unexpected input: ipts directory ({ipts_dir}) does not match current proposal ({proposal}).\n')
    
    initial_image_dir = source_dir.replace('/mcp/', '/mcp-tpx3/', 1)
    if target_dir is not None:
        # expand away tilde if present.
        target_dir = os.path.expanduser(target_dir)
    else:
        if beamline in ['CG1D']:
            target_dir = f'/HFIR/{beamline}'
        else:
            target_dir = f'/SNS/{beamline}'

    # Proposal is already in path. Don't need it in subdir.
    image_subdir = new_subdir.replace(proposal + '/', '')
    print('In determine_source_and_target_directories():new_subdir {} proposal: {} image_subdir: {}\n'.format(new_subdir, proposal, image_subdir))

    new_image_dir = "{}/{}/images/mcp/Run_{}/{}".format(target_dir, proposal, run_number, image_subdir) 
    
    print('initial_image_dir: {}\nnew_image_dir: {}\n'.format(initial_image_dir, new_image_dir))
    return initial_image_dir, new_image_dir

def determine_raw_img_directories(beamline, target_dir, proposal, run_number, image_file_path, alt_image_file_path, detector_type, det_sub_dir):
    """
    Determines source and target directories for copying.
    """
    if beamline in ['CG1D']:
        img_base = '/mcp-cg1d'
    elif beamline in ['VENUS']:
        # Just Use Numerical Values from Enum for Now... ;-/
        if int(detector_type) == 3:  # 'MCP TPX'
            img_base = '/mcp-bl10-tpx'
        elif int(detector_type) == 4:  # 'Timepix 3'
            img_base = '/img-bl10'
        elif int(detector_type) == 7:  # 'QHY600 sCMOS'
            img_base = '/img-bl10'
        else:
            img_base = '/img-bl10'
        print('VENUS Detector Type {} -> Base Directory = [{}].\n'.format(str(detector_type), str(img_base)))
    else:
        # img_base = '/mcp-cg1d/tpx3files'
        img_base = '/mcp-tpx3/tpx3files'

    if beamline in ['VENUS']:
        # Make sure we have an ImageFilePath Subdirectory...!
        # Otherwise, We Copy Over the ENTIRE MCP/IPTS Proposal Folder!! ;-o
        if image_file_path == "":
            print("ERROR Constructing Raw Image Directory - ImageFilePath is Empty [{}]\n".format(image_file_path))
            raise
        # Ok to Use ImageFilePath, hopefully... ;-D
        else:
            initial_img_dir = "{}/{}/{}".format(img_base, proposal, image_file_path) 
            # Check Whether This Directory Exists...
            if not os.path.isdir(initial_img_dir):
                # If Not, Then Try Alternate ImageFilePath...
                print("Warning: Raw Image Directory Does Not Exist [{}]!\n".format(initial_img_dir))
                print("Trying Alternate ImageFilePath = [{}]!\n".format(alt_image_file_path))
                initial_img_dir = "{}/{}/{}".format(img_base, proposal, alt_image_file_path) 
                # Also Munge ImageFilePath for Subdirectory Below... ;-D
                image_file_path = alt_image_file_path
    else:
        initial_img_dir = "{}/{}/Run_{}".format(img_base, proposal, run_number) 

    if target_dir is not None:
        # expand away tilde if present.
        target_dir = os.path.expanduser(target_dir)
    else:
        if beamline in ['CG1D']:
            target_dir = f'/HFIR/{beamline}'
        else:
            target_dir = f'/SNS/{beamline}'

    if beamline in ['VENUS']:
        ### OLD WAY - NO LONGER USED...
        ### Extract 1st Subdirectory Path from ImageFilePath
        ### and Use in Archive Path...
        ##sub_dir = image_file_path.split('/')
        ##new_img_dir = "{}/{}/images/mcp/{}/Run_{}/{}".format(target_dir, proposal, sub_dir[0], run_number, det_sub_dir) 
        ### NEW WAY - Just Use Entire Subdirectory Path from ImageFilePath
        new_img_dir = "{}/{}/{}".format(target_dir, proposal, image_file_path) 
    else:
        # new_img_dir = "{}/{}/raw/Run_{}/tpx3".format(target_dir, proposal, run_number) 
        new_img_dir = "{}/{}/images/mcp/Run_{}/{}".format(target_dir, proposal, run_number, det_sub_dir) 
    
    print('initial_img_dir: {}\nnew_img_dir: {}\n'.format(initial_img_dir, new_img_dir))
    return initial_img_dir, new_img_dir

def get_files_to_copy(initial_image_dir, run_number):
    """
    Gets files for the specified run that need to be copied.
    """
    files_to_copy_ini = os.listdir(initial_image_dir)
    # print('files_to_copy_ini:\n{}\n'.format('\n'.join(str(f) for f in files_to_copy_ini)))
    files_to_copy = []
    for file_in_dir in files_to_copy_ini:
        if re.search('Run_{}'.format(run_number), file_in_dir):
            if not re.search('.*.done', file_in_dir):
                files_to_copy.append(os.path.join(initial_image_dir, file_in_dir))
            else:
                print('Skipping Done marker file for copy:\n{}\n'.format(file_in_dir))
                # REMOVEME

    # print('Number of files to copy:\n{}\n'.format(len(files_to_copy)))
    # Temporarily only print last 15 characters of file name to reduce log file load.    
    # print('files_to_copy (last 15 chars):\n{}\n'.format('\n'.join(str(f[-15:]) for f in files_to_copy)))
    # print('files_to_copy:\n{}\n'.format('\n'.join(str(f) for f in files_to_copy)))
    # print('files_to_copy:\n{}\n'.format(files_to_copy))
    return files_to_copy

def get_img_files_to_copy(initial_image_dir):
    """
    Gets raw img files for the specified run that need to be copied.
    """
    files_to_copy_ini = os.listdir(initial_image_dir)
    # print('files_to_copy_ini:\n{}\n'.format('\n'.join(str(f) for f in files_to_copy_ini)))
    files_to_copy = []
    for file_in_dir in files_to_copy_ini:
        if not re.search('.*.done', file_in_dir):
            files_to_copy.append(os.path.join(initial_image_dir, file_in_dir))
        else:
            print('Skipping Done marker file for copy:\n{}\n'.format(file_in_dir))
            # REMOVEME

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
    ret = -1
    try:
        # print('Copying [{}] to [{}].\n'.format(source_file, target_file))
        # command = ['rsync', '--list-only' , '-avz']
        command = ['rsync' , '-avz']
        command += arg_list
        p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdo, stde = p.communicate()
        ret = p.returncode
        if p.returncode != 0:
            raise Exception("Failed to run rsync command {} with return code of {}. {}".format(' '.join(command), p.returncode, stde))
        else:
            print("Done running rsync command {} with return code of {}. {}\n".format(' '.join(command), p.returncode, stdo))
    except Exception as ex:
        raise Exception("Failed to run rsync command {}. {}".format(' '.join(command), ex))
    return ret


def mark_file_done(source_file, target_file):
    """
    Mark a file as Done copying to the target location.
    """
    print('Creating Done Marker File for:\n{}\n'.format(source_file))
    # REMOVEME

    # Create Done Marker File Path
    source_dir = os.path.dirname(source_file)
    orig_file = os.path.basename(source_file)
    done = "." + orig_file + ".done"
    done_file = os.path.join(source_dir, done)
    print('Done Marker File Path:\n{}\n'.format(done_file))
    # REMOVEME

    # Obtain Size of the Target File in Bytes
    size_in_bytes = -1
    try:
        size_in_bytes = os.path.getsize(target_file)
        print('Size in Bytes of Target File:\n{}\n'.format(size_in_bytes))
        # REMOVEME
    except FileNotFoundError:
        print('Error: Target File Not Found:\n{}\n'.format(target_file))
    except Exception as e:
        print('Exception Obtaining Source File Size:\n{}\n'.format(str(e)))

    # Scribble Size in Bytes Into Done Marker File
    if size_in_bytes > 0:
        try:
            with open(done_file, 'w') as file:
                file.write('{}\n'.format(str(size_in_bytes)))
            print('Done Marker File Written Size={}:\n{}\n'.format(str(size_in_bytes), done_file))
        except PermissionError:
            print('Error: Permission Denied Writing Done File:\n{}\n'.format(done_file))
            # Maybe this should be an Exception, when we're more confident...?
        except IOError as e:
            print('IOError Writing Done File:\n{}\n{}\n'.format(done_file,str(e)))
            # Maybe this should be an Exception, when we're more confident...?
    else:
        print('Error Marking File Copy Done for:\n{}\n'.format(source_file))
        # Maybe this should be an Exception, when we're more confident...?

def copy_file(source_file, target_file):
    """
    Copy a file to a target location.
    """
    # print('Copying [{}] to [{}].\n'.format(source_file, target_file))
    ret = run_rsync([source_file, target_file])
    if ret == 0:
        mark_file_done(source_file, target_file)


def copy_files_batch(initial_image_dir, target_dir, run_number):
    """
    Copy specified source files to specified target directory using a single rsync command.
    """
    initial_image_dir = initial_image_dir.rstrip('/') + '/'
    arg_list = ["--include=*Run_{}*".format(run_number), "--exclude=*", initial_image_dir, target_dir]
    ret = run_rsync(arg_list)
    # Can't Easily Create ".*.done" Marker Files in Batch Copy Mode...!
    #    - if this method is truly needed, then it should be re-worked to
    #    explicitly select the desired Image/Tiff files by name, and then
    #    just use copy_files_individually() instead... ;-D
    # if ret == 0:
        # mark_file_done(source_file, target_file)

# No Longer Used... (originally used in copy_images()...)
def copy_img_files_batch(initial_image_dir, target_dir):
    """
    Copy specified raw img files to specified target directory using a single rsync command.
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


# No Longer Used... (originally used in copy_images()...)
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
    Construct list of target files, but don't presume they all exist yet.

    Wait until there has been no increase in the number of files for the specified wait period.
    Files will be checked every interval according to the specified interval period.

    """
    stable_files_count_time = 0.0
    if for_main_image_files:
        source_files = get_files_to_copy(initial_image_dir, run_number)
    else:
        source_files = get_img_files_to_copy(initial_image_dir)
    file_count = len(source_files)
    time_val = time.time()
    print('Waiting for stable file count. Current number of files to copy:{} stable file count time: {} (sec)\n'.format(file_count, stable_files_count_time))
    while stable_files_count_time < wait_period_sec:
        time.sleep(interval_period_sec)
        if for_main_image_files:
            source_files_now = get_files_to_copy(initial_image_dir, run_number)
        else:
            source_files_now = get_img_files_to_copy(initial_image_dir)
        file_count_now = len(source_files_now)
        time_val_now = time.time()
        if file_count_now == file_count:
            stable_files_count_time += (time_val_now - time_val)
        else:
            stable_files_count_time = 0.0
        time_val = time_val_now
        source_files = source_files_now
        file_count = file_count_now
        print('Waiting for stable file count. Current number of files to copy:{} stable file count time: {} (sec)\n'.format(file_count, stable_files_count_time))

    target_files = []
    for source_file in source_files:
        head_tail = os.path.split(source_file)
        target_files.append(os.path.join(target_dir, head_tail[1]))
    return source_files, target_files


def copy_images(beamline, proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name, image_file_path, alt_image_file_path, detector_type, det_sub_dir, include_tiff_files=True):
    """
    Copies image files for the specified run.
    """
    print('In copy_images().\nbeamline: {}\nproposal: {}\nrun_number: {}\n'.format(beamline, proposal, run_number, tiff_file_path, tiff_file_name))

    # Determine proper subdirectories.
    source_dir, ipts_dir, new_subdir = determine_subdirectories(tiff_file_path)

    # I _Think_ This Case is Redundant Now...
    #    - get_target_files_patiently() without "for_main_image_files=True"
    #    will just grab Every File in the initial_image_dir
    #    - however copy_files_batch() only copies the files with "Run_{}"
    #    run_number in the file names, so this is basically the "same thing"!
    # It is worth noting that if "include_tiff_files" is True (anywhere
    # except CG1D and VENUS), you would actually copy Every File *Twice*!!
    #    - this seems bad, like we should just eliminate the whole
    #    "include_tiff_files" option all together...
    # Nonetheless, please note that with copy_files_batch() here,
    # there are _No_ ".*.done" marker files created for local cleanup...! ;-D
    if include_tiff_files:
        try:
            # Determine source and target directories.
            initial_image_dir, new_image_dir = determine_source_and_target_directories(beamline, source_dir, ipts_dir, target_dir, proposal, new_subdir, run_number)
                    
            # Identify target files (for use in cataloging). Wait for file count to be stable for at least 60.0 seconds.
            # target_files = get_target_files(initial_image_dir, run_number, new_image_dir)
            source_files, target_files = get_target_files_patiently(initial_image_dir, run_number, new_image_dir, wait_period_sec=60.0)

            print('In copy_images().\ninitial_image_dir: {}\nnew_image_dir: {}\n'.format(initial_image_dir, new_image_dir))

            # Assure target directory exists.
            assure_directory_exists(new_image_dir)
            copy_files_batch(initial_image_dir, new_image_dir, run_number)
        except:
            e = sys.exc_info()
            print('ERROR In copy_images(). Trying to write TIFF files: {}\n'.format(str(e)))
            traceback.print_exc(limit=50, file=sys.stdout)

    # ---------------------
    # Handle raw img files
    initial_img_dir, new_img_dir = determine_raw_img_directories(beamline, target_dir, proposal, run_number, image_file_path, alt_image_file_path, detector_type, det_sub_dir)
    # Identify target img files. Wait for file count to be stable for at least 60.0 seconds.
    # Use the Same "Run_{}".format(run_number) Criteria for Image as TIFF...
    # -> for_main_image_files=True (default)
    # Otherwise We're Screwwwwed if Images from Multiple Runs Land in
    # the Same Subdirectory... ;-Q
    # source_files, target_files = get_target_files_patiently(initial_img_dir, run_number, new_img_dir, wait_period_sec=60.0, for_main_image_files=False)
    source_files, target_files = get_target_files_patiently(initial_img_dir, run_number, new_img_dir, wait_period_sec=60.0, for_main_image_files=True)

    print('In copy_images(); raw img portion.\ninitial_img_dir: {}\nnew_img_dir: {}\n'.format(initial_img_dir, new_img_dir))

    # Assure target directory exists.
    print('In copy_images(); Making sure directory exists: {}\n'.format(new_img_dir))
    assure_directory_exists(new_img_dir)
    print('In copy_images(); About to copy files: {} to target_dir: {}\n'.format(source_files, new_img_dir))
    copy_files_individually(source_files, new_img_dir)
    # copy_img_files_batch(initial_img_dir, new_img_dir)

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


#ONCAT = "https://oncat.ornl.gov"
ONCAT = "https://oncat-prod-1.ornl.gov"

def catalog_images(files_to_catalog, creds=None):
    creds = get_creds()
    for file_path in files_to_catalog:
        should_catalog = should_catalog_file(file_path)
        print('In catalog_images(). file_path: {} should_catalog: {}\n'.format(file_path, should_catalog))
        if should_catalog:
            response = requests.post(
                "{}/api/datafiles{}/ingest".format(ONCAT, file_path),
                headers={"Authorization": "Bearer {}".format(creds)},
                timeout=60.000,
            )

            # Be Willing to Retry a Few Times... (Say, 5... ;-D)
            done = False
            cnt = 0
            while ( not done ) and ( cnt < 5 ):
                try:
                    # Raise on any errors.
                    response.raise_for_status()
                    done = True
                except requests.exceptions.HTTPError as e:
                    # Handle any errors.  Assume that the network could go down,
                    # ONCat could go down, the network mount available to ONCat could go
                    # down, etc., etc.
                    cnt += 1
                    try:
                        print("Cataloging ERROR for file {}, Attempt {} of 5: {}\n".format(file_path, cnt, e.response.json()))
                    except:
                        print("Cataloging ERROR for file {}, Attempt {} of 5: Invalid JSON Response - {} {}\n".format(file_path, cnt, e.response.status_code, e.response.text))
                    # Give Catalog Time to "Un-Whatever" Itself... ;-D
                    time.sleep(60)
                except requests.exceptions.Timeout as t:
                    cnt += 1
                    print("Cataloging ERROR for file {}, Attempt {} of 5: Request Timed Out (timeout=60.000)\n".format(file_path, cnt))
                    # Give Catalog Time to "Un-Whatever" Itself... ;-D
                    time.sleep(60)

            # Did it (eventually) work...?
            # Give it One Last Try (if not just to raise the exception!)
            if not done:
                try:
                    # Raise on any errors.
                    response.raise_for_status()
                except requests.exceptions.HTTPError as e:
                    # Handle any errors.  Assume that the network could go down,
                    # ONCat could go down, the network mount available to ONCat could go
                    # down, etc., etc.
                    cnt += 1
                    try:
                        print("Cataloging ERROR for file {}, FINAL Attempt {}: {}\n".format(file_path, cnt, e.response.json()))
                    except:
                        print("Cataloging ERROR for file {}, FINAL Attempt {}: Invalid JSON Response - {} {}\n".format(file_path, cnt, e.response.status_code, e.response.text))
                    raise
                except requests.exceptions.Timeout as t:
                    cnt += 1
                    print("Cataloging ERROR for file {}, FINAL Attempt {}: Request Timed Out (timeout=60.000)\n".format(file_path, cnt))
                    raise

        else:
            print('Nonapplicable file: {}. Skipping catalog.\n'.format(file_path))
            pass

def finish_up(rtn_code):
    """
    Exit with specified return code.
    """
    print("Finishing up with return code {}\n".format(rtn_code))
    sys.exit(rtn_code)


def process_args(arg_list):
    """
    Process command line arguments.
    """
    beamline = None
    proposal = None
    run_number = None
    source_dir = None
    target_dir = None

    tiff_file_path = 'not_found_yet'
    tiff_file_name = 'not_found_yet'
    image_file_path = 'not_found_yet'
    tpx_file_path = 'not_found_yet'
    config_tpx_file_path = 'not_found_yet'
    config_tiff_file_path = 'not_found_yet'
    alt_image_file_path = 'not_found_yet'

    # Loop through Command Line Parameters...
    for arg in arg_list:
        print("arg=%s" % arg)
        pargs = arg.split('=')
        key = pargs[0]
        if len(pargs) > 1:
            value = pargs[1]
        else:
            value = ""
        print("key=%s" % key)
        print("value=%s\n" % value)
        if key == "beamline":
            beamline = value
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
        elif key == "TpxFilePath":
            tpx_file_path = value
        elif key == "ImageFilePath":
            image_file_path = value
        elif key == "ConfigTpxFilePath":
            config_tpx_file_path = value
        elif key == "ConfigTIFFFilePath":
            config_tiff_file_path = value
        elif key == "DetectorType":
            detector_type = value

    # Accept Alternate Image File Paths, Only If _Not_ Directly Specified!
    if image_file_path == 'not_found_yet':
        # Process Alternatives in "Least Extant" Order... ;-D
        # (I.e. Prefer Use/OverWrite with Most Recent Alternates... ;-)
        if tpx_file_path != 'not_found_yet':
            print('process_args(): Steal Missing ImageFilePath from TpxFilePath = [{}].\n'.format(str(tpx_file_path)))
            image_file_path = tpx_file_path
        if config_tiff_file_path != 'not_found_yet':
            print('process_args(): Steal Missing ImageFilePath from ConfigTIFFFilePath = [{}].\n'.format(str(config_tiff_file_path)))
            image_file_path = config_tiff_file_path
        if config_tpx_file_path != 'not_found_yet':
            print('process_args(): Steal Missing ImageFilePath from ConfigTpxFilePath = [{}].\n'.format(str(config_tpx_file_path)))
            image_file_path = config_tpx_file_path
    # We Have *Two* Valid Image File Paths, Better Keep 2nd as "Alternate"
    elif config_tpx_file_path != 'not_found_yet':
        print('process_args(): *BOTH* ImageFilePath from ConfigTpxFilePath are Set!\n')
        print('process_args(): Save ConfigTpxFilePath as Alternate = [{}].\n'.format(str(config_tpx_file_path)))
        alt_image_file_path = config_tpx_file_path

    return beamline, proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name, image_file_path, alt_image_file_path, detector_type


def do_pre_post_timepix3(arg_list):
    """
    Do pre-post-processing for Timepix3 Imaging.
    """
    return_code = 0
    try:
        print('Pre-Post-Processing for Timepix3.\n')

        beamline, proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name, image_file_path, alt_image_file_path, detector_type = process_args(arg_list)

        parms_present = all (p is not None for p in [proposal, run_number, tiff_file_path, tiff_file_name, image_file_path, detector_type])

        if parms_present is not None:
            if beamline in ['CG1D', 'VENUS']:
                include_tiff_files = False
            else:
                include_tiff_files = True
            # No Value Strings from ADARA/STC in Command Args Yet... ;-b
            #if detector_type in ['MCP TPX']:
            #    det_sub_dir = 'tpx'
            #elif detector_type in ['Timepix 3']:
            #    det_sub_dir = 'tpx3'
            #else:
            #    det_sub_dir = 'raw'
            # Just Use Numerical Values from Enum for Now... ;-/
            if int(detector_type) == 3:  # 'MCP TPX'
                det_sub_dir = 'tpx'
            elif int(detector_type) == 4:  # 'Timepix 3'
                det_sub_dir = 'tpx3'
            elif int(detector_type) == 7:  # 'QHY600 sCMOS'
                det_sub_dir = 'qhy600'
            else:
                det_sub_dir = 'raw'
            print('Detector Type {} -> Detector Sub-Directory = [{}].\n'.format(str(detector_type), str(det_sub_dir)))
            files_to_catalog = copy_images(beamline, proposal, run_number, source_dir, target_dir, tiff_file_path, tiff_file_name, image_file_path, alt_image_file_path, detector_type, det_sub_dir, include_tiff_files=include_tiff_files)
            catalog_images(files_to_catalog)
        else:
            print('ERROR: Not all parameters present.\n')
            return_code = -2
    except Exception as e:
        print('ERROR In do_pre_post_imaging(): {}\n'.format(str(e)))
        print(traceback.format_exc())
        return_code = -1
    finally:
        finish_up(return_code)


if __name__ == "__main__":
    # Test parameters: [['/TESTING/SNS/stcdata_devel/stc_pre_post_timepix3.py', 'FileName=C:/data/IPTS-26687/DAS_TEST_8_1_21/20210803_Run_51901_sample_file_0001_0605', 'SubDir=DAS_TEST_8_1_21', 'facility=SNS', 'beamline=HFIR', 'proposal=IPTS-26687', 'run_number=51901']]
    print("STC Pre-Post AutoReduction Timepix3 Command Script Entry.\n")
    print("   [%s]\n" % sys.argv[0])
    print("   [%s]" % sys.argv)

    do_pre_post_timepix3(sys.argv[1:])

# vim: expandtab

