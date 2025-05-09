#!/usr/bin/python3
"""
This module provides a set of functions to configure and manage UPMEM DIMMs. It includes
capabilities to flash the DIMM's MCU, enable or disable DPUs, retrieve USB and DFU device
information, and manage device modes. The module is designed to be used with command-line arguments,
allowing for versatile scripting and automation in managing UPMEM DIMMs.

Functions:
- subprocess_verbose: Executes a subprocess command with optional verbose output.
- vpd_mode: Manages the VPD (Vital Product Data) of DIMMs, including enabling/disabling DPUs.
- flash_mcu_mode: Flashes the MCU of a specified DIMM.
- get_usb_infos: Retrieves information about UPMEM DIMMs connected via USB.
- usb_info_mode: Prints USB information for DIMMs.
- flash_dimm_dfu: Flashes a DIMM using DFU (Device Firmware Update) mode.
- set_dimm_dfu_mode: Sets a DIMM into DFU mode for firmware updates.
- get_dfu_devices: Lists UPMEM devices currently in DFU mode.
- exit_dfu_mode: Exits DFU mode for a specified device.
- find_device_by_serial_number: Finds a device by its serial number.
- find_device_by_rank: Finds a device by its rank.
- get_tty_devices: Retrieves tty devices based on serial number or rank.

Exceptions:
- RankError: Custom exception for rank-related errors in DIMM processing.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys
import time
from argparse import Namespace
from typing import (Callable, Dict, Iterable, List, NamedTuple,
                    Optional, TypeVar, Union, cast)

try:
    import pandas as pd
except ImportError as imp_err:
    raise ImportError("pandas is not installed. Please install it with you system package manager "
                      "(e.g. apt install python3-pandas).") from imp_err
try:
    import serial
except ImportError as imp_err:
    raise ImportError("pyserial is not installed. Please install it with you system package "
                      "manager (e.g. apt install python3-serial).") from imp_err
from dpu.vpd import vpd
from dpu.vpd.db import DPUVpdDatabase
from dpu.vpd.dimm import DPUVpd


class RankError(Exception):
    """Exception raised for errors in processing ranks."""


RankOperation = Callable[[Namespace, str], None]
GlobalOperation = Callable[[Namespace, List[str]], None]


def rank_operation(func: RankOperation) -> RankOperation:
    setattr(func, '_is_rank_operation', True)
    return func


def global_operation(func: GlobalOperation) -> GlobalOperation:
    setattr(func, '_is_global_operation', True)
    return func


def validate_args(args: Namespace) -> None:
    """Validate the command-line arguments.

    Args:
        args (Namespace): Arguments from command line or script.

    Raises:
        ValueError: If the arguments are invalid.
    """
    if args.rank and args.sernum:
        raise ValueError("You cannot specify both --rank and --sernum options.")

    if args.sernum and not args.flash_mcu_dfu:
        raise ValueError("--sernum option can only be used with the --flash-mcu-dfu mode.")

    if args.dry_run and not args.flash_mcu_dfu:
        raise ValueError("--dry-run option can only be used with the --flash-mcu-dfu mode.")

    if (args.disable_dpu or args.enable_dpu) and not args.rank:
        raise ValueError("You must specify at least a rank with --rank option.")

    if args.update_db and not all('=' in pair for pair in args.update_db):
        raise ValueError("--update-db option must be in the form key=value.")

    if args.update_db and not all(len(pair.split('=')) == 2 for pair in args.update_db):
        raise ValueError("--update-db option must be in the form key=value.")

    if args.set_vdd and (int(args.set_vdd) < 1000 or int(args.set_vdd) >= 1600):
        raise ValueError("VDD value is out of range, must be in [1000: 1600[")

    if args.set_osc and (int(args.set_osc) < 700 or int(args.set_osc) >= 1000):
        raise ValueError("OSC value is out of range, must be in [700: 1000[")


def subprocess_verbose(args: Namespace, command_list: List[str], timeout: float = 10):
    proc = subprocess.run(
        command_list,
        check=False,
        timeout=timeout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)

    if args.verbose:
        print(proc.stdout.decode())

    return proc


def apply_on_devices(operation: RankOperation, device_list: Iterable[str], args: Namespace) -> bool:
    def try_apply(operation: RankOperation, device: str, args: Namespace) -> bool:
        try:
            operation(args, device)
            return False  # No error
        except RankError as e:
            if args.stop_on_error:
                raise
            print(e)
            return True  # Error occurred

    error_statuses = [try_apply(operation, device, args) for device in device_list]
    return any(error_statuses)


T = TypeVar('T')


def map_on_devices(operation: Callable[[str], T],
                   device_list: Iterable[str],
                   args: Namespace) -> tuple[bool, List[T]]:
    def try_map(operation: Callable[[str], T], device: str) -> Optional[T]:
        try:
            return operation(device)  # No error
        except RankError as e:
            if args.stop_on_error:
                raise
            print(e)
            return None  # Error occurred

    return_values = [try_map(operation, device) for device in device_list]
    return None in return_values, list(filter(None, return_values))


@rank_operation
def vpd_mode(args: Namespace, rank_path: str):
    # Make sure the options are correct.
    # disable/enable_dpu on all ranks is at least suspect, so ask for a rank
    # to be given.
    if (args.disable_dpu or args.enable_dpu) and not args.rank:
        raise ValueError("Error, you must specify at least a rank with --rank option.")

    with DPUVpd(rank_path) as dimm_vpd:
        if args.info:
            print(f"* VPD for {rank_path}")
            print(dimm_vpd)
        elif args.disable_dpu:
            for dpu in args.disable_dpu:
                slice_id = int(dpu.split(".")[0])
                dpu_id = int(dpu.split(".")[1])
                dimm_vpd.disable_dpu(slice_id, dpu_id)
        elif args.enable_dpu:
            for dpu in args.enable_dpu:
                slice_id = int(dpu.split(".")[0])
                dpu_id = int(dpu.split(".")[1])
                dimm_vpd.enable_dpu(slice_id, dpu_id)


@rank_operation
def flash_mcu_mode(args: Namespace, rank_path: str):
    print(
        f"* Flashing MCU of rank {rank_path}...",
        end='',
        flush=True)

    command = f"ectool --interface=ci --name={rank_path} sysjump 1"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to jump to RO FW.")

    command = (f"ectool --interface=ci --name={rank_path} flasherase {vpd.FLASH_OFF_RW} "
               f"{vpd.FLASH_SIZE_RW}")
    proc = subprocess_verbose(args, command.split(" "), timeout=20)
    if proc.returncode != 0:
        raise RankError("Failed to erase RW partition.")

    command = (f"ectool --interface=ci --name={rank_path} flashwrite {vpd.FLASH_OFF_RW} "
               f"{args.flash_mcu}")
    proc = subprocess_verbose(args, command.split(" "), timeout=200)
    if proc.returncode != 0:
        raise RankError("Failed to write RW partition.")

    command = f"ectool --interface=ci --name={rank_path} reboot_ec cold"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to reboot MCU.")

    print("Success.")


def get_usb_infos(args: Namespace) -> tuple[bool, pd.DataFrame]:
    """Get serial number and rank information from upmem tty devices.

    Returns:
        bool: False if success, True otherwise.

        pandas.DataFrame: Informations of all upmem tty devices.

    Raises:
        RankError: If an error occurs while retrieving USB information.
    """
    def get_tty_device(tty_dev: str) -> Dict[str, Optional[str]]:
        tty_device: Dict[str, Optional[str]] = {}
        tty_device['name'] = os.path.basename(tty_dev)
        tty_device['rank0'] = None
        tty_device['rank1'] = None
        tty_device['serial_number'] = None
                
        device_name = os.path.basename(tty_dev)
        
        bytes_array = serial_send_cmd(args, device_name, b'dimm\n', timeout=0.5)

        if(bytes_array == None):
            return tty_device
            
        # 5. Parse output, typical string : DIMM '' S/N 12634275
        strings = [b.decode('utf-8') for b in bytes_array]
        for s in strings:
            match_serial = re.search(r'S/N (\w+)\r\n', s)
            match_rank = re.findall(r"'(dpu_rank\d+)'", s)
            if match_serial:
                tty_device['serial_number'] = match_serial.group(1)
            if match_rank:
                tty_device['rank0'] = match_rank[0]
                tty_device['rank1'] = match_rank[1]

        return tty_device

    # Loop over all tty devices
    error_mode, tty_devices = map_on_devices(
        get_tty_device, glob.iglob("/sys/bus/usb-serial/drivers/google/tty*"), args)

    # 6. Create dataframe from list of dict
    df = pd.DataFrame(tty_devices, index=None)
    return error_mode, df


@global_operation
def usb_info_mode(args: Namespace, rank_path_list: List[str]) -> None:
    del rank_path_list
    error_mode, df_info = get_usb_infos(args)
    print(df_info)
    if error_mode:
        raise RuntimeError("At least one error occurred while retrieving USB information.")


@rank_operation
def flash_dimm_dfu(args: Namespace, device_name: str) -> None:
    """ Flash upmem dimm via DFU.

    Args:
        args (Namespace): Arguments from command line or script.
        device_name (str): Device name read from dfu_util cmd.

    Raises:
        RankError: If an error occurs while flashing the device.
    """
    # 2. Go through all DFU devices and flash them using dfu-util.
    print(f"* Flashing upmem USB devices {device_name}...",
          end='', flush=True)

    command = (f"dfu-util -a 0 -s {vpd.FLASH_BASE_ADDRESS + vpd.FLASH_OFF_RO}:"
               f"{vpd.FLASH_SIZE_RO_RW} -D {args.flash_mcu_dfu} -p {device_name}")
    if not args.dry_run:
        proc = subprocess_verbose(args, command.split(" "), timeout=30)
        if proc.returncode != 0:
            raise RankError(f"Failed to flash device {device_name}")
    else:
        print(command)

    command = (f"dfu-util -a 0 -s {vpd.FLASH_BASE_ADDRESS + vpd.FLASH_OFF_RO}:"
               f"{vpd.FLASH_SIZE_RO_RW}:force:unprotect -D {args.flash_mcu_dfu} -p {device_name}")
    if not args.dry_run:
        proc = subprocess_verbose(args, command.split(" "), timeout=30)
        if proc.returncode != 0:
            raise RankError(f"Failed to reboot device {device_name}")
    else:
        print(command)

    print("Success")


def serial_send_cmd(args: Namespace, device_name: str, cmd: str, timeout=2, read_response=True):
     response = ""
     if not args.dry_run:
        # 1. Open connection with tty device
        try:
            tty_ser = serial.Serial(
                f"/dev/{device_name}", baudrate=115200, timeout=timeout)
        except serial.SerialTimeoutException as e:
            raise RankError(f"Device {device_name}: {e}") from e
        
        # 2. Flush console
        tty_ser.readlines()

        # 3.  Send cmd
        try:
            tty_ser.write(cmd)
        except serial.SerialException as e:
            raise RankError(f"Device {device_name}: {e}") from e
        
        #In some case we don't want to read from serial device at all (reboot device for example)
        if(read_response):
            # 4. Read output
            try:
                response = tty_ser.readlines()
            except serial.SerialException as e:
                raise RankError(f"Device {device_name}: {e}") from e

        # 5. Close connection
        tty_ser.close()
        
        return response

    
@rank_operation
def set_dimm_dfu_mode(args: Namespace, device_name: str) -> None:
    """set upmem tty device in DFU mode.

    Args:
        args (Namespace): Arguments from command line or script.
        device_name (str): 'ttyUSBX' for example.

    Raises:
        RankError: If an error occurs while setting the device in DFU mode.
    """
    print(f"* USB {device_name} set in DFU mode", end='\n', flush=True)
    dfu_cmd=(b'\n\n\ndfu\n')
    serial_send_cmd(args, device_name, dfu_cmd,read_response=False)
    # 4. Wait for devices to reboot in DFU mode
    if(not args.dry_run):
        time.sleep(5)


def get_dfu_devices() -> List[str]:
    """Get a list of upmem devices in DFU mode.

    Returns:
        list of str: List of upmem devices in DFU mode.
    """
    command = "dfu-util -l"
    proc = subprocess.run(command.split(" "),
                          check=False,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    pattern = re.compile(r'Found DFU: \[0483:df11\].*?path="(.*?)"', re.MULTILINE)
    match = pattern.findall(proc.stdout.decode())
    return list(set(match))


@rank_operation
def exit_dfu_mode(args: Namespace, device_name: str) -> None:
    """Exit DFU mode.

    Args:
        args (Namespace): Arguments from command line or script.
        device_name (str): Device name.

    Raises:
        RankError: If an error occurs while exiting DFU mode.
    """
    print(f"* Exiting DFU mode for upmem devices {device_name}...",
          end='', flush=True)
    command = (f"dfu-util -a 0 -s {vpd.FLASH_BASE_ADDRESS + vpd.FLASH_OFF_RO}:"
               f"{vpd.FLASH_SIZE_RO_RW}:force:unprotect -D {args.flash_mcu_dfu} -p {device_name}")
    if not args.dry_run:
        proc = subprocess_verbose(args, command.split(" "), timeout=30)
        if proc.returncode != 0:
            raise RankError(f"Failed to reboot device {device_name}, or device didn't exist")
    else:
        print(command)

    print("Success")

    time.sleep(5)


def find_devices_by_serial_number(df: pd.DataFrame, serial_numbers: str,
                                  args: Namespace) -> tuple[bool, List[str]]:
    def query_serial_number(sernum: str) -> str:
        row = df.query(f'serial_number == "{sernum}"')
        if row.empty:
            raise RankError(f"Serial number {sernum} does not exist")
        return row['name'].iloc[0]

    return map_on_devices(query_serial_number, serial_numbers.split(","), args)


def find_devices_by_rank(df: pd.DataFrame, rank_path_list: List[str],
                         args: Namespace) -> tuple[bool, List[str]]:
    def query_rank(rank: str) -> str:
        match = re.search(r"dpu_rank\d{1,2}$", rank)
        if not match:
            raise RankError(f"Error, {rank} path is invalid. It should be /dev/dpu_rankxx")
        rank_name = match.group(0)
        row = df.query(f'rank0 == "{rank_name}" or rank1 == "{rank_name}"')
        if row.empty:
            raise RankError(f"Rank {rank_name} does not exist")
        device_name = row['name'].iloc[0]
        print(f"Rank {rank_name} found on /dev/{device_name}")
        return device_name

    return map_on_devices(query_rank, rank_path_list, args)


def get_tty_devices(df: pd.DataFrame, args: Namespace,
                    rank_path_list: List[str]) -> tuple[bool, Iterable[str]]:
    if args.sernum != "":
        return find_devices_by_serial_number(df, args.sernum, args)
    if args.rank != "":
        return find_devices_by_rank(df, rank_path_list, args)
    return False, df['name'].astype(str).tolist()


@global_operation
def flash_mcu_dfu_mode(args: Namespace, rank_path_list: List[str]) -> None:
    error_mode = False

    ##########################################################################
    # 0. Exit all devices from DFU mode
    ##########################################################################
    list_dfu_devices = get_dfu_devices()
    error_mode |= apply_on_devices(exit_dfu_mode, list_dfu_devices, args)

    ##########################################################################
    # 1. Retrieve a list of all upmem tty devices
    ##########################################################################
    usb_error, df = get_usb_infos(args)
    error_mode |= usb_error
    print(df)

    if df.empty:
        raise RuntimeError("No upmem USB device was found, check USB connection!")

    ##########################################################################
    # 2. Filter user input and create a list of tty devices to be flashed
    ##########################################################################
    tty_error, list_tty_devices = get_tty_devices(df, args, rank_path_list)
    error_mode |= tty_error
    list_tty_devices = list(set(list_tty_devices))

    ##########################################################################
    # 3. Put devices in DFU mode
    ##########################################################################
    print(f"The following dimms will be flashed: {list_tty_devices}")
    error_mode |= apply_on_devices(set_dimm_dfu_mode, list_tty_devices, args)

    ##########################################################################
    # 4. Flash devices
    ##########################################################################

    list_dfu_devices = get_dfu_devices()
    if not list_dfu_devices and not args.dry_run:
        raise RuntimeError("No devices are in DFU mode")
    error_mode |= apply_on_devices(flash_dimm_dfu, list_dfu_devices, args)

    if error_mode:
        raise RuntimeError("At least one error occurred during the flashing process.")


@rank_operation
def flash_vpd_mode(args: Namespace, rank_path: str) -> None:
    print(
        f"* Flashing VPD of rank {rank_path}...",
        end='',
        flush=True)

    if vpd.dpu_vpd_commit_to_device_from_file(args.flash_vpd, rank_path):
        raise RankError("Failed to write VPD.")

    print("Success.")


@rank_operation
def flash_vpd_db_mode(args: Namespace, rank_path: str) -> None:
    print(
        f"* Flashing VPD database of rank {rank_path}...",
        end='',
        flush=True)

    if vpd.dpu_vpd_db_commit_to_device_from_file(
            args.flash_vpd_db, rank_path):
        raise RankError("Failed to write VPD database.")

    print("Success.")


@rank_operation
def vdd_mode(args: Namespace, rank_path: str) -> None:
    print(
        f"* Voltage and intensity of rank {rank_path}: ",
        end='',
        flush=True)

    command = f"ectool --interface=ci --name={rank_path} vdd"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to get VDD.")

    print(proc.stdout.decode().rstrip())


@rank_operation
def reboot_mode(args: Namespace, rank_path: str) -> None:
    print(f"* Rebooting rank {rank_path}...", end='', flush=True)

    command = f"ectool --interface=ci --name={rank_path} reboot_ec RW"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to reboot.")

    print("Success")


@rank_operation
def mcu_version_mode(args: Namespace, rank_path: str) -> None:
    print(f"* MCU firmware of rank {rank_path}: ", end='', flush=True)

    command = f"ectool --interface=ci --name={rank_path} version"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to print MCU firmware versions.")

    ro_version, rw_version, fw_copy = proc.stdout.decode().split('\n')[0:3]
    print("")
    print(f"\t{ro_version}")
    print(f"\t{rw_version}")
    print(f"\t{fw_copy}", end="\n\n")


@rank_operation
def osc_mode(args: Namespace, rank_path: str) -> None:
    print(f"* Frequency of rank {rank_path}: ", end='', flush=True)

    command = f"ectool --interface=ci --name={rank_path} osc"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to print FCK frequency.")

    fck, div = proc.stdout.decode().split('\n')[0:2]
    print("")
    print(f"\t{fck}")
    print(f"\t{div}", end="\n\n")


@rank_operation
def database_mode(args: Namespace, rank_path: str) -> None:
    print(f"* VPD database for {rank_path}: ", end='', flush=True)

    db_path = "db.bin"

    # Read flash segment info file
    command = (f"ectool --interface=ci --name={rank_path} flashread {vpd.FLASH_OFF_VPD_DB} "
               f"{vpd.FLASH_SIZE_VPD_DB} {db_path}")
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to read VPD database flash segment.")

    # Init database based on flash content
    db = DPUVpdDatabase(db_path)

    print("")
    db.dump()
    print("")
    os.remove(db_path)


@rank_operation
def update_db_mode(args: Namespace, rank_path: str) -> None:
    print(
        f"* Updating VPD database for {rank_path}: ",
        end='',
        flush=True)

    # Read flash segment into file
    db_path = "db.bin"
    command = (f"ectool --interface=ci --name={rank_path} flashread {vpd.FLASH_OFF_VPD_DB} "
               f"{vpd.FLASH_SIZE_VPD_DB} {db_path}")
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to read VPD database flash segment.")

    # Init database based on flash content
    db = DPUVpdDatabase(db_path)

    # Add or update key/value pairs
    # For key/value pairs used by the MCU FW, use explicit value length
    # (otherwise the MCU might read past the value)
    d = dict(pair.split('=') for pair in args.update_db)
    for key, val in d.items():
        if key in ('div_min', 'div_max'):
            db.add_byte(key, int(val))
        elif key in ('fck', 'fck_min', 'fck_max', 'div_min', 'div_max', 'vdddpu', 'chip_version'):
            db.add_short(key, int(val))
        elif key in ('chip_version'):
            db.add_string(key, val)
        elif val.isnumeric():
            db.add_numeric(key, int(val))
        else:
            db.add_string(key, val)

    # Flash new database
    db.write_to_device(rank_path)
    os.remove(db_path)

    print("Success.")


@rank_operation
def set_vdd_mode(args: Namespace, rank_path: str) -> None:
    vdd_value = int(args.set_vdd)

    print(f"* Setting voltage to {vdd_value} mV for rank {rank_path}: ", end='', flush=True)

    command = f"ectool --interface=ci --name={rank_path} vdd {vdd_value}"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to set VDD.")

    print("Success")


@rank_operation
def set_osc_mode(args: Namespace, rank_path: str) -> None:
    osc_value = int(args.set_osc)

    print(f"* Setting osc to {osc_value} MHz for rank {rank_path}: ", end='', flush=True)

    command = f"ectool --interface=ci --name={rank_path} osc {osc_value}"
    proc = subprocess_verbose(args, command.split(" "))
    if proc.returncode != 0:
        raise RankError("Failed to set OSC.")

    print("Success")


def parse_args():
    parser = argparse.ArgumentParser(description='Upmem DIMM configuration tool')
    parser.add_argument(
        '--rank',
        metavar='<rank_path>',
        default="",
        help="Comma-separated rank paths (e.g.: /dev/dpu_rankX), default to all ranks")
    parser.add_argument(
        '--sernum',
        metavar='<S/N>',
        default="",
        help=("Comma-separated serial numbers as they appear in ectool dimm command. "
              "Only used in --flash-mcu-dfu mode."))
    parser.add_argument(
        '--verbose',
        action='store_true',
        help="Verbose mode: print debugging messages")
    parser.add_argument(
        '--info',
        action='store_true',
        help="Display info about the DIMM (i.e.: VPD)")
    parser.add_argument(
        '--disable-dpu',
        action='append',
        metavar='<x.y>',
        help=("Disable dpu y in chip x on rank specified by --rank option, can be set multiple "
              "times (e.g.: --disable-dpu 0.1 --disable-dpu 3.4)"))
    parser.add_argument(
        '--enable-dpu',
        action='append',
        metavar='<x.y>',
        help=("Enable dpu y in chip x on rank specified by --rank option, can be set multiple "
              "times (e.g.: --enable-dpu 0.1 --enable-dpu 3.4)"))
    parser.add_argument(
        '--flash-mcu',
        metavar='<fw_path>',
        help="Flash MCU with the firmware given in argument")
    parser.add_argument(
        '--flash-mcu-dfu',
        metavar='<fw_path>',
        help=("Flash *ALL* MCUs connected with USB cable with the firmware given in argument, "
              "--rank option is not used and it requires dfu-util."))
    parser.add_argument(
        '--flash-vpd',
        metavar='<vpd_path>',
        help="Flash VPD with the VPD given in argument")
    parser.add_argument(
        '--flash-vpd-db',
        metavar='<db_path>',
        help="Flash VPD database with the database given in argument")
    parser.add_argument(
        '--set-vdd',
        metavar='<vdd_mV>',
        type=int,
        help="Set voltage of the chips on the DIMM (cannot be used when the rank is used)")
    parser.add_argument(
        '--set-osc',
        metavar='<osc_freq>',
        type=int,
        help="Set frequency of the chips on the DIMM (cannot be used when the rank is used)")
    parser.add_argument(
        '--vdd',
        action='store_true',
        help=("Display voltage and intensity of the chips on the DIMM "
              "(cannot be used when the rank is used)"))
    parser.add_argument(
        '--reboot',
        action='store_true',
        help="Reboot the MCU (cannot be used when the rank is used)")
    parser.add_argument(
        '--mcu-version',
        action='store_true',
        help="Print MCU firwmare versions (cannot be used when the rank is used)")
    parser.add_argument(
        '--osc',
        action='store_true',
        help="Print FCK frequency (cannot be used when the rank is used)")
    parser.add_argument(
        '--database',
        action='store_true',
        help="Print DIMM database (cannot be used when the rank is used)")
    parser.add_argument(
        '--update-db',
        action='append',
        metavar='<x=y>',
        help=("Add or update the key/value pair in the database. Only decimal values and strings "
              "are supported (e.g.: --update-db ltc7106_vdddpu=1200)"))
    parser.add_argument(
        '--usb-info',
        action='store_true',
        help="Dispay dimm serial number and its associated ranks")
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help="Do not flash mcu when using --flash-mcu-dfu command")
    parser.add_argument(
        '--stop-on-error',
        action='store_true',
        help="Stop on first error")

    args = parser.parse_args()
    validate_args(args)

    return args, parser


class Mode(NamedTuple):
    """NamedTuple to store mode information."""
    name: str
    function: Union[RankOperation, GlobalOperation]


mode_functions: List[Mode] = [
    Mode('info', vpd_mode),
    Mode('disable_dpu', vpd_mode),
    Mode('enable_dpu', vpd_mode),
    Mode('flash_mcu', flash_mcu_mode),
    Mode('flash_mcu_dfu', flash_mcu_dfu_mode),
    Mode('flash_vpd', flash_vpd_mode),
    Mode('flash_vpd_db', flash_vpd_db_mode),
    Mode('vdd', vdd_mode),
    Mode('reboot', reboot_mode),
    Mode('mcu_version', mcu_version_mode),
    Mode('osc', osc_mode),
    Mode('database', database_mode),
    Mode('update_db', update_db_mode),
    Mode('set_vdd', set_vdd_mode),
    Mode('set_osc', set_osc_mode),
    Mode('usb_info', usb_info_mode),
]


def main():
    args, parser = parse_args()

    # Get the ranks to work on.
    rank_path_list: List[str] = args.rank.split(",") if args.rank else glob.glob("/dev/dpu_rank*")

    action = next((func for arg, func in mode_functions if getattr(args, arg, None)), None)

    if not action:
        parser.print_help(sys.stderr)
        raise ValueError("You must specify at least one mode.")

    if hasattr(action, '_is_global_operation'):
        action = cast(GlobalOperation, action)
        action(args, rank_path_list)
        return

    if not hasattr(action, '_is_rank_operation'):
        raise ValueError(f"Invalid mode action for {action}. You must specify a decorator")
    action = cast(RankOperation, action)

    if apply_on_devices(action, rank_path_list, args):
        raise RuntimeError("At least one error occurred during the process.")


if __name__ == "__main__":
    main()
