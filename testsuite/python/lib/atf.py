##############################################################################
# ATF (Automated Testing Framework) python testsuite module
# Copyright (C) SchedMD LLC.
##############################################################################
import collections
import errno
import glob
import logging
import math
import os
import pwd
import pathlib
import pytest
import re
import shutil
import stat
import socket
import subprocess
import sys
import time
import traceback


##############################################################################
# ATF module functions
##############################################################################

default_command_timeout = 60
default_polling_timeout = 15
default_sql_cmd_timeout = 120

PERIODIC_TIMEOUT = 30


def get_open_port():
    """Finds an open port.

    Warning: Race conditions abound so be ready to retry calling function;

    Example:
        >>> while not some_test(port):
        >>>     port = get_open_port()

    Shamelessly based on:
    https://stackoverflow.com/questions/2838244/get-open-tcp-port-in-python
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("", 0))
    s.listen(1)
    port = s.getsockname()[1]
    s.close()
    return port


def node_range_to_list(node_expression):
    """Converts a node range expression into a list of node names.

    Example:
        >>> node_range_to_list('node[1,3-5]')
        ['node1', 'node3', 'node4', 'node5']
    """

    node_list = []
    output = run_command_output(
        f"scontrol show hostnames {node_expression}", fatal=True, quiet=True
    )
    for line in output.rstrip().splitlines():
        node_list.append(line)
    return node_list


def node_list_to_range(node_list):
    """Converts a list of node names to a node range expression.

    Example:
        >>> node_list_to_range(['node1', 'node3', 'node4', 'node5'])
        'node[1,3-5]'
    """

    return run_command_output(
        f"scontrol show hostlistsorted {','.join(node_list)}", fatal=True, quiet=True
    ).rstrip()


def range_to_list(range_expression):
    """Converts an integer range expression into a list of integers.

    Example:
        >>> range_to_list('1,3-5')
        [1, 3, 4, 5]
    """

    return list(map(int, node_range_to_list(f"[{range_expression}]")))


def list_to_range(numeric_list):
    """Converts a list of integers to an integer range expression.

    Example:
        >>> list_to_range([1, 3, 4, 5])
        '1,3-5'
    """

    node_range_expression = node_list_to_range(map(str, numeric_list))
    return re.sub(r"^\[(.*)\]$", r"\1", node_range_expression)


def run_command(
    command,
    fatal=False,
    timeout=default_command_timeout,
    quiet=False,
    chdir=None,
    user=None,
    input=None,
    xfail=False,
    env_vars=None,
):
    """Executes a command and returns a dictionary result.

    Args:
       command (string): The command to execute. The command is run within a
           bash subshell, so pipes, redirection, etc. are performed.
       fatal (boolean): If True, a non-zero exit code (or zero if combined
           with xfail) will result in the test failing.
       timeout (integer): If the command does not exit before timeout number
           of seconds, this function will return with an exit code of 110.
       quiet (boolean): If True, logging is performed at the TRACE log level.
       chdir (directory): Change to the specified directory before executing
           the command.
       user (user name): Run the command as the specified user. This requires
           the invoking user to have unprompted sudo rights.
       input (string): The specified input is supplied to the command as stdin.
       xfail (boolean): If True, the command is expected to fail.
       env_vars (string): A string to set environmental variables that is
            prepended to the command when run.

    Returns:
        A dictionary containing the following keys:
            start_time: epoch start time
            duration: number of seconds the command ran for
            exit_code: exit code for the command
            stdout: command stdout as a string
            stderr: command stderr as a string

    Example:
        >>> run_command('ls -l', fatal=True)
        {'command': 'ls -l', 'start_time': 1712268971.532, 'duration': 0.007, 'exit_code': 0, 'stdout': 'total 124\n-rw-rw-r-- 1 slurm slurm 118340 Apr  4 22:15 atf.py\n-rw-rw-r-- 1 slurm slurm    498 Apr  4 22:09 test.py\n-rw-rw-r-- 1 slurm slurm   1013 Apr  4 22:09 python_script.py\n', 'stderr': ''}

        >>> run_command('ls /non/existent/path', xfail=True)
        {'command': 'ls /non/existent/path', 'start_time': 1712269123.2, 'duration': 0.005, 'exit_code': 2, 'stdout': '', 'stderr': "ls: cannot access '/non/existent/path': No such file or directory\n"}

        >>> run_command('sleep 5', timeout=2)
        {'command': 'sleep 5', 'start_time': 1712269157.113, 'duration': 2.0, 'exit_code': 110, 'stdout': '', 'stderr': ''}
    """

    additional_run_kwargs = {}
    if chdir is not None:
        additional_run_kwargs["cwd"] = chdir
    if input is not None:
        additional_run_kwargs["input"] = input
    if timeout is not None:
        additional_run_kwargs["timeout"] = timeout

    if quiet:
        log_command_level = logging.TRACE
        log_details_level = logging.TRACE
    else:
        log_command_level = logging.NOTE
        log_details_level = logging.DEBUG

    if env_vars is not None:
        command = env_vars.strip() + " " + command

    start_time = time.time()
    invocation_message = "Running command"
    if user is not None:
        invocation_message += f" as user {user}"
    invocation_message += f": {command}"
    logging.log(log_command_level, invocation_message)
    try:
        if user is not None and user != properties["test-user"]:
            if not properties["sudo-rights"]:
                pytest.skip(
                    "This test requires the test user to have unprompted sudo rights",
                    allow_module_level=True,
                )
            cp = subprocess.run(
                ["sudo", "-nu", user, "/bin/bash", "-lc", command],
                capture_output=True,
                text=True,
                **additional_run_kwargs,
            )
        else:
            cp = subprocess.run(
                command,
                shell=True,
                executable="/bin/bash",
                capture_output=True,
                text=True,
                **additional_run_kwargs,
            )
        end_time = time.time()
        duration = end_time - start_time
        exit_code = cp.returncode
        stdout = cp.stdout
        stderr = cp.stderr
    except subprocess.TimeoutExpired as e:
        duration = e.timeout
        exit_code = errno.ETIMEDOUT
        # These are byte objects, not strings
        stdout = e.stdout.decode("utf-8") if e.stdout else ""
        stderr = e.stderr.decode("utf-8") if e.stderr else ""

    if input is not None:
        logging.log(log_details_level, f"Command input: {input}")
    logging.log(log_details_level, f"Command exit code: {exit_code}")
    logging.log(log_details_level, f"Command stdout: {stdout}")
    logging.log(log_details_level, f"Command stderr: {stderr}")
    logging.log(log_details_level, "Command duration: %.03f seconds", duration)

    message = ""
    if exit_code == errno.ETIMEDOUT:
        message = f'Command "{command}" timed out after {duration} seconds'
    elif exit_code != 0 and not xfail:
        message = f'Command "{command}" failed with rc={exit_code}'
    elif exit_code == 0 and xfail:
        message = f'Command "{command}" was expected to fail but succeeded'
    if (exit_code != 0 and not xfail) or (exit_code == 0 and xfail):
        if stderr != "" or stdout != "":
            message += ":"
        if stderr != "":
            message += f" {stderr}"
        if stdout != "":
            message += f" {stdout}"

    if message != "":
        message = message.rstrip()
        if fatal:
            pytest.fail(message)
        elif not quiet:
            logging.warning(message)

    results = {}
    results["command"] = command
    results["start_time"] = float(int(start_time * 1000)) / 1000
    results["duration"] = float(int(duration * 1000)) / 1000
    results["exit_code"] = exit_code
    results["stdout"] = stdout
    results["stderr"] = stderr

    return results


def run_command_error(command, **run_command_kwargs):
    """Executes a command and returns the standard error.

    This function accepts the same arguments as run_command.

    Args:
        command (string): The command to execute. The command is run within a
            bash subshell, so pipes, redirection, etc. are performed.

    Returns:
        The standard error (stderr) output of the command as a string.

    Example:
        >>> run_command_error('ls /non/existent/path')
        "ls: cannot access '/non/existent/path': No such file or directory\n"

        >>> run_command_error('grep foo /etc/passwd', quiet=True)
        ''

        >>> run_command_error('echo error message >&2', xfail=True)
        'error message\n'
    """

    results = run_command(command, **run_command_kwargs)

    return results["stderr"]


def run_command_output(command, **run_command_kwargs):
    """Executes a command and returns the standard output.

    This function accepts the same arguments as run_command.

    Args:
        command (string): The command to execute. The command is run within a
            Bash subshell, so pipes, redirection, etc. are performed.

    Returns:
        The standard output (stdout) of the command as a string.

    Example:
        >>> run_command_output('ls')
        'file1.txt\nfile2.txt\nscript.py\n'

        >>> run_command_output('echo Hello, World!')
        'Hello, World!\n'

        >>> run_command_output('grep foo /etc/passwd', xfail=True)
        ''
    """

    results = run_command(command, **run_command_kwargs)

    return results["stdout"]


def run_command_exit(command, **run_command_kwargs):
    """Executes a command and returns the exit code.

    This function accepts the same arguments as run_command.

    Args:
        command (string): The command to execute. The command is run within a
            Bash subshell, so pipes, redirection, etc. are performed.

    Returns:
        The exit code of the command as an integer.

    Example:
        >>> run_command_exit('ls')
        0

        >>> run_command_exit('grep foo /etc/passwd', xfail=True)
        1

        >>> run_command_exit('sleep 5', timeout=2)
        110
    """

    results = run_command(command, **run_command_kwargs)

    return results["exit_code"]


def repeat_until(
    callable,
    condition,
    timeout=default_polling_timeout,
    poll_interval=None,
    xfail=False,
    fatal=False,
):
    """Repeats a callable until a condition is met or it times out.

    The callable returns an object that the condition operates on.

    Args:
        callable (callable): Repeatedly called until the condition is met or
            the timeout is reached.
        condition (callable): A callable object that returns a boolean. This
            function will return True when the condition call returns True.
        timeout (integer): If timeout number of seconds expires before the
            condition is met, return False.
        poll_interval (float): Number of seconds to wait between condition
            polls. This may be a decimal fraction. The default poll interval
            depends on the timeout used, but varies between .1 and 1 seconds.
        xfail (boolean): If True, a timeout is expected.
        fatal (boolean): If True, the test will fail if condition is not met
            (or if condition is met with xfail).

    Returns:
        True if the condition is met by the timeout, False otherwise.

    Example:
        >>> repeat_until(lambda : random.randint(1,10), lambda n: n == 5, timeout=30, poll_interval=1)
        True
    """

    begin_time = time.time()

    if poll_interval is None:
        if timeout <= 5:
            poll_interval = 0.1
        elif timeout <= 10:
            poll_interval = 0.2
        else:
            poll_interval = 1

    condition_met = False
    while time.time() < begin_time + timeout:
        if condition(callable()):
            condition_met = True
            break
        time.sleep(poll_interval)

    if not xfail and not condition_met:
        if fatal:
            pytest.fail(f"Condition was not met within the {timeout} second timeout")
        else:
            logging.warning(
                f"Condition was not met within the {timeout} second timeout"
            )
    elif xfail and condition_met:
        if fatal:
            pytest.fail(
                f"Condition was met within the {timeout} second timeout and wasn't expected"
            )
        else:
            logging.warning(
                f"Condition was met within the {timeout} second timeout and wasn't expected"
            )

    return condition_met


def repeat_command_until(command, condition, quiet=True, **repeat_until_kwargs):
    """Repeats a command until a condition is met or it times out.

    This function accepts the same arguments as repeat_until.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if the condition is met by the timeout, False otherwise.

    Example:
        >>> repeat_command_until("scontrol ping", lambda results: re.search(r'is UP', results['stdout']))
        True
    """

    return repeat_until(
        lambda: run_command(command, quiet=quiet), condition, **repeat_until_kwargs
    )


def pids_from_exe(executable):
    """Finds process IDs (PIDs) of running processes with given executable name.

    Args:
        executable (string): The name of the executable for which to find running processes.

    Returns:
        A list of integer process IDs (PIDs) for running processes that match the given
        executable name.

    Example:
        >>> pids_from_exe('/usr/bin/python3')
        [12345, 67890]

        >>> pids_from_exe('/usr/sbin/sshd')
        [54321]

        >>> pids_from_exe('/bin/non-existent')
        []
    """

    # We have to elevate privileges here, but forking off thousands of sudo
    # commands is expensive, so we will sudo a dynamic bash script for speed
    script = f"""cd /proc
for pid in `ls -d1 [0-9]*`;
do if [[ "$(readlink $pid/exe)" = "{executable}" ]];
   then echo $pid;
   fi
