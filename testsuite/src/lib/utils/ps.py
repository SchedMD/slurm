############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
# SchedMD
from utils.log import (
    log,
)
from utils.cmds import (
    run_cmd,
)


def get_pids_from_exe(exe_path, verbose=True) -> list:
    if verbose:
        log(f"Retrieving pids from {exe_path}")

    # NOTE do we need a run_cmd_output?
    pid_list = run_cmd(f"pidof {exe_path}").strip().split()
    return pid_list


# TODO add a repeat_until to this with timeout
def kill_pids_from_exe(exe_path):
    for pid in get_pids_from_exe(exe_path):
        run_cmd(f"kill {pid}")


def is_tool(tool):
    from shutil import which

    return which(tool) is not None
