############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os, shlex, subprocess, sys, time

# SchedMD
from utils.log import (
    log,
    log_new_line,
)

CMD_HOOK = "CMD: "


# Run functions with stats
def perform(action_desc, function, *args, verbose=True, new_line=False, decor=".."):
    log_func = log
    nl = ""

    if new_line:
        log_func = log_new_line
        nl = "\n"

    # action_desc: ie Cloning Repo, building slurm,
    if verbose:
        log_func(f"[{decor}{action_desc}{decor}]{nl}")

    start = time.perf_counter()
    result = function(*args)
    finish = time.perf_counter()
    time_stat = round(finish - start, 2)

    if verbose:
        log_func(f"[{decor}{action_desc}{decor}] finished in {time_stat} seconds")

    return result


def run_cmd(cmd, env=None, quiet=False, print_output=False, timeout=None, shell=False):
    if not quiet:
        log(CMD_HOOK + cmd)

    # If shell is specified, then let the shell split and parse the cmd string
    if not shell:
        cmd = shlex.split(cmd)

    if print_output:
        std_out = sys.stdout
        std_err = sys.stderr
    else:
        std_out = subprocess.PIPE
        std_err = subprocess.PIPE

    output = subprocess.run(
        cmd,
        env=env,
        stdout=std_out,
        stderr=std_err,
        timeout=timeout,
        shell=shell,
        text=True,
    )

    if (
        not quiet
        and not print_output
        and output.returncode != 0
        and output.stderr != ""
    ):
        log("Error: %s" % output.stderr)

    # Access rc from output elsewhere with output.returncode
    return output


def run_cmd_or_exit(
    cmd, msg, rc=0, quiet=False, print_output=False, timeout=None, shell=False
):
    output = run_cmd(
        cmd, quiet=quiet, print_output=print_output, timeout=timeout, shell=shell
    )

    if output.returncode != rc:
        log(f"{msg}")
        log(f"'{cmd}' failed with returncode {output.returncode}")
        log(f"stderr: {output.stderr}")
        log(f"stdout: {output.stdout}")

        sys.exit("Exiting")
    else:
        return output