done"""
    pids = []
    output = run_command_output(script, user="root", quiet=True)
    for line in output.rstrip().splitlines():
        pids.append(int(line))
    return pids


def is_slurmctld_running(quiet=False):
    """Checks whether slurmctld is running.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if the slurmctld is running, False otherwise.

    Example:
        >>> is_slurmctld_running()
        True

        >>> is_slurmctld_running(quiet=True)
        True
    """

    # Check whether slurmctld is running
    if re.search(r"is UP", run_command_output("scontrol ping", quiet=quiet)):
        return True

    return False


def start_slurmctld(clean=False, quiet=False):
    """Starts the Slurm controller daemon (slurmctld).

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        None

    Example:
        >>> start_slurmctld()  # Start slurmctld with default settings
        >>> start_slurmctld(clean=True, quiet=True)  # Start slurmctld with clean state and quiet logging
    """

    if not properties["auto-config"]:
        require_auto_config("wants to start slurmctld")

    if not is_slurmctld_running(quiet=quiet):
        # Start slurmctld
        command = f"{properties['slurm-sbin-dir']}/slurmctld"
        if clean:
            command += " -c -i"
        results = run_command(command, user=properties["slurm-user"], quiet=quiet)
        if results["exit_code"] != 0:
            pytest.fail(
                f"Unable to start slurmctld (rc={results['exit_code']}): {results['stderr']}"
            )

        # Verify that slurmctld is running
        if not repeat_command_until(
            "scontrol ping", lambda results: re.search(r"is UP", results["stdout"])
        ):
            pytest.fail(f"Slurmctld is not running")


def start_slurm(clean=False, quiet=False):
    """Starts all applicable Slurm daemons.

    This function examines the Slurm configuration files to determine which daemons
    need to be started.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        None

    Example:
        >>> start_slurm()  # Start all Slurm daemons with default settings
        >>> start_slurm(clean=True, quiet=True)  # Start all Slurm daemons with clean state and quiet logging
    """

    if not properties["auto-config"]:
        require_auto_config("wants to start slurm")

    # Determine whether slurmdbd should be included
    if (
        get_config_parameter("AccountingStorageType", live=False, quiet=quiet)
        == "accounting_storage/slurmdbd"
    ):
        if (
            run_command_exit(
                "sacctmgr show cluster", user=properties["slurm-user"], quiet=quiet
            )
            != 0
        ):
            # Start slurmdbd
            results = run_command(
                f"{properties['slurm-sbin-dir']}/slurmdbd",
                user=properties["slurm-user"],
                quiet=quiet,
            )
            if results["exit_code"] != 0:
                pytest.fail(
                    f"Unable to start slurmdbd (rc={results['exit_code']}): {results['stderr']}"
                )

            # Verify that slurmdbd is running
            if not repeat_command_until(
                "sacctmgr show cluster", lambda results: results["exit_code"] == 0
            ):
                pytest.fail(f"Slurmdbd is not running")

    # Remove unnecessary default node0 from config to avoid being used or reserved
    output = run_command_output(
        f"cat {properties['slurm-config-dir']}/slurm.conf",
        user=properties["slurm-user"],
        quiet=quiet,
    )
    if len(re.findall(r"NodeName=", output)) > 1:
        run_command(
            f"sed -i '/NodeName=node0 /d' {properties['slurm-config-dir']}/slurm.conf",
            user=properties["slurm-user"],
            quiet=quiet,
        )

    # Start slurmctld
    start_slurmctld(clean, quiet)

    # Build list of slurmds
    slurmd_list = []
    output = run_command_output(
        f"perl -nle 'print $1 if /^NodeName=(\\S+)/' {properties['slurm-config-dir']}/slurm.conf",
        user=properties["slurm-user"],
        quiet=quiet,
    )
    if not output:
        pytest.fail("Unable to determine the slurmd node names")
    for node_name_expression in output.rstrip().split("\n"):
        if node_name_expression != "DEFAULT":
            slurmd_list.extend(node_range_to_list(node_name_expression))

    # (Multi)Slurmds
    for slurmd_name in slurmd_list:
        # Check whether slurmd is running
        if run_command_exit(f"pgrep -f 'slurmd -N {slurmd_name}'", quiet=quiet) != 0:
            # Start slurmd
            results = run_command(
                f"{properties['slurm-sbin-dir']}/slurmd -N {slurmd_name}",
                user="root",
                quiet=quiet,
            )
            if results["exit_code"] != 0:
                pytest.fail(
                    f"Unable to start slurmd -N {slurmd_name} (rc={results['exit_code']}): {results['stderr']}"
                )

            # Verify that the slurmd is running
            if (
                run_command_exit(f"pgrep -f 'slurmd -N {slurmd_name}'", quiet=quiet)
                != 0
            ):
                pytest.fail(f"Slurmd -N {slurmd_name} is not running")


def stop_slurmctld(quiet=False):
    """Stops the Slurm controller daemon (slurmctld).

    This function may only be used in auto-config mode.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        None

    Example:
        >>> stop_slurmctld()  # Stop slurmctld with default logging
        >>> stop_slurmctld(quiet=True)  # Stop slurmctld with quiet logging
    """

    if not properties["auto-config"]:
        require_auto_config("wants to stop slurmctld")

    # Stop slurmctld
    run_command(
        "scontrol shutdown slurmctld", user=properties["slurm-user"], quiet=quiet
    )

    # Verify that slurmctld is not running
    if not repeat_until(
        lambda: pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmctld"),
        lambda pids: len(pids) == 0,
    ):
        pytest.fail("Slurmctld is still running")


def stop_slurm(fatal=True, quiet=False):
    """Stops all applicable Slurm daemons.

    This function examines the Slurm configuration files to determine which daemons
    need to be stopped.

    This function may only be used in auto-config mode.

    Args:
        fatal (boolean): If True, a failure to stop all daemons will result in the
            test failing.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if all Slurm daemons were stopped, False otherwise.

    Example:
        >>> stop_slurm()  # Stop all Slurm daemons with default settings
        True

        >>> stop_slurm(fatal=False, quiet=True)  # Stop all Slurm daemons with non-fatal failures and quiet logging
        False
    """

    failures = []

    if not properties["auto-config"]:
        require_auto_config("wants to stop slurm")

    # Determine whether slurmdbd should be included
    if (
        get_config_parameter("AccountingStorageType", live=False, quiet=quiet)
        == "accounting_storage/slurmdbd"
    ):
        # Stop slurmdbd
        results = run_command(
            "sacctmgr shutdown", user=properties["slurm-user"], quiet=quiet
        )
        if results["exit_code"] != 0:
            failures.append(
                f"Command \"sacctmgr shutdown\" failed with rc={results['exit_code']}"
            )

        # Verify that slurmdbd is not running (we might have to wait for rollups to complete)
        if not repeat_until(
            lambda: pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmdbd"),
            lambda pids: len(pids) == 0,
            timeout=60,
        ):
            failures.append("Slurmdbd is still running")

    # Stop slurmctld and slurmds
    results = run_command(
        "scontrol shutdown", user=properties["slurm-user"], quiet=quiet
    )
    if results["exit_code"] != 0:
        failures.append(
            f"Command \"scontrol shutdown\" failed with rc={results['exit_code']}"
        )

    # Verify that slurmctld is not running
    if not repeat_until(
        lambda: pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmctld"),
        lambda pids: len(pids) == 0,
    ):
        failures.append("Slurmctld is still running")

    # Build list of slurmds
    slurmd_list = []
    output = run_command_output(
        f"perl -nle 'print $1 if /^NodeName=(\\S+)/' {properties['slurm-config-dir']}/slurm.conf",
        quiet=quiet,
    )
    if not output:
        failures.append("Unable to determine the slurmd node names")
    else:
        for node_name_expression in output.rstrip().split("\n"):
            if node_name_expression != "DEFAULT":
                slurmd_list.extend(node_range_to_list(node_name_expression))

    # Verify that slurmds are not running
    if not repeat_until(
        lambda: pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmd"),
        lambda pids: len(pids) == 0,
    ):
        pids = pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmd")
        run_command(f"pgrep -f {properties['slurm-sbin-dir']}/slurmd -a", quiet=quiet)
        failures.append(f"Some slurmds are still running ({pids})")

    if failures:
        if fatal:
            pytest.fail(failures[0])
        else:
            logging.warning(failures[0])
            return False
    else:
        return True


def restart_slurmctld(clean=False, quiet=False):
    """Restarts the Slurm controller daemon (slurmctld).

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        None

    Example:
        >>> restart_slurmctld()  # Restart slurmctld with default settings
        >>> restart_slurmctld(clean=True, quiet=True)  # Restart slurmctld with clean state and quiet logging
    """

    stop_slurmctld(quiet=quiet)
    start_slurmctld(clean=clean, quiet=quiet)


def restart_slurm(clean=False, quiet=False):
    """Restarts all applicable Slurm daemons.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        None

    Example:
        >>> restart_slurm()  # Restart all Slurm daemons with default settings
        >>> restart_slurm(clean=True, quiet=True)  # Restart all Slurm daemons with clean state and quiet logging
    """

    stop_slurm(quiet=quiet)
    start_slurm(clean=clean, quiet=quiet)


def require_slurm_running():
    """Ensures that the Slurm daemons are running.

    In local-config mode, the test is skipped if Slurm is not running.
    In auto-config mode, Slurm is started if necessary.

    In order to avoid multiple restarts of Slurm (in auto-config), this function
    should be called at the end of the setup preconditions.

    Args:
        None

    Returns:
        None

    Example:
        >>> require_slurm_running()  # Ensure Slurm is running or start it in auto-config mode
    """

    global nodes

    if properties["auto-config"]:
        if not is_slurmctld_running(quiet=True):
            start_slurm(clean=True, quiet=True)
            properties["slurm-started"] = True
    else:
        if not is_slurmctld_running(quiet=True):
            pytest.skip(
                "This test requires slurm to be running", allow_module_level=True
            )

    # As a side effect, build up initial nodes dictionary
    nodes = get_nodes(quiet=True)


def backup_config_file(config="slurm"):
    """Backs up a configuration file.

    This function may only be used in auto-config mode.

    Args:
        config (string): Name of the config file to back up (without the .conf suffix).

    Returns:
        None

    Example:
        >>> backup_config_file('slurm')
        >>> backup_config_file('gres')
        >>> backup_config_file('cgroup')
    """

    if not properties["auto-config"]:
        require_auto_config(f"wants to modify the {config} configuration file")

    properties["configurations-modified"].add(config)

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"
    backup_config_file = f"{config_file}.orig-atf"

    # If a backup already exists, issue a warning and return (honor existing backup)
    if os.path.isfile(backup_config_file):
        logging.trace(f"Backup file already exists ({backup_config_file})")
        return

    # If the file to backup does not exist, touch an empty backup file with
    # the sticky bit set. restore_config_file will remove the file.
    if not os.path.isfile(config_file):
        run_command(
            f"touch {backup_config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        run_command(
            f"chmod 1000 {backup_config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )

    # Otherwise, copy the config file to the backup
    else:
        run_command(
            f"cp {config_file} {backup_config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )


def restore_config_file(config="slurm"):
    """Restores a configuration file.

    This function may only be used in auto-config mode.

    Args:
        config (string): Name of config file to restore (without the .conf suffix).

    Returns:
        None

    Example:
        >>> restore_config_file('slurm')
        >>> restore_config_file('gres')
        >>> restore_config_file('cgroup')
    """

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"
    backup_config_file = f"{config_file}.orig-atf"

    properties["configurations-modified"].remove(config)

    # If backup file doesn't exist, it has probably already been
    # restored by a previous call to restore_config_file
    if not os.path.isfile(backup_config_file):
        logging.trace(
            f"Backup file does not exist for {config_file}. It has probably already been restored."
        )
        return

    # If the sticky bit is set and the file is empty, remove both the file and the backup
    backup_stat = os.stat(backup_config_file)
    if backup_stat.st_size == 0 and backup_stat.st_mode & stat.S_ISVTX:
        run_command(
            f"rm -f {backup_config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        if os.path.isfile(config_file):
            run_command(
                f"rm -f {config_file}",
                user=properties["slurm-user"],
                fatal=True,
                quiet=True,
            )

    # Otherwise, copy backup config file to primary config file
    # and remove the backup (.orig-atf)
    else:
        run_command(
            f"cp {backup_config_file} {config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        run_command(
            f"rm -f {backup_config_file}",
            user=properties["slurm-user"],
            fatal=True,
            quiet=True,
        )


def get_config(live=True, source="slurm", quiet=False):
    """Returns the Slurm configuration as a dictionary.

    Args:
        live (boolean):
            If True, the configuration information is obtained via
            a query to the relevant Slurm daemon (e.g., scontrol show config).
            If False, the configuration information is obtained by directly
            parsing the relevant Slurm configuration file (e.g. slurm.conf).
        source (string):
            If live is True, source should be either scontrol or sacctmgr.
            If live is False, source should be the name of the config file
            without the .conf prefix (e.g. slurmdbd).
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        A dictionary comprised of the parameter names and their values.
        For parameters that can have multiple lines and subparameters,
        the dictionary value will be a dictionary of dictionaries.

    Example:
        >>> get_config()
        {'AccountingStorageBackupHost': '(null)', 'AccountingStorageEnforce': 'none', 'AccountingStorageHost': 'localhost', 'AccountingStorageExternalHost': '(null)', 'AccountingStorageParameters': '(null)', 'AccountingStoragePort': '0', 'AccountingStorageTRES': 'cpu,mem,energy,node,billing,fs/disk,vmem,pages', ...}
        >>> get_config(live=False, source='slurm')
        {'SlurmctldHost': 'nathan-atf-docstrings', 'SlurmUser': 'slurm', 'SlurmctldLogFile': '/var/slurm/log/slurmctld.log', 'SlurmctldPidFile': '/var/slurm/run/slurmctld.pid', 'SlurmctldDebug': 'debug3', 'SlurmdLogFile': '/var/slurm/log/slurmd.%n.log', 'SlurmdPidFile': '/var/slurm/run/slurmd.%n.pid', ...}
        >>> get_config(live=True, source='scontrol', quiet=True)
        {'AccountingStorageBackupHost': '(null)', 'AccountingStorageEnforce': 'none', 'AccountingStorageHost': 'localhost', 'AccountingStorageExternalHost': '(null)', 'AccountingStorageParameters': '(null)', 'AccountingStoragePort': '0', 'AccountingStorageTRES': 'cpu,mem,energy,node,billing,fs/disk,vmem,pages', ...}
    """

    slurm_dict = {}

    if live:
        if source == "slurm" or source == "controller" or source == "scontrol":
            command = "scontrol"
        elif source == "slurmdbd" or source == "dbd" or source == "sacctmgr":
            command = "sacctmgr"
        else:
            pytest.fail(f"Invalid live source value ({source})")

        output = run_command_output(f"{command} show config", fatal=True, quiet=quiet)

        for line in output.splitlines():
            if match := re.search(rf"^\s*(\S+)\s*=\s*(.*)$", line):
                slurm_dict[match.group(1)] = match.group(2).rstrip()
    else:
        config = source
        config_file = f"{properties['slurm-config-dir']}/{config}.conf"

        # We might be looking for parameters in a config file that has not
        # been created yet. If so, we just want this to return an empty dict
        output = run_command_output(
            f"cat {config_file}", user=properties["slurm-user"], quiet=quiet
        )
        for line in output.splitlines():
            if match := re.search(rf"^\s*(\S+)\s*=\s*(.*)$", line):
                parameter_name, parameter_value = (
                    match.group(1),
                    match.group(2).rstrip(),
                )
                if parameter_name.lower() in [
                    "downnodes",
                    "frontendname",
                    "name",
                    "nodename",
                    "nodeset",
                    "partitionname",
                    "switchname",
                ]:
                    instance_name, subparameters = parameter_value.split(" ", 1)
                    subparameters_dict = {}
                    for subparameter_name, subparameter_value in re.findall(
                        r" *([^= ]+) *= *([^ ]+)", subparameters
                    ):
                        # Reformat the value if necessary
                        if is_integer(subparameter_value):
                            subparameter_value = int(subparameter_value)
                        elif is_float(subparameter_value):
                            subparameter_value = float(subparameter_value)
                        elif subparameter_value == "(null)":
                            subparameter_value = None
                        subparameters_dict[subparameter_name] = subparameter_value
                    if parameter_name not in slurm_dict:
                        slurm_dict[parameter_name] = {}
                    slurm_dict[parameter_name][instance_name] = subparameters_dict
                else:
                    # Reformat the value if necessary
                    if is_integer(parameter_value):
                        parameter_value = int(parameter_value)
                    elif is_float(parameter_value):
                        parameter_value = float(parameter_value)
                    elif parameter_value == "(null)":
                        parameter_value = None
                    slurm_dict[parameter_name] = parameter_value

    return slurm_dict


def get_config_parameter(name, default=None, **get_config_kwargs):
    """Obtains the value for a Slurm configuration parameter.

    This function accepts the same arguments as get_config.

    Args:
        name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.

    Returns:
        The value of the specified parameter, or the default if not found.

    Example:
        >>> get_config_parameter('JobAcctGatherFrequency')
        '30'
        >>> get_config_parameter('MaxJobCount', default='10000')
        '10000'
        >>> get_config_parameter('partitionname', default='debug', live=True, source='scontrol')
        'debug'
    """

    config_dict = get_config(**get_config_kwargs)

    # Convert keys to lower case so we can do a case-insensitive search
    lower_dict = dict(
        (key.casefold(), str(value).casefold()) for key, value in config_dict.items()
    )

    if name.casefold() in lower_dict:
        return lower_dict[name.casefold()]
    else:
        return default


def config_parameter_includes(name, value, **get_config_kwargs):
    """Checks whether a configuration parameter includes a specific value.

    When a parameter may contain a comma-separated list of values, this
    function can be used to determine whether a specific value is within
    the list.

    This function accepts the same arguments as get_config.

    Args:
        name (string): The parameter name.
        value (string): The value you are looking for.

    Returns:
        True if the specified string value is found within the parameter
        value list, False otherwise.

    Example:
        >>> config_parameter_includes('SlurmdParameters', 'config_overrides')
        False
    """

    config_dict = get_config(**get_config_kwargs)

    # Convert keys to lower case so we can do a case-insensitive search
    lower_dict = dict((key.casefold(), value) for key, value in config_dict.items())

    if name.casefold() in lower_dict and value.lower() in map(
        str.lower, lower_dict[name.casefold()].split(",")
    ):
        return True
    else:
        return False


def set_config_parameter(
    parameter_name, parameter_value, source="slurm", restart=False
):
    """Sets the value of the specified configuration parameter.

    This function modifies the specified slurm configuration file and
    reconfigures slurm (or restarts slurm if restart=True). A backup
    is automatically created and the original configuration is restored
    after the test completes.

    This function may only be used in auto-config mode.

    Args:
        parameter_name (string): The parameter name.
        parameter_value (string): The parameter value.
            Use a value of None to unset a parameter.
        source (string): Name of the config file without the .conf prefix.
        restart (boolean): If True and slurm is running, slurm will be
            restarted rather than reconfigured.

    Note:
        When setting a complex parameter (one which may be repeated and has
        its own subparameters, such as with nodes, partitions and gres),
        the parameter_value should be a dictionary of dictionaries.

    Returns:
        None

    Example:
        >>> set_config_parameter('ClusterName', 'cluster1')
    """

    if not properties["auto-config"]:
        require_auto_config("wants to modify parameters")

    if source == "dbd":
        config = "slurmdbd"
    else:
        config = source

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"

    # This has the side-effect of adding config to configurations-modified
    backup_config_file(config)

    # Remove all matching parameters and append the new parameter
    lines = []
    output = run_command_output(
        f"cat {config_file}", user=properties["slurm-user"], quiet=True
    )
    for line in output.splitlines():
        if not re.search(rf"(?i)^\s*{parameter_name}\s*=", line):
            lines.append(f"{line}\n")
    if isinstance(parameter_value, dict):
        for instance_name in parameter_value:
            line = f"{parameter_name}={instance_name}"
            for subparameter_name, subparameter_value in parameter_value[
                instance_name
            ].items():
                line += f" {subparameter_name}={subparameter_value}"
            lines.append(f"{line}\n")
    elif parameter_value != None:
        lines.append(f"{parameter_name}={parameter_value}\n")
    input = "".join(lines)
    run_command(
        f"cat > {config_file}",
        input=input,
        user=properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    slurmctld_running = is_slurmctld_running(quiet=True)

    # Remove clustername state file if we aim to change the cluster name
    if parameter_name.lower() == "clustername":
        state_save_location = get_config_parameter(
            "StateSaveLocation", live=slurmctld_running, quiet=True
        )
        run_command(
            f"rm -f {state_save_location}/clustername",
            user=properties["slurm-user"],
            quiet=True,
        )

    # Reconfigure (or restart) slurm controller if it is already running
    if slurmctld_running:
        if source != "slurm" or parameter_name.lower() in [
            "accountingstoragetype",
            "rebootprogram",
        ]:
            restart_slurm(quiet=True)
        elif restart or parameter_name.lower() in [
            "authtype",
            "controlmach",
            "plugindir",
            "statesavelocation",
            "slurmctldhost",
            "slurmctldport",
            "slurmdport",
        ]:
            restart_slurmctld(quiet=True)
        else:
            run_command(
                "scontrol reconfigure", user=properties["slurm-user"], quiet=True
            )


def add_config_parameter_value(name, value, source="slurm"):
    """Appends a value to configuration parameter list.

    When a parameter may contain a comma-separated list of values, this
    function can be used to add a value to the list.

    This function may only be used in auto-config mode.

    Args:
        name (string): The parameter name.
        value (string): The value to add.
        source (string): Name of the config file without the .conf prefix.

    Returns:
        None

    Example:
        >>> add_config_parameter_value('SlurmdParameters', 'config_overrides')
    """

    if config_parameter_includes(name, value, live=False, quiet=True, source=source):
        return

    original_value_string = get_config_parameter(
        name, live=False, quiet=True, source=source
    )
    if original_value_string is None:
        set_config_parameter(name, value, source=source)
    else:
        value_list = original_value_string.split(",")
        value_list.append(value)
        set_config_parameter(name, ",".join(value_list), source=source)


def remove_config_parameter_value(name, value, source="slurm"):
    """Removes a value from a configuration parameter list.

    When a parameter may contain a comma-separated list of values, this
    function can be used to remove a value from the list.

    This function may only be used in auto-config mode.

    Args:
        name (string): The parameter name.
        value (string): The value to remove.
        source (string): Name of the config file without the .conf prefix.

    Returns:
        None

    Example:
        >>> remove_config_parameter_value('SlurmdParameters', 'config_overrides')
    """
    if not config_parameter_includes(
        name, value, live=False, quiet=True, source=source
    ):
        return

    value_list = get_config_parameter(
        name, live=False, quiet=True, source=source
    ).split(",")
    value_list.remove(value.casefold())
    if value_list:
        set_config_parameter(name, ",".join(value_list), source=source)
    else:
        set_config_parameter(name, None, source=source)


def is_tool(tool):
    """Returns True if the tool is found in PATH.

    Args:
        tool (string): The name of the tool to check for in the PATH environment variable.

    Returns:
        True if the tool is found in PATH, False otherwise.

    Example:
        >>> is_tool('ls')
        True
        >>> is_tool('uninstalled-tool-name')
        False
    """

    from shutil import which

    return which(tool) is not None


def require_tool(tool):
    """Skips if the supplied tool is not found.

    Args:
        tool (string): The name of the tool to check for in the PATH environment variable.

    Returns:
        None

    Example:
        >>> require_tool('ls')
        >>> require_tool('uninstalled-tool-name')
    """

    if not is_tool(tool):
        msg = f"This test requires '{tool}' and it was not found"
        pytest.skip(msg, allow_module_level=True)


def require_whereami(is_cray=False):
    """Compiles the whereami.c program to be used by tests.

    This function installs the whereami program.  To get the
    correct output, TaskPlugin is required in the slurm.conf
    file before slurm starts up.
    ex: TaskPlugin=task/cray_aries,task/cgroup,task/affinity

    The file will be installed in the testsuite/python/lib/scripts
    directory where the whereami.c file is located

    Args:
        None

    Returns:
        None

    Examples:
        >>> atf.require_whereami()
        >>> print('\nwhereami is located at', atf.properties['whereami'])
        >>> output = atf.run_command(f"srun {atf.properties['whereami']}",
        >>>     user=atf.properties['slurm-user'])
    """
    require_config_parameter("TaskPlugin", "task/cgroup,task/affinity")

    # Set requirement for cray systems
    if is_cray:
        require_config_parameter(
            "TaskPlugin", "task/cray_aries,task/cgroup,task/affinity"
        )

    # If the file already exists and we don't need to recompile
    dest_file = f"{properties['testsuite_scripts_dir']}/whereami"
    if os.path.isfile(dest_file):
        properties["whereami"] = dest_file
        return

    source_file = f"{properties['testsuite_scripts_dir']}/whereami.c"
    if not os.path.isfile(source_file):
        pytest.skip("Could not find whereami.c!", allow_module_level=True)

    run_command(
        f"gcc {source_file} -o {dest_file}", fatal=True, user=properties["slurm-user"]
    )
    properties["whereami"] = dest_file


def require_config_parameter(
    parameter_name, parameter_value, condition=None, source="slurm", skip_message=None
):
    """Ensures that a configuration parameter has the required value.

    In local-config mode, the test is skipped if the required configuration is not set.
    In auto-config mode, sets the required configuration value if necessary.

    Args:
        parameter_name (string): The parameter name.
        parameter_value (string): The target parameter value.
        condition (callable): If there is a range of acceptable values, a
            condition can be specified to test whether the current parameter
            value is sufficient. If not, the target parameter_value will be
            used (or the test will be skipped in the case of local-config mode).
        source (string): Name of the config file without the .conf prefix.
        skip_message (string): Message to be displayed if in local-config mode
            and parameter not present.

    Note:
        When requiring a complex parameter (one which may be repeated and has
        its own subparameters, such as with nodes, partitions and gres),
        the parameter_value should be a dictionary of dictionaries. See the
        fourth example for multi-line parameters.

    Returns:
        None

    Examples:
        >>> require_config_parameter('SelectType', 'select/cons_tres')
        >>> require_config_parameter('SlurmdTimeout', 5, lambda v: v <= 5)
        >>> require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}, 'mps': {'Count': 100}}, source='gres')
        >>> require_config_parameter("PartitionName", {"primary": {"Nodes": "ALL"}, "dynamic1": {"Nodes": "ns1"}, "dynamic2": {"Nodes": "ns2"}, "dynamic3": {"Nodes": "ns1,ns2"}})
    """

    if isinstance(parameter_value, dict):
        tmp1_dict = dict()
        for k1, v1 in parameter_value.items():
            tmp2_dict = dict()
            for k2, v2 in v1.items():
                if isinstance(v2, str):
                    tmp2_dict[k2.casefold()] = v2.casefold()
                else:
                    tmp2_dict[k2.casefold()] = v2
            tmp1_dict[k1.casefold()] = tmp2_dict

        parameter_value = tmp1_dict
    elif isinstance(parameter_value, str):
        parameter_value = parameter_value.casefold()

    observed_value = get_config_parameter(
        parameter_name, live=False, source=source, quiet=True
    )

    condition_satisfied = False
    if condition is None:
        condition = lambda observed, desired: observed == desired
        if observed_value == parameter_value:
            condition_satisfied = True
    else:
        if condition(observed_value):
            condition_satisfied = True

    if not condition_satisfied:
        if properties["auto-config"]:
            set_config_parameter(parameter_name, parameter_value, source=source)
        else:
            if skip_message is None:
                skip_message = f"This test requires the {parameter_name} parameter to be {parameter_value} (but it is {observed_value})"
            pytest.skip(skip_message, allow_module_level=True)


def require_config_parameter_includes(name, value, source="slurm"):
    """Ensures that a configuration parameter list contains the required value.

    In local-config mode, the test is skipped if the configuration parameter
    list does not include the required value.
    In auto-config mode, adds the required value to the configuration parameter
    list if necessary.

    Args:
        name (string): The parameter name.
        value (string): The value we want to be in the list.
        source (string): Name of the config file without the .conf prefix.

    Returns:
        None

    Example:
        >>> require_config_parameter_includes('SlurmdParameters', 'config_overrides')
    """

    if properties["auto-config"]:
        add_config_parameter_value(name, value, source=source)
    else:
        if not config_parameter_includes(
            name, value, source=source, live=False, quiet=True
        ):
            pytest.skip(
                f"This test requires the {name} parameter to include {value}",
                allow_module_level=True,
            )


def require_config_parameter_excludes(name, value, source="slurm"):
    """Ensures that a configuration parameter list does not contain a value.

    In local-config mode, the test is skipped if the configuration parameter
    includes the specified value. In auto-config mode, removes the specified
    value from the configuration parameter list if necessary.

    Args:
        name (string): The parameter name.
        value (string): The value we do not want to be in the list.
        source (string): Name of the config file without the .conf prefix.

    Returns:
        None

    Example:
        >>> require_config_parameter_excludes('SlurmdParameters', 'config_overrides')
    """
    if properties["auto-config"]:
        remove_config_parameter_value(name, value, source=source)
    else:
        if config_parameter_includes(
            name, value, source=source, live=False, quiet=True
        ):
            pytest.skip(
                f"This test requires the {name} parameter to exclude {value}",
                allow_module_level=True,
            )


def require_tty(number):
    """Creates a TTY device file if it does not exist.

    Args:
        number (integer): The number of the TTY device.

    Returns:
        None

    Example:
        >>> require_tty(1)
        >>> require_tty(2)
    """

    tty_file = f"/dev/tty{number}"
    if not os.path.exists(tty_file):
        run_command(
            f"mknod -m 666 {tty_file} c 4 {number}", user="root", fatal=True, quiet=True
        )


## Use this to create an entry in gres.conf and create an associated tty
# def require_gres_device(name):
#
#    gres_value = get_config_parameter('Name', live=False, source='gres', quiet=True)
#    if gres_value is None or name not in gres_value:
#        if not properties['auto-config']:
#            pytest.skip(f"This test requires a '{name}' gres device to be defined in gres.conf", allow_module_level=True)
#        else:
#            require_tty(0)
#            require_config_parameter('Name', {name: {'File': '/dev/tty0'}}, source='gres')


def require_auto_config(reason=""):
    """Ensures that auto-config mode is being used.

    This function skips the test if auto-config mode is not enabled.

    Args:
        reason (string): Augments the skip reason with a context-specific
            explanation for why the auto-config mode is needed by the test.

    Returns:
        None

    Example:
        >>> require_auto_config("wants to set the Epilog")
    """

    if not properties["auto-config"]:
        message = "This test requires auto-config to be enabled"
        if reason != "":
            message += f" ({reason})"
        pytest.skip(message, allow_module_level=True)


def require_accounting(modify=False):
    """Ensures that Slurm accounting is configured.

    In local-config mode, the test is skipped if Slurm accounting is not
    configured. In auto-config mode, configures Slurm accounting if necessary.

    Args:
        modify (boolean): If True, this indicates to the ATF that the test
            will modify the accounting database (e.g. adding accounts, etc).
            A database backup is automatically created and the original dump
            is restored after the test completes.

    Returns:
        None

    Example:
        >>> require_accounting()
        >>> require_accounting(modify=True)
    """

    if properties["auto-config"]:
        if (
            get_config_parameter("AccountingStorageType", live=False, quiet=True)
            != "accounting_storage/slurmdbd"
        ):
            set_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
        if modify:
            backup_accounting_database()
    else:
        if modify:
            require_auto_config("wants to modify the accounting database")
        elif (
            get_config_parameter("AccountingStorageType", live=False, quiet=True)
            != "accounting_storage/slurmdbd"
        ):
            pytest.skip(
                "This test requires accounting to be configured",
                allow_module_level=True,
            )


def get_user_name():
    """Returns the username of the current user.

    Args:
        None

    Returns:
        The username of the current user.

    Example:
        >>> get_user_name()
        'john_doe'
    """
    return pwd.getpwuid(os.getuid()).pw_name


def cancel_jobs(
    job_list,
    timeout=default_polling_timeout,
    poll_interval=0.1,
    fatal=False,
    quiet=False,
):
    """Cancels a list of jobs and waits for them to complete.

    Args:
        job_list (list): A list of job ids to cancel. All 0s will be ignored.
        timeout (integer): Number of seconds to wait for jobs to be done before
            timing out.
        poll_interval (float): Number of seconds to wait between job state
            polls.
        fatal (boolean): If True, a timeout will result in the test failing.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if all jobs were successfully cancelled and completed within
        the timeout period, False otherwise.

    Example:
        >>> cancel_jobs([1234, 5678], timeout=60, fatal=True)
        True
        >>> cancel_jobs([9876, 5432], timeout=30, fatal=False)
        False
    """

    # Filter list to ignore job_ids being 0
    job_list = [i for i in job_list if i != 0]
    job_list_string = " ".join(str(i) for i in job_list)

    if job_list_string == "":
        return True

    run_command(f"scancel {job_list_string}", fatal=fatal, quiet=quiet)

    for job_id in job_list:
        status = wait_for_job_state(
            job_id,
            "DONE",
            timeout=timeout,
            poll_interval=poll_interval,
            fatal=fatal,
            quiet=quiet,
        )
        if not status:
            if fatal:
                pytest.fail(
                    f"Job ({job_id}) was not cancelled within the {timeout} second timeout"
                )
            return status

    return True


def cancel_all_jobs(
    timeout=default_polling_timeout, poll_interval=0.1, fatal=False, quiet=False
):
    """Cancels all jobs by the test user and waits for them to be cancelled.

    Args:
        fatal (boolean): If True, a timeout will result in the test failing.
        timeout (integer): If timeout number of seconds expires before the
            jobs are verified to be cancelled, fail.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if all jobs were successfully cancelled and completed within
        the timeout period, False otherwise.

    Example:
        >>> cancel_all_jobs(timeout=60, fatal=True)
        True
        >>> cancel_all_jobs(timeout=30, fatal=False)
        False
    """

    user_name = get_user_name()

    run_command(f"scancel -u {user_name}", fatal=fatal, quiet=quiet)

    return repeat_command_until(
        f"squeue -u {user_name} --noheader",
        lambda results: results["stdout"] == "",
        timeout=timeout,
        poll_interval=poll_interval,
        fatal=fatal,
        quiet=quiet,
    )


def is_integer(value):
    try:
        int(value)
    except ValueError:
        return False
    else:
        return True


def is_float(value):
    try:
        float(value)
    except ValueError:
        return False
    else:
        return True


def get_nodes(live=True, quiet=False, **run_command_kwargs):
    """Returns the node configuration as a dictionary of dictionaries.

    If the live argument is not True, the dictionary will contain the literal
    configuration values and the DEFAULT node will be separately indexed.

    Args:
        live (boolean):
            If True, the node configuration information is obtained via a query
            to the slurmctld daemon (e.g. scontrol show config).
            If False, the node configuration information is obtained by
            directly parsing the Slurm configuration file (slurm.conf).
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        A dictionary of dictionaries where the first level keys are the node
        names and with their values being a dictionary of configuration
        parameters for the respective node.

    Example:
        >>> get_nodes()
        {'node1': {'NodeName': 'node1', 'Arch': 'x86_64', 'CoresPerSocket': 1, 'CPUAlloc': 0, 'CPUEfctv': 1, 'CPUTot': 1, 'CPULoad': 91.4, 'AvailableFeatures': None, 'ActiveFeatures': None, 'Gres': None, 'NodeAddr': 'slurm-host', 'NodeHostName': 'slurm-host', 'Port': 6821, 'Version': '24.05.0-0rc1', ...}}
        >>> get_nodes(live=False, quiet=True)
        {'node1': {'NodeHostname': 'slurm-host', 'Port': 6821, 'NodeName': 'node1'}}
    """

    nodes_dict = {}

    if live:
        output = run_command_output(
            "scontrol show nodes -oF", fatal=True, quiet=quiet, **run_command_kwargs
        )

        node_dict = {}
        for line in output.splitlines():
            if line == "":
                continue

            while match := re.search(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", line):
                parameter_name, parameter_value = match.group(1), match.group(2)

                # Remove the consumed parameter from the line
                line = re.sub(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", "", line)

                # Reformat the value if necessary
                if is_integer(parameter_value):
                    parameter_value = int(parameter_value)
                elif is_float(parameter_value):
                    parameter_value = float(parameter_value)
                elif parameter_value == "(null)":
                    parameter_value = None

                # Add it to the temporary node dictionary
                node_dict[parameter_name] = parameter_value

            # Add the node dictionary to the nodes dictionary
            nodes_dict[node_dict["NodeName"]] = node_dict

            # Clear the node dictionary for use by the next node
            node_dict = {}

    else:
        # Get the config dictionary
        config_dict = get_config(live=False, quiet=quiet)

        # Convert keys to lower case so we can do a case-insensitive search
        lower_config_dict = dict(
            (key.lower(), value) for key, value in config_dict.items()
        )

        # DEFAULT will be included separately
        if "nodename" in lower_config_dict:
            for node_expression, node_expression_dict in lower_config_dict[
                "nodename"
            ].items():
                port_expression = (
                    node_expression_dict["Port"]
                    if "Port" in node_expression_dict
                    else ""
                )

                # Break up the node expression and port expression into lists
                node_list = node_range_to_list(node_expression)
                port_list = range_to_list(port_expression)

                # Iterate over the nodes in the expression
                for node_index in range(len(node_list)):
                    node_name = node_list[node_index]
                    # Add the parameters to the temporary node dictionary
                    node_dict = dict(node_expression_dict)
                    node_dict["NodeName"] = node_name
                    if node_index < len(port_list):
                        node_dict["Port"] = int(port_list[node_index])
                    # Add the node dictionary to the nodes dictionary
                    nodes_dict[node_name] = node_dict

    return nodes_dict


def get_node_parameter(node_name, parameter_name, default=None, live=True):
    """Obtains the value for a node configuration parameter.

    Args:
        node_name (string): The node name.
        parameter_name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.
        live (boolean):
            If True, the node configuration information is obtained via a query
            to the slurmctld daemon (e.g. scontrol show config).
            If False, the node configuration information is obtained by
            directly parsing the Slurm configuration file (slurm.conf).

    Returns:
        The value of the specified node parameter, or the default if not found.

    Example:
        >>> get_node_parameter('node1', 'State')
        'IDLE'
        >>> get_node_parameter('node2', 'RealMemory', default=4)
        1
        >>> get_node_parameter('node3', 'Partitions', default='primary', live=False)
        'primary'
    """

    nodes_dict = get_nodes(live=live)

    if node_name in nodes_dict:
        node_dict = nodes_dict[node_name]
    else:
        pytest.fail(f"Node ({node_name}) was not found in the node configuration")

    if parameter_name in node_dict:
        return node_dict[parameter_name]
    else:
        return default


def set_node_parameter(node_name, new_parameter_name, new_parameter_value):
    """Sets the value of the specified node configuration parameter.

    This function sets a node property for the specified node and restarts
    the relevant slurm daemons. A backup is automatically created and the
    original configuration is restored after the test completes.

    This function may only be used in auto-config mode.

    Args:
        node_name (string): The node name.
        new_parameter_name (string): The parameter name.
        new_parameter_value (string): The parameter value.
            Use a value of None to unset a node parameter.

    Returns:
        None

    Example:
        >>> set_node_parameter('node1', 'Features', 'f1')
    """

    if not properties["auto-config"]:
        require_auto_config("wants to modify node parameters")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(
        f"cat {config_file}", user=properties["slurm-user"], quiet=True
    )
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the node among the various NodeName definitions
    found_node_name = False
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        words = re.split(r" +", line.strip())
        if len(words) < 1:
            continue
        parameter_name, parameter_value = words[0].split("=", 1)
        if parameter_name.lower() != "nodename":
            continue

        # We found a NodeName line. Read in the node parameters
        node_expression = parameter_value
        port_expression = ""
        original_node_parameters = collections.OrderedDict()
        for word in words[1:]:
            parameter_name, parameter_value = word.split("=", 1)
            if parameter_name.lower() == "port":
                port_expression = parameter_value
            else:
                original_node_parameters[parameter_name] = parameter_value

        node_list = node_range_to_list(node_expression)
        port_list = range_to_list(port_expression)

        # Determine whether our node is included in this node expression
        if node_name in node_list:
            # Yes. We found the node
            found_node_name = True
            node_index = node_list.index(node_name)

            # Delete the original node definition
            new_config_lines.pop(line_index)

            # Add the modified definition for the specified node
            modified_node_parameters = original_node_parameters.copy()
            if new_parameter_value is None:
                if new_parameter_name in modified_node_parameters:
                    del modified_node_parameters[new_parameter_name]
            else:
                modified_node_parameters[new_parameter_name] = new_parameter_value
            modified_node_line = f"NodeName={node_name}"
            if node_index < len(port_list):
                modified_node_line += f" Port={port_list[node_index]}"
            for parameter_name, parameter_value in modified_node_parameters.items():
                modified_node_line += f" {parameter_name}={parameter_value}"
            new_config_lines.insert(line_index, modified_node_line)

            # If the node was part of an aggregate node definition
            node_list.remove(node_name)
            if len(node_list):
                # Write the remainder of the aggregate using the original attributes
                remainder_node_expression = node_list_to_range(node_list)
                remainder_node_line = f"NodeName={remainder_node_expression}"
                if node_index < len(port_list):
                    port_list.pop(node_index)
                if port_list:
                    remainder_port_expression = list_to_range(port_list)
                    remainder_node_line += f" Port={remainder_port_expression}"
                for parameter_name, parameter_value in original_node_parameters.items():
                    remainder_node_line += f" {parameter_name}={parameter_value}"
                new_config_lines.insert(line_index, remainder_node_line)

            break

    if not found_node_name:
        pytest.fail(
            f"Invalid node specified in set_node_parameter(). Node ({node_name}) does not exist"
        )

    # Write the config file back out with the modifications
    backup_config_file("slurm")
    new_config_string = "\n".join(new_config_lines)
    run_command(
        f"echo '{new_config_string}' > {config_file}",
        user=properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    # Restart slurm controller if it is already running
    if is_slurmctld_running(quiet=True):
        restart_slurm(quiet=True)


def get_reservations(quiet=False, **run_command_kwargs):
    """Returns the reservations as a dictionary of dictionaries.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        A dictionary of dictionaries where the first level keys are the
        reservation names and with their values being a dictionary of
        configuration parameters for the respective reservation.

    Example:
        >>> get_reservations()
        {'resv1': {'ReservationName': 'resv1', 'StartTime': '2024-04-06T00:00:17', 'EndTime': '2024-04-06T00:15:17', 'Duration': '00:15:00', 'Nodes': 'node1', 'NodeCnt': 1, 'CoreCnt': 1, 'Features': None, 'PartitionName': 'primary', 'Flags': 'DAILY,REPLACE_DOWN,PURGE_COMP=00:01:00', 'TRES': 'cpu=1', 'Users': 'atf', 'Groups': None, 'Accounts': None, 'Licenses': None, 'State': 'INACTIVE', 'BurstBuffer': None, 'MaxStartDelay': None}}
    """

    resvs_dict = {}
    resv_dict = {}

    output = run_command_output(
        "scontrol show reservations -o", fatal=True, quiet=quiet, **run_command_kwargs
    )
    for line in output.splitlines():
        if line == "":
            continue

        while match := re.search(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", line):
            parameter_name, parameter_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", "", line)

            # Reformat the value if necessary
            if is_integer(parameter_value):
                parameter_value = int(parameter_value)
            elif is_float(parameter_value):
                parameter_value = float(parameter_value)
            elif parameter_value == "(null)":
                parameter_value = None

            # Add it to the temporary resv dictionary
            resv_dict[parameter_name] = parameter_value

        # Add the resv dictionary to the resvs dictionary
        resvs_dict[resv_dict["ReservationName"]] = resv_dict

        # Clear the resv dictionary for use by the next resv
        resv_dict = {}

    return resvs_dict


def get_reservation_parameter(resv_name, parameter_name, default=None):
    """Obtains the value for a reservation configuration parameter.

    Args:
        resv_name (string): The reservation name.
        parameter_name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.

    Returns:
        The value of the specified reservation parameter, or the default if not found.

    Example:
        >>> get_reservation_parameter('resv1', 'PartitionName')
        'primary'
        >>> get_reservation_parameter('resv2', 'EndTime', default='2024-04-06T00:15:17')
        '2024-04-06T00:15:17'
    """

    resvs_dict = get_reservations()

    if resv_name in resvs_dict:
        resv_dict = resvs_dict[resv_name]
    else:
        pytest.fail(f"reservation ({resv_name}) was not found")

    if parameter_name in resv_dict:
        return resv_dict[parameter_name]
    else:
        return default


def is_super_user():
    """Checks if the current user is a super user.

    Args:
        None

    Returns:
        True if the current user is a super user, False otherwise.

    Example:
        >>> is_super_user()
        False
    """

    uid = os.getuid()

    if uid == 0:
        return True

    user = pwd.getpwuid(uid)[0]
    if get_config_parameter("SlurmUser") == user:
        return True

    return False


def require_sudo_rights():
    """Skips the test if the test user does not have unprompted sudo privileges.

    Args:
        None

    Returns:
        None

    Example:
        >>> require_sudo_rights()
    """
    if not properties["sudo-rights"]:
        pytest.skip(
            "This test requires the test user to have unprompted sudo privileges",
            allow_module_level=True,
        )


def submit_job_sbatch(sbatch_args='--wrap "sleep 60"', **run_command_kwargs):
    """Submits a job using sbatch and returns the job id.

    The submitted job will automatically be cancelled when the test ends.

    Args:
        sbatch_args (string): The arguments to sbatch.

    Returns:
        The job id.

    Example:
        >>> submit_job_sbatch('--wrap "echo Hello"')
        1234
        >>> submit_job_sbatch('-J myjob --wrap "echo World"', fatal=True)
        5678
    """

    output = run_command_output(f"sbatch {sbatch_args}", **run_command_kwargs)

    if match := re.search(r"Submitted \S+ job (\d+)", output):
        job_id = int(match.group(1))
        properties["submitted-jobs"].append(job_id)
        return job_id
    else:
        return 0


# Returns results
def run_job(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the run_command results dictionary.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The srun run_command results dictionary.

    Example:
        >>> run_job('-n 4 --output=output.txt ./my_executable', timeout=60)
        {'command': 'srun -n 4 --output=output.txt ./my_executable', 'start_time': 1712276708.827, 'duration': 60.0, 'exit_code': 110, 'stdout': '', 'stderr': 'srun: Requested partition configuration not available now\nsrun: job 15 queued and waiting for resources\n'}
        >>> run_job('-n 1 --error=error.txt ./my_executable', fatal=True)
        {'command': 'srun -n 1 --error=error.txt my_executable', 'start_time': 1712277798.016, 'duration': 0.06, 'exit_code': 0, 'stdout': 'foo bar\n', 'stderr': ''}
    """

    results = run_command(f"srun {srun_args}", **run_command_kwargs)

    return results


# Return exit code
def run_job_exit(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the exit code.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The exit code from srun.

    Example:
        >>> run_job_exit('-n 4 --output=output.txt ./my_executable', timeout=60)
        2
        >>> run_job_exit('-n 2 --error=error.txt ./my_executable', fatal=True)
        0
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results["exit_code"]


# Return output
def run_job_output(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the standard output.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The standard output from srun.

    Example:
        >>> run_job_output('-n 4 ./my_executable', timeout=60)
        'stdout of the command'
        >>> run_job_output('-n 2 --output=output.txt ./my_executable', fatal=True)
        ''
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results["stdout"]


# Return error
def run_job_error(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the standard error.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The standard error from srun.

    Example:
        >>> run_job_error('-n 4 --output=output.txt ./my_executable', timeout=60)
        'stderr of the command'
        >>> run_job_error('-n 200000 ./my_executable', fatal=True) # Will automatically fail the test due to resources
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results["stderr"]


# Return job id
def submit_job_srun(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the job id.

    This function obtains the job id by adding the -v option to srun and parsing
    out the job id. If the srun command times out, it will automatically be
    cancelled when the test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The job id from srun.

    Example:
        >>> submit_job_srun('-n 4 --output=output.txt ./my_executable')
        12345
        >>> submit_job_srun('-n 2 --output=output.txt ./my_executable', timeout=60)
        67890
    """

    results = run_job(" ".join(["-v", srun_args]), **run_command_kwargs)

    if match := re.search(r"jobid (\d+)", results["stderr"]):
        return int(match.group(1))
    else:
        return 0


# Return job id (command should not be interactive/shell)
def submit_job_salloc(salloc_args, **run_command_kwargs):
    """Submits a job using salloc and returns the job id.

    The submitted job will automatically be cancelled when the test ends.

    Args:
        salloc_args (string): The arguments to salloc.

    Returns:
        The job id.

    Example:
        >>> submit_job_salloc('-N 1 -t 60 --output=output.txt ./my_executable')
        12345
        >>> submit_job_salloc('-N 2 -t 120 --output=output.txt ./my_executable', timeout=60)
        67890
    """

    results = run_command(f"salloc {salloc_args}", **run_command_kwargs)
    if match := re.search(r"Granted job allocation (\d+)", results["stderr"]):
        job_id = int(match.group(1))
        properties["submitted-jobs"].append(job_id)
        return job_id
    else:
        return 0


# Return job id
def submit_job(command, job_param, job, *, wrap_job=True, **run_command_kwargs):
    """Submits a job using the given command and returns the job id.

    Args:
        command (string): The command to submit the job (salloc, srun, sbatch).
        job_param (string): The arguments to the job.
        job (string): The command or job file to be executed.
        wrap_job (boolean): If True, the job will be wrapped when the command is 'sbatch'.

    Returns:
        The job id.

    Example:
        >>> submit_job('salloc', '-N 1 -t 60', './my_executable', quiet=True)
        12345
        >>> submit_job('srun', '-N 2 -t 120', './my_executable', quiet=True)
        67890
        >>> submit_job('sbatch', '-N 1 -t 60', './my_script.sh', wrap_job=False, quiet=True)
        23456
    """

    # Make sure command is a legal command to run a job
    assert command in [
        "salloc",
        "srun",
        "sbatch",
    ], f"Invalid command '{command}'. Should be salloc, srun, or sbatch."

    if command == "salloc":
        return submit_job_salloc(f"{job_param} {job}", **run_command_kwargs)
    elif command == "srun":
        return submit_job_srun(f"{job_param} {job}", **run_command_kwargs)
    elif command == "sbatch":
        # If the job should be wrapped, do so before submitting
        if wrap_job:
            job = f"--wrap '{job}'"
        return submit_job_sbatch(f"{job_param} {job}", **run_command_kwargs)


def run_job_nodes(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the allocated node list.

    This function obtains the job id by adding the -v option to srun and parsing
    out the allocated node list. If the srun command times out, it will
    automatically be cancelled when the test ends.

    Args:
        srun_args (string): The arguments to srun.

    Returns:
        The allocated node list for the job.

    Example:
        >>> run_job_nodes('-N 2 hostname', quiet=True)
        ['node001', 'node002']
        >>> run_job_nodes('-N 1 --exclude=node001 hostname', quiet=True)
        ['node002']
    """

    results = run_command(f"srun -v {srun_args}", **run_command_kwargs)
    node_list = []
    if results["exit_code"] == 0:
        if match := re.search(r"jobid \d+: nodes\(\d+\):`([^']+)'", results["stderr"]):
            node_list = node_range_to_list(match.group(1))
    return node_list


def get_jobs(job_id=None, **run_command_kwargs):
    """Returns the job configuration as a dictionary of dictionaries.

    Args:
        job_id (integer): The id of a specific job of which to get parameters.

    Returns:
        A dictionary of dictionaries where the first level keys are the job ids
        and with their values being a dictionary of configuration parameters for
        the respective job.

    Example:
        >>> get_jobs()
        {38: {'JobId': 38, 'JobName': 'wrap', 'UserId': 'atf(1002)', 'GroupId': 'atf(1002)', ...},
         39: {'JobId': 39, 'JobName': 'wrap', 'UserId': 'atf(1002)', 'GroupId': 'atf(1002)', ...}}
        >>> get_jobs(job_id='12345', quiet=True)
        {12345: {'JobId': '12345', 'JobName': 'foo.sh', 'UserId': 'test(1003)', ...}}
    """

    jobs_dict = {}

    command = "scontrol -d -o show jobs"
    if job_id is not None:
        command += f" {job_id}"
    output = run_command_output(command, fatal=True, **run_command_kwargs)

    job_dict = {}
    for line in output.splitlines():
        if line == "":
            continue

        while match := re.search(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", line):
            param_name, param_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", "", line)

            # Reformat the value if necessary
            if is_integer(param_value):
                param_value = int(param_value)
            elif is_float(param_value):
                param_value = float(param_value)
            elif param_value == "(null)":
                param_value = None

            # Add it to the temporary job dictionary
            job_dict[param_name] = param_value

        # Add the job dictionary to the jobs dictionary
        if job_dict:
            jobs_dict[job_dict["JobId"]] = job_dict

            # Clear the job dictionary for use by the next job
            job_dict = {}

    return jobs_dict


def get_steps(step_id=None, **run_command_kwargs):
    """Returns the steps as a dictionary of dictionaries.

    Args:
        step_id (string): The specific step ID to retrieve information for. If
            not provided, information for all steps will be returned.

    Returns:
        A dictionary of dictionaries where the first level keys are the step ids
        and with their values being a dictionary of configuration parameters for
        the respective step.

    Example:
        >>> get_steps()
        {'44.batch': {'StepId': '44.batch', 'UserId': 1002, 'StartTime': '2024-04-05T01:17:53', ...},
         '44.0': {'StepId': 44.0, 'UserId': 1002, 'StartTime': '2024-04-05T01:17:54', ...},
         '45.batch': {'StepId': '45.batch', 'UserId': 1002, 'StartTime': '2024-04-05T01:17:53', ...},
         '45.0': {'StepId': 45.0, 'UserId': 1002, 'StartTime': '2024-04-05T01:17:54', ...}}
        >>> get_steps(step_id='123.0', quiet=True)
        {'123.0': {'StepId': 123.0, 'UserId': 1002, 'StartTime': '2024-04-05T01:21:14', ...}}
    """

    steps_dict = {}

    command = "scontrol -d -o show steps"
    if step_id is not None:
        command += f" {step_id}"
    output = run_command_output(command, fatal=True, **run_command_kwargs)

    step_dict = {}
    for line in output.splitlines():
        if line == "":
            continue

        while match := re.search(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", line):
            param_name, param_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", "", line)

            # Reformat the value if necessary
            if is_integer(param_value):
                param_value = int(param_value)
            elif is_float(param_value):
                param_value = float(param_value)
            elif param_value == "(null)":
                param_value = None

            # Add it to the temporary job dictionary
            step_dict[param_name] = param_value

        # Add the job dictionary to the jobs dictionary
        if step_dict:
            steps_dict[str(step_dict["StepId"])] = step_dict

            # Clear the job dictionary for use by the next job
            step_dict = {}

    return steps_dict


def get_job(job_id, quiet=False):
    """Returns job information for a specific job as a dictionary.

    Args:
        job_id (integer): The id of the job for which information is requested.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        A dictionary containing parameters for the specified job. If the job id
        is not found, an empty dictionary is returned.

    Example:
        >>> get_job(51)
        {'JobId': 51, 'JobName': 'wrap', 'UserId': 'atf(1002)', 'GroupId': 'atf(1002)', ...}
        >>> get_job(182, quiet=True)
        {'JobId': 182, 'JobName': 'foo.sh', 'UserId': 'atf(1002)', 'GroupId': 'atf(1002)', ...}
    """

    jobs_dict = get_jobs(job_id, quiet=quiet)

    return jobs_dict[job_id] if job_id in jobs_dict else {}


def get_job_parameter(job_id, parameter_name, default=None, quiet=False):
    """Returns the value of a specific parameter for a given job.

    Args:
        job_id (integer): The id of the job for which the parameter value is requested.
        parameter_name (string): The name of the parameter whose value is to be obtained.
        default (string or None): The value to be returned if the parameter is not found.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        The value of the specified job parameter, or the default value if the
        parameter is not found.

    Example:
        >>> get_job_parameter(12345, 'UserId')
        'atf(1002)'
        >>> get_job_parameter(67890, 'Partition', default='normal', quiet=True)
        'primary'
    """

    jobs_dict = get_jobs(quiet=quiet)

    if job_id in jobs_dict:
        job_dict = jobs_dict[job_id]
    else:
        pytest.fail(f"Job ({job_id}) was not found in the job configuration")

    if parameter_name in job_dict:
        return job_dict[parameter_name]
    else:
        return default


def get_job_id_from_array_task(array_job_id, array_task_id, fatal=False, quiet=True):
    """Returns the raw job id of a task of a job array.

    Args:
        array_job_id (integer): The id of the job array.
        array_task_id (integer): The id of the task of the job array.
        fatal (boolean): If True, fails if the raw job id is not found in the system.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        The raw job id of the given task of a job array, or 0 if not found.

    Example:
        >>> get_job_id_from_array_task(234, 2)
        241
    """

    jobs_dict = get_jobs(quiet=quiet)
    for job_id, job_values in jobs_dict.items():
        if (
            job_values["ArrayJobId"] == array_job_id
            and job_values["ArrayTaskId"] == array_task_id
        ):
            return job_id

    if fatal:
        pytest.fail(f"{array_job_id}_{array_task_id} was not found in the system")

    return 0


def get_step_parameter(step_id, parameter_name, default=None, quiet=False):
    """Returns the value of a specific parameter for a given step.

    Args:
        step_id (string): The id of the step for which the parameter value is requested.
        parameter_name (string): The name of the parameter whose value is to be obtained.
        default (string or None): The value to be returned if the parameter is not found.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        The value of the specified step parameter, or the default value if the
        parameter is not found.

    Example:
        >>> get_step_parameter("45.0", 'NodeList')
        'node[2,4]'
        >>> get_step_parameter("60.1", 'Partition', default='normal', quiet=True)
        'primary'
    """

    steps_dict = get_steps(quiet=quiet)

    if step_id not in steps_dict:
        logging.debug(f"Step ({step_id}) was not found in the step list")
        return default

    step_dict = steps_dict[step_id]
    if parameter_name in step_dict:
        return step_dict[parameter_name]
    else:
        return default


def wait_for_node_state(
    nodename,
    desired_node_state,
    timeout=default_polling_timeout,
    poll_interval=None,
    fatal=False,
    reverse=False,
):
    """Wait for a specified node state to be reached.

    Polls the node state every poll interval seconds, waiting up to the timeout
    for the specified node state to be reached.

    Args:
        nodename (string): The name of the node whose state is being monitored.
        desired_node_state (string): The state that the node is expected to reach.
        timeout (integer): The number of seconds to wait before timing out.
        poll_interval (float): Number of seconds between node state polls.
        fatal (boolean): If True, a timeout will cause the test to fail.
        reverse (boolean): If True, wait for the node to lose the desired state.

    Returns:
        Boolean value indicating whether the node ever reached the desired state.

    Example:
        >>> wait_for_node_state('node1', 'IDLE', timeout=60, poll_interval=5)
        True
        >>> wait_for_node_state('node2', 'DOWN', timeout=30, fatal=True)
        False
    """

    # Figure out if we're waiting for the desired_node_state to be present or to be gone
    if reverse:
        condition = lambda state: desired_node_state not in state.split("+")
    else:
        condition = lambda state: desired_node_state in state.split("+")

    # Wrapper for the repeat_until command to do all our state checking for us
    repeat_until(
        lambda: get_node_parameter(nodename, "State"),
        condition,
        timeout=timeout,
        poll_interval=poll_interval,
        fatal=fatal,
    )

    return (
        desired_node_state in get_node_parameter(nodename, "State").split("+")
    ) != reverse


def wait_for_step(job_id, step_id, **repeat_until_kwargs):
    """Wait for the specified step of a job to be running.

    Continuously polls the step state until it becomes running or until a
    timeout occurs.

    Args:
        job_id (integer): The id of the job.
        step_id (integer): The id of the step within the job.

    Returns:
        A boolean value indicating whether the specified step is running or not.

    Example:
        >>> wait_for_step(1234, 0, timeout=60, poll_interval=5, fatal=True)
        True
        >>> wait_for_step(5678, 1, timeout=30)
        False
    """

    step_str = f"{job_id}.{step_id}"
    return repeat_until(
        lambda: run_command_output(f"scontrol -o show step {step_str}"),
        lambda out: re.search(f"StepId={step_str}", out) is not None,
        **repeat_until_kwargs,
    )


def wait_for_step_accounted(job_id, step_id, **repeat_until_kwargs):
    """Wait for specified job step to appear in accounting database (`sacct`).

    Continuously polls the database until the step is accounted for or until a
    timeout occurs.

    Args:
        job_id (integer): The id of the job.
        step_id (integer): The id of the step within the job.

    Returns:
        A boolean value indicating whether the specified step is accounted for
        in the database or not.

    Example:
        >>> wait_for_step_accounted(1234, 0, timeout=60, poll_interval=5, fatal=True)
        True
        >>> wait_for_step_accounted(5678, 1, timeout=30)
        False
    """

    step_str = f"{job_id}.{step_id}"
    return repeat_until(
        lambda: run_command_output(f"sacct -j {job_id} -o jobid"),
        lambda out: re.search(f"{step_str}", out) is not None,
        **repeat_until_kwargs,
    )


def wait_for_job_state(
    job_id,
    desired_job_state,
    timeout=default_polling_timeout,
    poll_interval=None,
    fatal=False,
    quiet=False,
):
    """Wait for the specified job to reach the desired state.

    Continuously polls the job state until the desired state is reached or until
    a timeout occurs.

    Some supported job states are aggregate states, which may include multiple
    discrete states. Some logic is built-in to fail if a job reaches a state
    that makes the desired job state impossible to reach.

    Current supported aggregate states:
    - DONE

    Args:
        job_id (integer): The id of the job.
        desired_job_state (string): The desired state of the job.
        timeout (integer): The number of seconds to poll before timing out.
        poll_interval (float): Time (in seconds) between job state polls.
        fatal (boolean): If True, a timeout will cause the test to fail.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        Boolean value indicating whether the job reached the desired state.

    Example:
        >>> wait_for_job_state(1234, 'COMPLETED', timeout=300, poll_interval=10, fatal=True)
        True
        >>> wait_for_job_state(5678, 'RUNNING', timeout=60)
        False
    """

    if poll_interval is None:
        if timeout <= 5:
            poll_interval = 0.1
        elif timeout <= 10:
            poll_interval = 0.2
        else:
            poll_interval = 1

    if quiet:
        log_level = logging.TRACE
    else:
        log_level = logging.DEBUG

    # We don't use repeat_until here because we support pseudo-job states and
    # we want to allow early return (e.g. for a DONE state if we want RUNNING)
    begin_time = time.time()
    logging.log(
        log_level, f"Waiting for job ({job_id}) to reach state {desired_job_state}"
    )

    while time.time() < begin_time + timeout:
        job_state = get_job_parameter(
            job_id, "JobState", default="NOT_FOUND", quiet=True
        )

        if job_state in [
            "NOT_FOUND",
            "BOOT_FAIL",
            "CANCELLED",
            "COMPLETED",
            "DEADLINE",
            "FAILED",
            "NODE_FAIL",
            "OUT_OF_MEMORY",
            "TIMEOUT",
            "PREEMPTED",
        ]:
            if desired_job_state == "DONE" or job_state == desired_job_state:
                logging.log(
                    log_level, f"Job ({job_id}) is in desired state {desired_job_state}"
                )
                return True
            else:
                message = f"Job ({job_id}) is in state {job_state}, but we wanted {desired_job_state}"
                if fatal:
                    pytest.fail(message)
                else:
                    logging.warning(message)
                    return False
        elif job_state == desired_job_state:
            logging.log(
                log_level, f"Job ({job_id}) is in desired state {desired_job_state}"
            )
            return True
        else:
            logging.log(
                log_level,
                f"Job ({job_id}) is in state {job_state}, but we are waiting for {desired_job_state}",
            )

        time.sleep(poll_interval)

    message = f"Job ({job_id}) did not reach the {desired_job_state} state within the {timeout} second timeout"
    if fatal:
        pytest.fail(message)
    else:
        logging.warning(message)
        return False


def check_steps_delayed(job_id, job_output, expected_delayed):
    """Check the output file of a job for expected delayed steps.

    This function checks that the output file for a job contains the expected
    pattern of delayed job steps. Note that at the time of writing, this
    requires srun steps to have at least a verbosity level of "-vv" to log
    their f"srun: Step completed in JobId={job_id}, retrying" notification.

    Args:
        job_id (integer): The job ID that we're interested in.
        job_output (string): The content of the output file of the job.
        expected_delayed (integer): The initial number of delayed job steps. It
            is verified that this initial number of job steps are delayed and
            then this number of delayed job steps decrements one by one as
            running job steps finish.

    Returns:
        True if steps were delayed in the correct amounts and order, else False.

    Example:
        >>> check_steps_delayed(123, "srun: Received task exit notification for 1 task of StepId=1.0 (status=0x0000).\nsrun: node1: task 0: Completed\nsrun: debug:  task 0 done\nsrun: Step completed in JobId=1, retrying\nsrun: Step completed in JobId=1, retrying\nsrun: Step created for StepId=1.1\nsrun: Received task exit notification for 1 task of StepId=1.1 (status=0x0000).\nsrun: node2: task 1: Completed\nsrun: debug:  task 1 done\nsrun: debug:  IO thread exiting\nsrun: Step completed in JobId=1, retrying\nsrun: Step created for StepId=1.2", 2)
        True
        >>> check_steps_delayed(456, "srun: Received task exit notification for 1 task of StepId=1.0 (status=0x0000).\nsrun: node1: task 0: Completed\nsrun: debug:  task 0 done\nsrun: Step completed in JobId=1, retrying\nsrun: Step completed in JobId=1, retrying\nsrun: Step created for StepId=1.1\nsrun: Received task exit notification for 1 task of StepId=1.1 (status=0x0000).\nsrun: node2: task 1: Completed\nsrun: debug:  task 1 done\nsrun: debug:  IO thread exiting", 2)
        False
    """

    # Iterate through each group of expected delayed steps. For example,
    # if there was a job that had 5 steps that could run in parallel but, due to
    # resource constrains, only allowed 3 steps to run at a time, we would
    # expect a group of 2 delayed job steps followed by a group of 1 delayed job
    # steps. For this example job, expected_delayed=2.
    #
    # The idea of the for loop below is to iterate through each group of delayed
    # job steps and replace the expected output as we go with re.sub. This
    # ensures that the delayed job step groups occur in the correct order.
    #
    # Each regex pattern matches part of the pattern we'd expect to see in the
    # output, replaces the matched text (see previous paragraph), and then makes
    # sure there is still text left to match for the rest of the pattern. If the
    # regex pattern doesn't match anything, then re.sub will match and replace
    # all the rest of the output and leave job_output empty.
    for delayed_grp_size in range(expected_delayed, 0, -1):
        # Match all lines before receiving an exit notification. This regex
        # pattern will match any line that doesn't contain "srun: Received task
        # exit notification".
        before_start_pattern = r"(^((?!srun: Received task exit notification).)*$\n)*"
        job_output = re.sub(before_start_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {before_start_pattern}")
            return False

        # Match receiving the next exit notification. This regex pattern will
        # match the line where the exit notification is received when a step
        # that was already running finishes.
        exit_pattern = rf"srun: Received task exit notification for \[0-9]+ task of StepId={job_id}\.[0-9]+ \(status=0x[0-9A-Fa-f]+\)\.\n"
        job_output = re.sub(exit_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {exit_pattern}")
            return False

        # Match lines we don't want before a step completion. After the exit
        # notification, we now match all lines that don't contain "srun: Step
        # completed". Sometimes an exit notification can be received multiple
        # times and any redundant exit notifications are also matched by this
        # pattern.
        before_completed_pattern = r"(^((?!srun: Step completed).)*$\n)*"
        job_output = re.sub(before_completed_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {before_completed_pattern}")
            return False

        # Match number of lines retrying to start a delayed job step. Note that
        # this pattern searched for steps retrying "delayed_grp_size" number of
        # times. This is because every step that is delayed retries every time a
        # previously running step finishes.
        completed_pattern = rf"(srun: Step completed in JobId={job_id}, retrying\n){{{delayed_grp_size}}}"
        job_output = re.sub(completed_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {completed_pattern}")
            return False

        # Match lines we don't want before a step creation. Due to steps running
        # in parallel, other lines of text can be output from already running
        # steps before we're told a new step has been created. This regex
        # pattern matches all lines that don't contain "srun: Step created".
        before_created_pattern = r"(^((?!srun: Step created).)*$\n)*"
        job_output = re.sub(before_created_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {before_created_pattern}")
            return False

        # Match the step creation line for the delayed step
        created_pattern = rf"srun: Step created for StepId={job_id}\.[0-9]+"
        job_output = re.sub(created_pattern, "", job_output, 1, re.MULTILINE)
        if not job_output:
            logging.error(f"Pattern not found: {created_pattern}")
            return False

    return True


def create_node(node_dict):
    """Creates a node with the properties described by the supplied dictionary.

    This function is currently only used as a helper function within other
    library functions (e.g. require_nodes). It modifies the Slurm configuration
    file and restarts the relevant Slurm daemons. A backup is automatically
    created, and the original configuration is restored after the test
    completes. This function may only be used in auto-config mode.

    Args:
        node_dict (dictionary): A dictionary containing the desired node
            properties.

    Returns:
        None

    Example:
        >>> create_node({'NodeName': 'new_node', 'CPUs': 4, 'RealMemory': 16384})
    """

    if not properties["auto-config"]:
        require_auto_config("wants to add a node")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(
        f"cat {config_file}", user=properties["slurm-user"], quiet=True
    )
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the last NodeName definition
    last_node_line_index = 0
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        if re.search(r"(?i)^ *NodeName", line) is not None:
            last_node_line_index = line_index
    if last_node_line_index == 0:
        last_node_line_index = line_index

    # Build up the new node line
    node_line = ""
    if "NodeName" in node_dict:
        node_line = f"NodeName={node_dict['NodeName']}"
        node_dict.pop("NodeName")
    if "Port" in node_dict:
        node_line += f" Port={node_dict['Port']}"
        node_dict.pop("Port")
    for parameter_name, parameter_value in sorted(node_dict.items()):
        node_line += f" {parameter_name}={parameter_value}"

    # Add the new node line
    new_config_lines.insert(last_node_line_index + 1, node_line)

    # Write the config file back out with the modifications
    backup_config_file("slurm")
    new_config_string = "\n".join(new_config_lines)
    run_command(
        f"echo '{new_config_string}' > {config_file}",
        user=properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    # Restart slurm if it is already running
    if is_slurmctld_running(quiet=True):
        restart_slurm(quiet=True)


# requirements_list is a list of (parameter_name, parameter_value) tuples.
# Uses non-live node info because must copy from existing node config line
# We implemented requirements_list as a list of tuples so that this could
# later be extended to include a comparator, etc.
# atf.require_nodes(1, [('CPUs', 4), ('RealMemory', 40)])
# atf.require_nodes(2, [('Gres', 'gpu:1,mps:100')])
def require_nodes(requested_node_count, requirements_list=[]):
    """Ensure that a requested number of nodes have the required properties.

    In local-config mode, the test is skipped if an insufficient number of
    nodes possess the required properties.
    In auto-config mode, nodes are created as needed with the required
    properties.

    Args:
        requested_node_count (integer): Number of required nodes.
        requirements_list (list of tuples): List of (parameter_name,
            parameter_value) tuples.

    Currently supported node requirement types include:
        CPUs
        Cores
        RealMemory
        Gres
        Features

    Returns:
        None

    Example:
        >>> require_nodes(2, [('CPUs', 4), ('RealMemory', 40)])
        >>> require_nodes(2, [('CPUs', 2), ('RealMemory', 30), ('Features', 'gpu,mpi')])
    """

    # If using local-config and slurm is running, use live node information
    # so that a test is not incorrectly skipped when slurm derives a non-single
    # CPUTot while the slurm.conf does not contain a CPUs property.
    if not properties["auto-config"] and is_slurmctld_running(quiet=True):
        live = True
    else:
        live = False

    # This should return separate nodes and a DEFAULT (unless live)
    nodes_dict = get_nodes(live=live, quiet=True)
    original_nodes = {}
    default_node = {}

    # Instantiate original node names from nodes_dict
    for node_name in nodes_dict:
        # Split out the default node
        if node_name == "DEFAULT":
            default_node = nodes_dict[node_name]
        else:
            original_nodes[node_name] = {}

    # Populate with any default parameters
    if default_node:
        for node_name in original_nodes:
            for parameter_name, parameter_value in default_node.items():
                if parameter_name.lower() != "nodename":
                    original_nodes[node_name][parameter_name] = parameter_value

    # Merge in parameters from nodes_dict
    for node_name in original_nodes:
        for parameter_name, parameter_value in nodes_dict[node_name].items():
            if parameter_name.lower() != "nodename":
                # Translate CPUTot to CPUs for screening qualifying nodes
                if parameter_name.lower() == "cputot":
                    parameter_name = "CPUs"
                original_nodes[node_name][parameter_name] = parameter_value

    # Check to see how many qualifying nodes we have
    qualifying_node_count = 0
    node_count = 0
    nonqualifying_node_count = 0
    first_node_name = ""
    first_qualifying_node_name = ""
    node_indices = {}
    augmentation_dict = {}
    for node_name in sorted(original_nodes):
        lower_node_dict = dict(
            (key.lower(), value) for key, value in original_nodes[node_name].items()
        )
        node_count += 1

        if node_count == 1:
            first_node_name = node_name

        # Build up node indices for use when having to create new nodes
        match = re.search(r"^(.*?)(\d*)$", node_name)
        node_prefix, node_index = match.group(1), match.group(2)
        if node_index == "":
            node_indices[node_prefix] = node_indices.get(node_prefix, [])
        else:
            node_indices[node_prefix] = node_indices.get(node_prefix, []) + [
                int(node_index)
            ]

        node_qualifies = True
        for requirement_tuple in requirements_list:
            parameter_name, parameter_value = requirement_tuple[0:2]
            if parameter_name in ["CPUs", "RealMemory"]:
                if parameter_name.lower() in lower_node_dict:
                    if lower_node_dict[parameter_name.lower()] < parameter_value:
                        if node_qualifies:
                            node_qualifies = False
                            nonqualifying_node_count += 1
                        if nonqualifying_node_count == 1:
                            augmentation_dict[parameter_name] = parameter_value
                else:
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict[parameter_name] = parameter_value
            elif parameter_name == "Cores":
                boards = lower_node_dict.get("boards", 1)
                sockets_per_board = lower_node_dict.get("socketsperboard", 1)
                cores_per_socket = lower_node_dict.get("corespersocket", 1)
                sockets = boards * sockets_per_board
                cores = sockets * cores_per_socket
                if cores < parameter_value:
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict["CoresPerSocket"] = math.ceil(
                            parameter_value / sockets
                        )
            elif parameter_name == "Gres":
                if parameter_name.lower() in lower_node_dict:
                    gres_list = parameter_value.split(",")
                    for gres_value in gres_list:
                        if match := re.search(r"^(\w+):(\d+)$", gres_value):
                            (required_gres_name, required_gres_value) = (
                                match.group(1),
                                match.group(2),
                            )
                        else:
                            pytest.fail(
                                "Gres requirement must be of the form <name>:<count>"
                            )
                        if match := re.search(
                            rf"{required_gres_name}:(\d+)",
                            lower_node_dict[parameter_name.lower()],
                        ):
                            if match.group(1) < required_gres_value:
                                if node_qualifies:
                                    node_qualifies = False
                                    nonqualifying_node_count += 1
                                if nonqualifying_node_count == 1:
                                    augmentation_dict[parameter_name] = gres_value
                        else:
                            if node_qualifies:
                                node_qualifies = False
                                nonqualifying_node_count += 1
                            if nonqualifying_node_count == 1:
                                augmentation_dict[parameter_name] = gres_value
                else:
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict[parameter_name] = parameter_value
            elif parameter_name == "Features":
                required_features = set(parameter_value.split(","))
                node_features = set(lower_node_dict.get("features", "").split(","))
                if not required_features.issubset(node_features):
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict[parameter_name] = parameter_value
            else:
                pytest.fail(f"{parameter_name} is not a supported requirement type")
        if node_qualifies:
            qualifying_node_count += 1
            if first_qualifying_node_name == "":
                first_qualifying_node_name = node_name

    # Not enough qualifying nodes
    if qualifying_node_count < requested_node_count:
        # If auto-config, configure what is required
        if properties["auto-config"]:
            # Create new nodes to meet requirements ignoring default node0
            new_node_count = requested_node_count

            # If we already have a qualifying node, we will use it as the template
            if qualifying_node_count > 0:
                template_node_name = first_qualifying_node_name
                template_node = nodes_dict[template_node_name].copy()
            # Otherwise we will use the first node as a template and augment it
            else:
                template_node_name = first_node_name
                template_node = nodes_dict[template_node_name].copy()
                for parameter_name, parameter_value in augmentation_dict.items():
                    template_node[parameter_name] = parameter_value

            base_port = int(nodes_dict[template_node_name]["Port"])

            # Build up a list of available new indices starting after the template
            match = re.search(r"^(.*?)(\d*)$", template_node_name)
            template_node_prefix, template_node_index = match.group(1), int(
                match.group(2)
            )
            used_indices = sorted(node_indices[template_node_prefix])
            new_indices = []
            new_index = template_node_index
            for i in range(new_node_count):
                new_index += 1
                while new_index in used_indices:
                    new_index += 1
                new_indices.append(new_index)

            # Create a new aggregate node
            # Later, we could consider collapsing the node into the template node if unmodified
            new_node_dict = template_node.copy()
            if new_node_count == 1:
                new_node_dict["NodeName"] = template_node_prefix + str(new_indices[0])
                new_node_dict["Port"] = base_port - template_node_index + new_indices[0]
            else:
                new_node_dict[
                    "NodeName"
                ] = f"{template_node_prefix}[{list_to_range(new_indices)}]"
                new_node_dict["Port"] = list_to_range(
                    list(
                        map(lambda x: base_port - template_node_index + x, new_indices)
                    )
                )
            create_node(new_node_dict)

        # If local-config, skip
        else:
            message = f"This test requires {requested_node_count} nodes"
            if requirements_list:
                message += f" with {requirements_list}"
            pytest.skip(message, allow_module_level=True)


def make_bash_script(script_name, script_contents):
    """Creates an executable Bash script with the specified contents.

    Args:
        script_name (string): Name of the script to create.
        script_contents (string): Contents of the script.

    Returns:
        None

    Example:
        >>> make_bash_script("my_script.sh", "echo 'Hello, World!'") # This creates an executable Bash script named "my_script.sh" with the contents "echo 'Hello, World!'"
    """

    with open(script_name, "w") as f:
        f.write("#!/bin/bash\n")
        f.write(script_contents)
    os.chmod(script_name, 0o0700)


def wait_for_file(file_name, **repeat_until_kwargs):
    """Waits for the specified file to be present.

    This function waits up to the specified timeout seconds for the file to be
    present, polling every poll_interval seconds. The default timeout and
    poll_interval are inherited from repeat_until.

    Args:
        file_name (string): The file name.

    Returns:
        True if the file was found, False otherwise.

    Example:
        >>> wait_for_file("my_file.txt", timeout=30, poll_interval=0.5)
        True
    """

    logging.debug(f"Waiting for file ({file_name}) to be present")
    return repeat_until(
        lambda: os.path.isfile(file_name), lambda exists: exists, **repeat_until_kwargs
    )


# Assuming this will only be called internally after validating accounting is configured and auto-config is set
def backup_accounting_database():
    """Backs up the accounting database.

    This function may only be used in auto-config mode. The database dump is
    automatically restored when the test ends.

    Args:
        None

    Returns:
        None

    Example:
        >>> backup_accounting_database() # Backs up Slurm accounting database to file in the test's temporary directory.
    """

    if not properties["auto-config"]:
        return

    mysqldump_path = shutil.which("mysqldump")
    if mysqldump_path is None:
        pytest.fail(
            "Unable to backup the accounting database. mysqldump was not found in your path"
        )
    mysql_path = shutil.which("mysql")
    if mysql_path is None:
        pytest.fail(
            "Unable to backup the accounting database. mysql was not found in your path"
        )

    sql_dump_file = f"{str(module_tmp_path / '../../slurm_acct_db.sql')}"

    # We set this here, because we will want to restore in all cases
    properties["accounting-database-modified"] = True

    # If a dump already exists, issue a warning and return (honor existing dump)
    if os.path.isfile(sql_dump_file):
        logging.warning(f"Dump file already exists ({sql_dump_file})")
        return

    slurmdbd_dict = get_config(live=False, source="slurmdbd", quiet=True)
    database_host, database_port, database_name, database_user, database_password = (
        slurmdbd_dict.get(key)
        for key in [
            "StorageHost",
            "StoragePort",
            "StorageLoc",
            "StorageUser",
            "StoragePass",
        ]
    )

    mysql_options = ""
    if database_host:
        mysql_options += f" -h {database_host}"
    if database_port:
        mysql_options += f" -P {database_port}"
    if database_user:
        mysql_options += f" -u {database_user}"
    else:
        mysql_options += f" -u {properties['slurm-user']}"
    if database_password:
        mysql_options += f" -p {database_password}"

    if not database_name:
        database_name = "slurm_acct_db"

    # If the slurm database does not exist, touch an empty dump file with
    # the sticky bit set. restore_accounting_database will remove the file.
    mysql_command = f"{mysql_path} {mysql_options} -e \"USE '{database_name}'\""
    if run_command_exit(mysql_command, quiet=True) != 0:
        # logging.warning(f"Slurm accounting database ({database_name}) is not present")
        run_command(f"touch {sql_dump_file}", fatal=True, quiet=True)
        run_command(f"chmod 1000 {sql_dump_file}", fatal=True, quiet=True)

    # Otherwise, copy the config file to the backup
    else:
        mysqldump_command = (
            f"{mysqldump_path} {mysql_options} {database_name} > {sql_dump_file}"
        )
        run_command(
            mysqldump_command, fatal=True, quiet=True, timeout=default_sql_cmd_timeout
        )


def restore_accounting_database():
    """Restores the accounting database from the backup.

    This function may only be used in auto-config mode.

    Args:
        None

    Returns:
        None

    Example:
        >>> restore_accounting_database() # Restores Slurm accounting database from previously created backup.
    """

    if not properties["accounting-database-modified"] or not properties["auto-config"]:
        return

    sql_dump_file = f"{str(module_tmp_path / '../../slurm_acct_db.sql')}"

    # If the dump file doesn't exist, it has probably already been
    # restored by a previous call to restore_accounting_database
    if not os.path.isfile(sql_dump_file):
        logging.warning(
            f"Slurm accounting database backup ({sql_dump_file}) is s not present. It has probably already been restored."
        )
        return

    mysql_path = shutil.which("mysql")
    if mysql_path is None:
        pytest.fail(
            "Unable to restore the accounting database. mysql was not found in your path"
        )

    slurmdbd_dict = get_config(live=False, source="slurmdbd", quiet=True)
    database_host, database_port, database_name, database_user, database_password = (
        slurmdbd_dict.get(key)
        for key in [
            "StorageHost",
            "StoragePort",
            "StorageLoc",
            "StorageUser",
            "StoragePass",
        ]
    )
    if not database_name:
        database_name = "slurm_acct_db"

    base_command = mysql_path
    if database_host:
        base_command += f" -h {database_host}"
    if database_port:
        base_command += f" -P {database_port}"
    if database_user:
        base_command += f" -u {database_user}"
    else:
        base_command += f" -u {properties['slurm-user']}"
    if database_password:
        base_command += f" -p {database_password}"

    # If the sticky bit is set and the dump file is empty, remove the database.
    # Otherwise, restore the dump.

    run_command(
        f'{base_command} -e "drop database {database_name}"',
        fatal=True,
        quiet=False,
        timeout=default_sql_cmd_timeout,
    )
    dump_stat = os.stat(sql_dump_file)
    if not (dump_stat.st_size == 0 and dump_stat.st_mode & stat.S_ISVTX):
        run_command(
            f'{base_command} -e "create database {database_name}"',
            fatal=True,
            quiet=True,
        )
        run_command(
            f"{base_command} {database_name} < {sql_dump_file}",
            fatal=True,
            quiet=True,
            timeout=default_sql_cmd_timeout,
        )

    # In either case, remove the dump file
    run_command(f"rm -f {sql_dump_file}", fatal=True, quiet=True)

    properties["accounting-database-modified"] = False


def compile_against_libslurm(
    source_file, dest_file, build_args="", full=False, shared=False
):
    """Compiles a test program against either libslurm.so or libslurmfull.so.

    This function compiles the specified source file against the Slurm library,
    either libslurm.so or libslurmfull.so, and creates the target binary file.

    Args:
        source_file (string): The name of the source file.
        dest_file (string): The name of the target binary file.
        build_args (string): Additional string to be appended to the build command.
        full (boolean): Use libslurmfull.so instead of libslurm.so.
        shared (boolean): Produces a shared library (adds the -shared compiler option
            and adds a .so suffix to the output file name).

    Returns:
        None

    Example:
        >>> compile_against_libslurm("my_test.c", "my_test", build_args="-Wall -Werror")
    """

    if full:
        slurm_library = "slurmfull"
    else:
        slurm_library = "slurm"
    if os.path.isfile(
        f"{properties['slurm-prefix']}/lib64/slurm/lib{slurm_library}.so"
    ):
        lib_dir = "lib64"
    else:
        lib_dir = "lib"
    if full:
        lib_path = f"{properties['slurm-prefix']}/{lib_dir}/slurm"
    else:
        lib_path = f"{properties['slurm-prefix']}/{lib_dir}"

    command = f"gcc {source_file} -g -pthread"
    if shared:
        command += " -fPIC -shared"
    command += f" -o {dest_file}"
    command += f" -I{properties['slurm-source-dir']} -I{properties['slurm-build-dir']} -I{properties['slurm-prefix']}/include -Wl,-rpath={lib_path} -L{lib_path} -l{slurm_library} -lresolv"
    if build_args != "":
        command += f" {build_args}"
    run_command(command, fatal=True)


def get_partitions(**run_command_kwargs):
    """Returns the Slurm partition configuration as a dictionary of dictionaries.

    Args:
        **run_command_kwargs: Auxiliary arguments to be passed to the
            run_command function (e.g., quiet, fatal, timeout, etc.).

    Returns:
        A dictionary of dictionaries, where the first-level keys are the
        partition names, and the values are dictionaries containing the
        configuration parameters for the respective partitions.

    Example:
        >>> get_partitions(quiet=True)
        {'partition1': {'PartitionName': 'partition1', 'AllowGroups': 'ALL', 'Defaults': 'YES', ...},
         'partition2': {'PartitionName': 'partition2', 'AllowGroups': 'group1,group2', 'Defaults': 'YES', ...}}
    """

    partitions_dict = {}

    output = run_command_output(
        "scontrol show partition -o", fatal=True, **run_command_kwargs
    )

    partition_dict = {}
    for line in output.splitlines():
        if line == "":
            continue

        while match := re.search(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", line):
            param_name, param_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r"^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)", "", line)

            # Reformat the value if necessary
            if is_integer(param_value):
                param_value = int(param_value)
            elif is_float(param_value):
                param_value = float(param_value)
            elif param_value == "(null)":
                param_value = None

            # Add it to the temporary partition dictionary
            partition_dict[param_name] = param_value

        # Add the partition dictionary to the partitions dictionary
        partitions_dict[partition_dict["PartitionName"]] = partition_dict

        # Clear the partition dictionary for use by the next partition
        partition_dict = {}

    return partitions_dict


def get_partition_parameter(partition_name, parameter_name, default=None):
    """Obtains the value for a Slurm partition configuration parameter.

    This function retrieves the value of the specified parameter for the given
    partition. If the parameter is not present, the default value is returned.

    Args:
        partition_name (string): The name of the partition.
        parameter_name (string): The name of the parameter to retrieve.
        default (string or None): The default value to return if the parameter is
            not found.

    Returns:
        The value of the specified partition parameter, or the default if not
        found.

    Example:
        >>> get_partition_parameter('my_partition', 'AllowAccounts')
        'ALL'
        >>> get_partition_parameter('second_partition', 'DefaultTime', '00:30:00')
        '00:30:00'
    """

    partitions_dict = get_partitions()

    if partition_name in partitions_dict:
        partition_dict = partitions_dict[partition_name]
    else:
        pytest.fail(
            f"Partition ({partition_name}) was not found in the partition configuration"
        )

    if parameter_name in partition_dict:
        return partition_dict[parameter_name]
    else:
        return default


def set_partition_parameter(partition_name, new_parameter_name, new_parameter_value):
    """Sets the value of the specified partition configuration parameter.

    This function the specified partition property and reconfigures
    the slurm daemons. A backup is automatically created and the
    original configuration is restored after the test completes.

    This function may only be used in auto-config mode.

    Args:
        partition_name (string): The partition name.
        new_parameter_name (string): The parameter name.
        new_parameter_value (string): The parameter value.
            Use a value of None to unset a partition parameter.

    Example:
        >>> set_partition_parameter('partition1', 'MaxTime', 'INFINITE')
    """

    if not properties["auto-config"]:
        require_auto_config("wants to modify partition parameters")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(
        f"cat {config_file}", user=properties["slurm-user"], quiet=True
    )
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the partition among the various Partition definitions
    found_partition_name = False
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        words = re.split(r" +", line.strip())
        if len(words) < 1:
            continue
        parameter_name, parameter_value = words[0].split("=", 1)
        if parameter_name.lower() != "partitionname":
            continue

        if parameter_value == partition_name:
            # We found a matching PartitionName line
            found_partition_name = True

            # Read in the partition parameters
            original_partition_parameters = collections.OrderedDict()
            for word in words[1:]:
                parameter_name, parameter_value = word.split("=", 1)
                original_partition_parameters[parameter_name] = parameter_value

            # Delete the original partition definition
            new_config_lines.pop(line_index)

            # Add the modified definition for the specified partition
            modified_partition_parameters = original_partition_parameters.copy()
            if new_parameter_value is None:
                if new_parameter_name in modified_partition_parameters:
                    del modified_partition_parameters[new_parameter_name]
            else:
                modified_partition_parameters[new_parameter_name] = new_parameter_value
            modified_partition_line = f"PartitionName={partition_name}"
            for (
                parameter_name,
                parameter_value,
            ) in modified_partition_parameters.items():
                modified_partition_line += f" {parameter_name}={parameter_value}"
            new_config_lines.insert(line_index, modified_partition_line)

            break

    if not found_partition_name:
        pytest.fail(
            f"Invalid partition name specified in set_partition_parameter(). Partition {partition_name} does not exist"
        )

    # Write the config file back out with the modifications
    backup_config_file("slurm")
    new_config_string = "\n".join(new_config_lines)
    run_command(
        f"echo '{new_config_string}' > {config_file}",
        user=properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    # Reconfigure slurm controller if it is already running
    if is_slurmctld_running(quiet=True):
        run_command("scontrol reconfigure", user=properties["slurm-user"], quiet=True)


def default_partition():
    """Returns the name of the default Slurm partition.

    This function retrieves the Slurm partition configuration and returns the
    name of the partition that is marked as the default.

    Args:
        None

    Returns:
        The name of the default Slurm partition.

    Example:
        >>> default_partition()
        'my_default_partition'
    """

    partitions_dict = get_partitions()

    for partition_name in partitions_dict:
        if partitions_dict[partition_name]["Default"] == "YES":
            return partition_name


# This is supplied for ease-of-use in test development only.
# Tests should not use this permanently. Use logging.debug() instead.
def log_debug(msg):
    logging.debug(msg)


##############################################################################
# ATF module initialization
##############################################################################


# This is a logging filter that adds a new LogRecord traceback attribute
class TraceBackFilter(logging.Filter):
    def filter(self, record):
        call_stack = []
        within_atf_context = False

        for frame_summary in (traceback.extract_stack())[-5::-1]:
            if within_atf_context:
                if "testsuite/python" not in frame_summary.filename:
                    break
            else:
                if "testsuite/python" in frame_summary.filename:
                    within_atf_context = True
                else:
                    continue

            function = frame_summary.name
            short_filename = frame_summary.filename.rpartition("testsuite/python/")[2]
            lineno = frame_summary.lineno

            call_stack.append(f"{function}@{short_filename}:{lineno}")

        record.traceback = ",".join(call_stack)

        return True


# Add a new traceback LogRecord attribute
logging.getLogger().addFilter(TraceBackFilter())

# Add a custom TRACE logging level
# This has to be done early enough to allow pytest --log-level=TRACE to be used
logging.TRACE = logging.NOTSET + 5
logging.addLevelName(logging.TRACE, "TRACE")


def _trace(message, *args, **kwargs):
    logging.log(logging.TRACE, message, *args, **kwargs)


logging.trace = _trace
logging.getLogger().trace = _trace

# Add a custom NOTE logging level in between INFO and DEBUG
logging.NOTE = logging.DEBUG + 5
logging.addLevelName(logging.NOTE, "NOTE")


def _note(message, *args, **kwargs):
    logging.log(logging.NOTE, message, *args, **kwargs)


logging.note = _note
logging.getLogger().note = _note

# The module-level temporary directory is initialized in conftest.py
module_tmp_path = None

# Instantiate and populate testrun-level properties
properties = {}

# Initialize directory properties
properties["testsuite_base_dir"] = str(pathlib.Path(__file__).resolve().parents[2])
properties["testsuite_python_lib"] = properties["testsuite_base_dir"] + "/python/lib"
properties["slurm-source-dir"] = str(pathlib.Path(__file__).resolve().parents[3])
properties["slurm-build-dir"] = properties["slurm-source-dir"]
properties["slurm-prefix"] = "/usr/local"
properties["testsuite_scripts_dir"] = (
    properties["testsuite_base_dir"] + "/python/scripts"
)

# Override directory properties with values from testsuite.conf file
testsuite_config = {}
# The default location for the testsuite.conf file (in SRCDIR/testsuite)
# can be overridden with the SLURM_TESTSUITE_CONF environment variable.
testsuite_config_file = os.getenv(
    "SLURM_TESTSUITE_CONF", f"{properties['testsuite_base_dir']}/testsuite.conf"
)
if not os.path.isfile(testsuite_config_file):
    pytest.fail(
        f"The unified testsuite configuration file (testsuite.conf) was not found. This file can be created from a copy of the autogenerated sample found in BUILDDIR/testsuite/testsuite.conf.sample. By default, this file is expected to be found in SRCDIR/testsuite ({properties['testsuite_base_dir']}). If placed elsewhere, set the SLURM_TESTSUITE_CONF environment variable to the full path of your testsuite.conf file."
    )
with open(testsuite_config_file, "r") as f:
    for line in f.readlines():
        if match := re.search(rf"^\s*(\w+)\s*=\s*(.*)$", line):
            testsuite_config[match.group(1).lower()] = match.group(2)
if "slurmsourcedir" in testsuite_config:
    properties["slurm-source-dir"] = testsuite_config["slurmsourcedir"]
if "slurmbuilddir" in testsuite_config:
    properties["slurm-build-dir"] = testsuite_config["slurmbuilddir"]
if "slurminstalldir" in testsuite_config:
    properties["slurm-prefix"] = testsuite_config["slurminstalldir"]
if "slurmconfigdir" in testsuite_config:
    properties["slurm-config-dir"] = testsuite_config["slurmconfigdir"]

# Set derived directory properties
# The environment (e.g. PATH, SLURM_CONF) overrides the configuration.
# If the Slurm clients and daemons are not in the current PATH
# but can be found using the configured SlurmInstallDir, add the
# derived bin and sbin dir to the current PATH.
properties["slurm-bin-dir"] = f"{properties['slurm-prefix']}/bin"
if squeue_path := shutil.which("squeue"):
    properties["slurm-bin-dir"] = os.path.dirname(squeue_path)
elif os.access(f"{properties['slurm-bin-dir']}/squeue", os.X_OK):
    os.environ["PATH"] += ":" + properties["slurm-bin-dir"]
properties["slurm-sbin-dir"] = f"{properties['slurm-prefix']}/sbin"
if slurmctld_path := shutil.which("slurmctld"):
    properties["slurm-sbin-dir"] = os.path.dirname(slurmctld_path)
elif os.access(f"{properties['slurm-sbin-dir']}/slurmctld", os.X_OK):
    os.environ["PATH"] += ":" + properties["slurm-sbin-dir"]
properties["slurm-config-dir"] = re.sub(
    r"\${prefix}", properties["slurm-prefix"], properties["slurm-config-dir"]
)
if slurm_conf_path := os.getenv("SLURM_CONF"):
    properties["slurm-config-dir"] = os.path.dirname(slurm_conf_path)

# Derive the slurm-user value
properties["slurm-user"] = "root"
slurm_config_file = f"{properties['slurm-config-dir']}/slurm.conf"
if not os.path.isfile(slurm_config_file):
    pytest.fail(
        f"The python testsuite was expecting your slurm.conf to be found in {properties['slurm-config-dir']}. Please create it or use the SLURM_CONF environment variable to indicate its location."
    )
if os.access(slurm_config_file, os.R_OK):
    with open(slurm_config_file, "r") as f:
        for line in f.readlines():
            if match := re.search(rf"^\s*(?i:SlurmUser)\s*=\s*(.*)$", line):
                properties["slurm-user"] = match.group(1)
else:
    # slurm.conf is not readable as test-user. We will try reading it as root
    results = run_command(
        f"grep -i SlurmUser {slurm_config_file}", user="root", quiet=True
    )
    if results["exit_code"] == 0:
        pytest.fail(f"Unable to read {slurm_config_file}")
    for line in results["stdout"].splitlines():
        if match := re.search(rf"^\s*(?i:SlurmUser)\s*=\s*(.*)$", line):
            properties["slurm-user"] = match.group(1)

properties["submitted-jobs"] = []
properties["test-user"] = pwd.getpwuid(os.getuid()).pw_name
properties["auto-config"] = False

# Instantiate a nodes dictionary. These are populated in require_slurm_running.
nodes = {}

# Check if user has sudo privileges
results = subprocess.run(
    "sudo -ln | grep -q '(ALL.*) NOPASSWD: ALL'",
    shell=True,
    capture_output=True,
    text=True,
)
if results.returncode == 0:
    properties["sudo-rights"] = True
else:
    properties["sudo-rights"] = False
