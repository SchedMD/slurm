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
import subprocess
import sys
import time
import traceback


##############################################################################
# ATF module functions
##############################################################################

def log_die(message):
    """Logs a critical message and exits with a non-zero exit code.

    While assert should be used to verify expected test conditions in tests,
    this function may be used to invoke a failure for failed setup conditions
    and operations in tests, library functions and scripts.
    """

    logging.critical(message)
    sys.exit(1)


def node_range_to_list(node_expression):
    """Converts a node range expression into a list of node names.

    Example:
        >>> node_range_to_list('node[1,3-5]')
        ['node1', 'node3', 'node4', 'node5']
    """

    node_list = []
    output = run_command_output(f"scontrol show hostnames {node_expression}", fatal=True, quiet=True)
    for line in output.rstrip().splitlines():
        node_list.append(line)
    return node_list


def node_list_to_range(node_list):
    """Converts a list of node names to a node range expression.

    Example:
        >>> node_list_to_range(['node1', 'node3', 'node4', 'node5'])
        'node[1,3-5]'
    """

    return run_command_output(f"scontrol show hostlistsorted {','.join(node_list)}", fatal=True, quiet=True).rstrip()


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
    return re.sub(r'^\[(.*)\]$', r'\1', node_range_expression)


def run_command(command, fatal=False, timeout=60, quiet=False, chdir=None, user=None, input=None, xfail=False):
    """Executes a command and returns a dictionary result.

    Args:
        command (string): The command to execute. The command is run within a
            bash subshell, so pipes, redirection, etc. are performed.
        fatal (boolean): If True, a non-zero exit code (or zero if combined
            with xfail) will result in the test failing.
        timeout (integer): If the command does not exit before timeout number
            of seconds, this function will return with an exit code of 110.
        chdir (directory): Change to the specified directory before executing
            the command.
        user (user name): Run the command as the specified user. This requires
            the invoking user to have unprompted sudo rights.
        input (string): The specified input is supplied to the command as stdin.
        xfail (boolean): If True, the command is expected to fail.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        A dictionary containing the following keys:
            start_time: epoch start time
            duration: number of seconds the command ran for
            exit_code: exit code for the command
            stdout: command stdout as a string
            stderr: command stderr as a string
    """

    additional_run_kwargs = {}
    if chdir is not None:
        additional_run_kwargs['cwd'] = chdir
    if input is not None:
        additional_run_kwargs['input'] = input
    if timeout is not None:
        additional_run_kwargs['timeout'] = timeout

    if quiet:
        log_command_level = logging.TRACE
        log_details_level = logging.TRACE
    else:
        log_command_level = logging.NOTE
        log_details_level = logging.DEBUG

    start_time = time.time()
    invocation_message = "Running command"
    if user is not None:
        invocation_message += f" as user {user}"
    invocation_message += f": {command}"
    logging.log(log_command_level, invocation_message)
    try:
        if user is not None and user != properties['test-user']:
            if not properties['sudo-rights']:
                pytest.skip("This test requires the test user to have unprompted sudo rights", allow_module_level=True)
            cp = subprocess.run(['sudo', '-nu', user, '/bin/bash', '-lc', command], capture_output=True, text=True, **additional_run_kwargs)
        else:
            cp = subprocess.run(command, shell=True, executable='/bin/bash', capture_output=True, text=True, **additional_run_kwargs)
        end_time = time.time()
        duration = end_time - start_time
        exit_code = cp.returncode
        stdout = cp.stdout
        stderr = cp.stderr
    except subprocess.TimeoutExpired as e:
        duration = e.timeout
        exit_code = errno.ETIMEDOUT
        stdout = e.stdout if e.stdout else ''
        stderr = e.stderr if e.stderr else ''

    if input is not None:
        logging.log(log_details_level, f"Command input: {input}")
    logging.log(log_details_level, f"Command exit code: {exit_code}")
    logging.log(log_details_level, f"Command stdout: {stdout}")
    logging.log(log_details_level, f"Command stderr: {stderr}")
    logging.log(log_details_level, "Command duration: %.03f seconds", duration)

    message = ''
    if exit_code == errno.ETIMEDOUT:
        message = f"Command \"{command}\" timed out after {duration} seconds"
    elif (exit_code != 0 and not xfail):
        message = f"Command \"{command}\" failed with rc={exit_code}"
    elif (exit_code == 0 and xfail):
        message = f"Command \"{command}\" was expected to fail but succeeded"
    if (exit_code != 0 and not xfail) or (exit_code == 0 and xfail):
        if stderr != '' or stdout != '':
            message += ':'
        if stderr !=  '':
            message += f" {stderr}"
        if stdout !=  '':
            message += f" {stdout}"

    if message != '':
        if fatal:
            log_die(message)
        elif not quiet:
            logging.warning(message)

    results = {}
    results['command'] = command
    results['start_time'] = float(int(start_time * 1000)) / 1000
    results['duration'] = float(int(duration * 1000)) / 1000
    results['exit_code'] = exit_code
    results['stdout'] = stdout
    results['stderr'] = stderr

    return results


def run_command_error(command, **run_command_kwargs):
    """Executes a command and returns the standard error.

    This function accepts the same arguments as run_command.
    """

    results = run_command(command, **run_command_kwargs)

    return results['stderr']


def run_command_output(command, **run_command_kwargs):
    """Executes a command and returns the standard output.

    This function accepts the same arguments as run_command.
    """

    results = run_command(command, **run_command_kwargs)

    return results['stdout']


def run_command_exit(command, **run_command_kwargs):
    """Executes a command and returns the exit code.

    This function accepts the same arguments as run_command.
    """

    results = run_command(command, **run_command_kwargs)

    return results['exit_code']


def repeat_until(callable, condition, timeout=5, poll_interval=None, fatal=False):
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
        fatal (boolean): If True, a timeout will result in the test failing.

    Example:
        >>> repeat_until(lambda : random.randint(1,10), lambda n: n == 5, timeout=10, poll_interval=1)
        True
    """

    begin_time = time.time()

    if poll_interval is None:
        if timeout <= 5:
            poll_interval = .1
        elif timeout <= 10:
            poll_interval = .2
        else:
            poll_interval = 1

    while time.time() < begin_time + timeout:
        if condition(callable()):
            return True
        time.sleep(poll_interval)

    if fatal:
        log_die(f"Condition was not met within the {timeout} second timeout")
    else:
        return False


def repeat_command_until(command, condition, quiet=True, **repeat_until_kwargs):
    """Repeats a command until a condition is met or it times out.

    This function accepts the same arguments as repeat_until.

    Additional Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Example:
        >>> repeat_command_until("scontrol ping", lambda results: re.search(r'is UP', results['stdout']))
        True
    """

    return repeat_until(lambda : run_command(command, quiet=quiet), condition, **repeat_until_kwargs)


def pids_from_exe(executable):
    # We have to elevate privileges here, but forking off thousands of sudo
    # commands is expensive, so we will sudo a dynamic bash script for speed
    script=f"""cd /proc
for pid in `ls -d1 [0-9]*`;
do if [[ "$(readlink $pid/exe)" = "{executable}" ]];
   then echo $pid;
   fi
done"""
    pids = []
    output = run_command_output(script, user='root', quiet=True)
    for line in output.rstrip().splitlines():
        pids.append(int(line))
    return pids


def is_slurmctld_running(quiet=False):
    """Checks whether slurmctld is running.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns:
        True if the slurmctld is running, False otherwise.
    """

    # Check whether slurmctld is running
    if re.search(r'is UP', run_command_output("scontrol ping", quiet=quiet)):
        return True

    return False


def start_slurmctld(clean=False, quiet=False):
    """Starts the slurmctld daemon.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    if not properties['auto-config']:
        require_auto_config("wants to start slurmctld")

    if not is_slurmctld_running(quiet=quiet):

        # Start slurmctld
        command = f"{properties['slurm-sbin-dir']}/slurmctld"
        if clean:
            command += " -c -i"
        results = run_command(command, user=properties['slurm-user'], quiet=quiet)
        if results['exit_code'] != 0:
            log_die(f"Unable to start slurmctld (rc={results['exit_code']}): {results['stderr']}")

        # Verify that slurmctld is running
        if not repeat_command_until("scontrol ping", lambda results: re.search(r'is UP', results['stdout'])):
            log_die(f"Slurmctld is not running")


def start_slurm(clean=False, quiet=False):
    """Starts all applicable slurm daemons.

    This function examines the slurm configuration files in order to
    determine which daemons need to be started.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    if not properties['auto-config']:
        require_auto_config("wants to start slurm")

    # Determine whether slurmdbd should be included
    if get_config_parameter('AccountingStorageType', live=False, quiet=quiet) == 'accounting_storage/slurmdbd':

        if run_command_exit("sacctmgr show cluster", user=properties['slurm-user'], quiet=quiet) != 0:

            # Start slurmdbd
            results = run_command(f"{properties['slurm-sbin-dir']}/slurmdbd", user=properties['slurm-user'], quiet=quiet)
            if results['exit_code'] != 0:
                log_die(f"Unable to start slurmdbd (rc={results['exit_code']}): {results['stderr']}")

            # Verify that slurmdbd is running
            if not repeat_command_until("sacctmgr show cluster", lambda results: results['exit_code'] == 0):
                log_die(f"Slurmdbd is not running")

    # Start slurmctld
    start_slurmctld(clean, quiet)

    # Build list of slurmds
    slurmd_list = []
    output = run_command_output(f"perl -nle 'print $1 if /^NodeName=(\\S+)/' {properties['slurm-config-dir']}/slurm.conf", user=properties['slurm-user'], quiet=quiet)
    if not output:
        log_die("Unable to determine the slurmd node names")
    for node_name_expression in output.rstrip().split('\n'):
        if node_name_expression != 'DEFAULT':
            slurmd_list.extend(node_range_to_list(node_name_expression))

    # (Multi)Slurmds
    for slurmd_name in slurmd_list:

        # Check whether slurmd is running
        if run_command_exit(f"pgrep -f 'slurmd -N {slurmd_name}'", quiet=quiet) != 0:

            # Start slurmd
            results = run_command(f"{properties['slurm-sbin-dir']}/slurmd -N {slurmd_name}", user='root', quiet=quiet)
            if results['exit_code'] != 0:
                log_die(f"Unable to start slurmd -N {slurmd_name} (rc={results['exit_code']}): {results['stderr']}")

            # Verify that the slurmd is running
            if run_command_exit(f"pgrep -f 'slurmd -N {slurmd_name}'", quiet=quiet) != 0:
                log_die(f"Slurmd -N {slurmd_name} is not running")


def stop_slurmctld(quiet=False):
    """Stops the slurmctld daemon.

    This function may only be used in auto-config mode.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    if not properties['auto-config']:
        require_auto_config("wants to stop slurmctld")

    # Stop slurmctld
    run_command("scontrol shutdown slurmctld", user=properties['slurm-user'], quiet=quiet)

    # Verify that slurmctld is not running
    if not repeat_until(lambda : pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmctld"), lambda pids: len(pids) == 0):
        log_die("Slurmctld is still running")


def stop_slurm(quiet=False):
    """Stops all applicable slurm daemons.

    This function examines the slurm configuration files in order to
    determine which daemons need to be stopped.

    This function may only be used in auto-config mode.

    Args:
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    if not properties['auto-config']:
        require_auto_config("wants to stop slurm")

    # Determine whether slurmdbd should be included
    if get_config_parameter('AccountingStorageType', live=False, quiet=quiet) == 'accounting_storage/slurmdbd':

        # Stop slurmdbd
        run_command("sacctmgr shutdown", user=properties['slurm-user'], quiet=quiet)

        # Verify that slurmdbd is not running (we might have to wait for rollups to complete)
        if not repeat_until(lambda : pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmdbd"), lambda pids: len(pids) == 0, timeout=60):
            log_die("Slurmdbd is still running")

    # Stop slurmctld and slurmds
    run_command("scontrol shutdown", user=properties['slurm-user'], quiet=quiet)

    # Verify that slurmctld is not running
    if not repeat_until(lambda : pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmctld"), lambda pids: len(pids) == 0, timeout=10):
        log_die("Slurmctld is still running")

    # Build list of slurmds
    slurmd_list = []
    output = run_command_output(f"perl -nle 'print $1 if /^NodeName=(\\S+)/' {properties['slurm-config-dir']}/slurm.conf", quiet=quiet)
    if not output:
        log_die("Unable to determine the slurmd node names")
    for node_name_expression in output.rstrip().split('\n'):
        if node_name_expression != 'DEFAULT':
            slurmd_list.extend(node_range_to_list(node_name_expression))

    # Verify that slurmds are not running
    if not repeat_until(lambda : pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmd"), lambda pids: len(pids) == 0, timeout=15):
        pids = pids_from_exe(f"{properties['slurm-sbin-dir']}/slurmd")
        run_command(f"pgrep -f {properties['slurm-sbin-dir']}/slurmd -a", quiet=quiet)
        log_die(f"Some slurmds are still running ({pids})")


def restart_slurmctld(clean=False, quiet=False):
    """Restarts the slurmctld daemons.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    stop_slurmctld(quiet=quiet)
    start_slurmctld(clean=clean, quiet=quiet)


def restart_slurm(clean=False, quiet=False):
    """Restarts all applicable slurm daemons.

    This function may only be used in auto-config mode.

    Args:
        clean (boolean): If True, clears previous slurmctld state.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    stop_slurm(quiet=quiet)
    start_slurm(clean=clean, quiet=quiet)


def require_slurm_running():
    """Ensures that the slurm daemons are running.

    In local-config mode, the test is skipped if slurm is not running.
    In auto-config mode, slurm is started if necessary.
    """

    global nodes

    if properties['auto-config']:
        if not is_slurmctld_running(quiet=True):
            start_slurm(clean=True, quiet=True)
            properties['slurm-started'] = True
    else:
        if not is_slurmctld_running(quiet=True):
            pytest.skip("This test requires slurm to be running", allow_module_level=True)

    # As a side effect, build up initial nodes dictionary
    nodes = get_nodes(quiet=True)


def backup_config_file(config='slurm'):
    """Backs up a configuration file.

    This function may only be used in auto-config mode.

    Args:
        config: Name of config file to back up (without the .conf suffix).
    """

    if not properties['auto-config']:
        require_auto_config(f"wants to modify the {config} configuration file")

    properties['configurations-modified'].add(config)

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"
    backup_config_file = f"{config_file}.orig-atf"

    # If a backup already exists, issue a warning and return (honor existing backup)
    if os.path.isfile(backup_config_file):
        logging.warning(f"Backup file already exists ({backup_config_file})")
        return

    # If the file to backup does not exist, touch an empty backup file with
    # the sticky bit set. restore_config_file will remove the file.
    if not os.path.isfile(config_file):
        run_command(f"touch {backup_config_file}", user=properties['slurm-user'], fatal=True, quiet=True)
        run_command(f"chmod 1000 {backup_config_file}", user=properties['slurm-user'], fatal=True, quiet=True)

    # Otherwise, copy the config file to the backup
    else:
        run_command(f"cp {config_file} {backup_config_file}", user=properties['slurm-user'], fatal=True, quiet=True)


def restore_config_file(config='slurm'):
    """Restores a configuration file.

    This function may only be used in auto-config mode.

    Args:
        config: Name of config file to back up (without the .conf suffix).
    """

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"
    backup_config_file = f"{config_file}.orig-atf"

    properties['configurations-modified'].remove(config)

    # If backup file doesn't exist, it has probably already been
    # restored by a previous call to restore_config_file
    if not os.path.isfile(backup_config_file):
        logging.warning(f"Backup file does not exist for {config_file}. It has probably already been restored.")
        return

    # If the sticky bit is set and the file is empty, remove both the file and the backup
    backup_stat = os.stat(backup_config_file)
    if backup_stat.st_size == 0 and backup_stat.st_mode & stat.S_ISVTX:
        run_command(f"rm -f {backup_config_file}", user=properties['slurm-user'], fatal=True, quiet=True)
        if os.path.isfile(config_file):
            run_command(f"rm -f {config_file}", user=properties['slurm-user'], fatal=True, quiet=True)

    # Otherwise, copy backup config file to primary config file
    # and remove the backup (.orig-atf)
    else:
        run_command(f"cp {backup_config_file} {config_file}", user=properties['slurm-user'], fatal=True, quiet=True)
        run_command(f"rm -f {backup_config_file}", user=properties['slurm-user'], fatal=True, quiet=True)


def get_config(live=True, source='slurm', quiet=False):
    """Returns the slurm configuration as a dictionary.

    Args:
        live (boolean):
            If True, the configuration information is obtained via
            a query to the relevant slurm daemon (e.g. scontrol show config).
            If False, the configuration information is obtained by directly
            parsing the relevant slurm configuration file (e.g. slurm.conf).
        source (string):
            If live is True, source should be either scontrol or sacctmgr.
            If live is False, source should be the name of the config file
            without the .conf prefix (e.g. slurmdbd).
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns: A dictionary comprised of the parameter names and their values.
        For parameters that can have multiple lines and subparameters,
        the dictionary value will be a dictionary of dictionaries.
    """

    slurm_dict = {}

    if live:
        if source == 'slurm' or source == 'controller' or source == 'scontrol':
            command = 'scontrol'
        elif source == 'slurmdbd' or source == 'dbd' or source == 'sacctmgr':
            command = 'sacctmgr'
        else:
            log_die(f"Invalid live source value ({source})")

        output = run_command_output(f"{command} show config", fatal=True, quiet=quiet)

        for line in output.splitlines():
            if match := re.search(rf"^\s*(\S+)\s*=\s*(.*)$", line):
                slurm_dict[match.group(1)] = match.group(2).rstrip()
    else:
        config = source
        config_file = f"{properties['slurm-config-dir']}/{config}.conf"

        # We might be looking for parameters in a config file that has not
        # been created yet. If so, we just want this to return an empty dict
        output = run_command_output(f"cat {config_file}", user=properties['slurm-user'], quiet=quiet)
        for line in output.splitlines():
            if match := re.search(rf"^\s*(\S+)\s*=\s*(.*)$", line):
                parameter_name, parameter_value = match.group(1), match.group(2).rstrip()
                if parameter_name.lower() in ['downnodes', 'frontendname', 'name', 'nodename', 'nodeset', 'partitionname', 'switchname']:
                    instance_name, subparameters = parameter_value.split(' ', 1)
                    subparameters_dict = {}
                    for (subparameter_name, subparameter_value) in re.findall(r' *([^= ]+) *= *([^ ]+)', subparameters):
                        # Reformat the value if necessary
                        if is_integer(subparameter_value):
                            subparameter_value = int(subparameter_value)
                        elif is_float(subparameter_value):
                            subparameter_value = float(subparameter_value)
                        elif subparameter_value == '(null)':
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
                    elif parameter_value == '(null)':
                        parameter_value = None
                    slurm_dict[parameter_name] = parameter_value

    return slurm_dict


def get_config_parameter(name, default=None, **get_config_kwargs):
    """Obtains the value for a slurm configuration parameter.

    This function accepts the same arguments as get_config.

    Additional Args:
        name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.

    Returns: The value of the specified parameter, or the default if not found.
    """

    config_dict = get_config(**get_config_kwargs)

    # Convert keys to lower case so we can do a case-insensitive search
    lower_dict = dict((key.lower(), value) for key, value in config_dict.items())

    if name.lower() in lower_dict:
        return lower_dict[name.lower()]
    else:
        return default


def config_parameter_includes(name, value, **get_config_kwargs):
    """Checks whether a configuration parameter includes a specific value.

    When a parameter may contain a comma-separated list of values, this
    function can be used to determine whether a specific value is within
    the list.

    This function accepts the same arguments as get_config.

    Additional Args:
        name (string): The parameter name.
        value (string): The value you are looking for.

    Returns: True if the specified string value is found within the parameter
        value list, False otherwise.

    Example:
        >>> config_parameter_includes('SlurmdParameters', 'config_overrides')
        False
    """

    config_dict = get_config(**get_config_kwargs)

    # Convert keys to lower case so we can do a case-insensitive search
    lower_dict = dict((key.lower(), value) for key, value in config_dict.items())

    if name.lower() in lower_dict and value.lower() in map(str.lower, lower_dict[name.lower()].split(',')):
        return True
    else:
        return False


def set_config_parameter(parameter_name, parameter_value, source='slurm', restart=False):
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
 
    Example:
        >>> set_config_parameter('ClusterName', 'cluster1')
    """

    if not properties['auto-config']:
        require_auto_config("wants to modify parameters")

    if source == 'dbd':
        config = 'slurmdbd'
    else:
        config = source

    config_file = f"{properties['slurm-config-dir']}/{config}.conf"

    # This has the side-effect of adding config to configurations-modified
    backup_config_file(config)

    # Remove all matching parameters and append the new parameter
    lines = []
    output = run_command_output(f"cat {config_file}", user=properties['slurm-user'], quiet=True)
    for line in output.splitlines():
        if not re.search(rf"(?i)^\s*{parameter_name}\s*=", line):
            lines.append(f"{line}\n")
    if isinstance(parameter_value, dict):
        for instance_name in parameter_value:
            line = f"{parameter_name}={instance_name}"
            for subparameter_name, subparameter_value in parameter_value[instance_name].items():
                line += f" {subparameter_name}={subparameter_value}"
            lines.append(f"{line}\n")
    elif parameter_value != None:
        lines.append(f"{parameter_name}={parameter_value}\n")
    input = ''.join(lines)
    run_command(f"cat > {config_file}", input=input, user=properties['slurm-user'], fatal=True, quiet=True)

    slurmctld_running = is_slurmctld_running(quiet=True)

    # Remove clustername state file if we aim to change the cluster name
    if parameter_name.lower() == "clustername":
        state_save_location = get_config_parameter('StateSaveLocation', live=slurmctld_running, quiet=True)
        run_command(f"rm -f {state_save_location}/clustername", user=properties['slurm-user'], quiet=True)

    # Reconfigure (or restart) slurm controller if it is already running
    if slurmctld_running:
        if source != 'slurm' or parameter_name.lower() in [
            'accountingstoragetype',
            'rebootprogram',
        ]:
            restart_slurm(quiet=True)
        elif restart or parameter_name.lower() in [
            'authtype',
            'controlmach',
            'plugindir',
            'statesavelocation',
            'slurmctldhost',
            'slurmctldport',
            'slurmdport',
        ]:
            restart_slurmctld(quiet=True)
        else:
            run_command("scontrol reconfigure", user=properties['slurm-user'], quiet=True)


def add_config_parameter_value(name, value, source='slurm'):
    """Appends a value to configuration parameter list.

    When a parameter may contain a comma-separated list of values, this
    function can be used to add a value to the list.

    This function may only be used in auto-config mode.

    Args:
        name (string): The parameter name.
        value (string): The value to add.
        source (string): Name of the config file without the .conf prefix.

    Example:
        >>> add_config_parameter_value('SlurmdParameters', 'config_overrides')
    """

    if config_parameter_includes(name, value, live=False, quiet=True, source=source):
        return

    original_value_string = get_config_parameter(name, live=False, quiet=True, source=source)
    if original_value_string is None:
        set_config_parameter(name, value, source=source)
    else:
        value_list = original_value_string.split(',')
        value_list.append(value)
        set_config_parameter(name, ','.join(value_list), source=source)


def remove_config_parameter_value(name, value, source='slurm'):
    """Removes a value from a configuration parameter list.

    When a parameter may contain a comma-separated list of values, this
    function can be used to remove a value from the list.

    This function may only be used in auto-config mode.

    Args:
        name (string): The parameter name.
        value (string): The value to remove.
        source (string): Name of the config file without the .conf prefix.

    Example:
        >>> remove_config_parameter_value('SlurmdParameters', 'config_overrides')
    """
    if not config_parameter_includes(name, value, live=False, quiet=True, source=source):
        return

    value_list = get_config_parameter(name, live=False, quiet=True, source=source).split(',')
    value_list.remove(value)
    if value_list:
        set_config_parameter(name, ','.join(value_list), source=source)
    else:
        set_config_parameter(name, None, source=source)


def require_config_parameter(parameter_name, parameter_value, condition=None, source='slurm', skip_message=None):
    """Ensures that a configuration parameter has the required value.

    In local-config mode, the test is skipped if the required configuration is not set.
    In auto-config mode, sets the required configuration value if necessary.

    Args:
        parameter_name (string): The parameter name.
        parameter_value (string): The target parameter value.
        source (string): Name of the config file without the .conf prefix.
        condition (callable): If there is a range of acceptable values, a
            condition can be specified to test whether the current parameter
            value is sufficient. If not, the target parameter_value will be
            used (or the test will be skipped in the case of local-config mode).

    Note:
        When requiring a complex parameter (one which may be repeated and has
        its own subparameters, such as with nodes, partitions and gres),
        the parameter_value should be a dictionary of dictionaries.
 
    Examples:
        >>> require_config_parameter('SelectType', 'select/cons_tres')
        >>> require_config_parameter('SlurmdTimeout', 5, lambda v: v <= 5)
        >>> require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}, 'mps': {'Count': 100}}, source='gres')
    """

    observed_value = get_config_parameter(parameter_name, live=False, source=source, quiet=True)
    condition_satisfied = False
    if condition is None:
        condition = lambda observed, desired: observed == desired
        if observed_value == parameter_value:
            condition_satisfied = True
    else:
        if condition(observed_value):
            condition_satisfied = True

    if not condition_satisfied:
        if properties['auto-config']:
            set_config_parameter(parameter_name, parameter_value, source=source)
        else:
            if skip_message is None:
                skip_message = f"This test requires the {parameter_name} parameter to be {parameter_value} (but it is {observed_value})"
            pytest.skip(skip_message, allow_module_level=True)


def require_config_parameter_includes(name, value, source='slurm'):
    """Ensures that a configuration parameter list contains the required value.

    In local-config mode, the test is skipped if the configuration parameter
    list does not include the required value.
    In auto-config mode, adds the required value to the configuration parameter
    list if necessary.

    Args:
        name (string): The parameter name.
        value (string): The value we want to be in the list.
        source (string): Name of the config file without the .conf prefix.

    Example:
        >>> require_config_parameter_includes('SlurmdParameters', 'config_overrides')
    """

    if properties['auto-config']:
        add_config_parameter_value(name, value, source=source)
    else:
        if not config_parameter_includes(name, value, source=source, live=False, quiet=True):
            pytest.skip(f"This test requires the {name} parameter to include {value}", allow_module_level=True)


def require_config_parameter_excludes(name, value, source='slurm'):
    """Ensures that a configuration parameter list does not contain a value.

    In local-config mode, the test is skipped if the configuration parameter
  includes the specified value.
    In auto-config mode, removes the specified value from the configuration
    parameter list if necessary.

    Args:
        name (string): The parameter name.
        value (string): The value we do not want to be in the list.
        source (string): Name of the config file without the .conf prefix.

    Example:
        >>> require_config_parameter_excludes('SlurmdParameters', 'config_overrides')
    """
    if properties['auto-config']:
        remove_config_parameter_value(name, value, source=source)
    else:
        if config_parameter_includes(name, value, source=source, live=False, quiet=True):
            pytest.skip(f"This test requires the {name} parameter to exclude {value}", allow_module_level=True)


def require_tty(number):

    tty_file = f"/dev/tty{number}"
    if not os.path.exists(tty_file):
        run_command(f"mknod -m 666 {tty_file} c 4 {number}", user='root', fatal=True, quiet=True)


## Use this to create an entry in gres.conf and create an associated tty
#def require_gres_device(name):
#
#    gres_value = get_config_parameter('Name', live=False, source='gres', quiet=True)
#    if gres_value is None or name not in gres_value:
#        if not properties['auto-config']:
#            pytest.skip(f"This test requires a '{name}' gres device to be defined in gres.conf", allow_module_level=True)
#        else:
#            require_tty(0)
#            require_config_parameter('Name', {name: {'File': '/dev/tty0'}}, source='gres')


def require_auto_config(reason=''):
    """Ensures that auto-config mode is being used.

    This function skips the test if auto-config mode is not enabled.

    Args:
        reason (string): Augments the skip reason with a context-specific
            explanation for why the auto-config mode is needed by the test.

    Example:
        >>> require_auto_config("wants to set the Epilog")
    """

    if not properties['auto-config']:
        message = "This test requires auto-config to be enabled"
        if reason != '':
            message += f" ({reason})"
        pytest.skip(message, allow_module_level=True)


def require_accounting(modify=False):
    """Ensures that slurm accounting is configured.

    In local-config mode, the test is skipped if slurm accounting is not
    configured.
    In auto-config mode, configures slurm accounting if necessary.

    Args:
        modify (boolean): If True, this indicates to the ATF that the test
            will modify the accounting database (e.g. adding accounts, etc).
            A database backup is automatically created and the original dump
            is restored after the test completes.
    """

    if properties['auto-config']:
        if get_config_parameter("AccountingStorageType", live=False, quiet=True) != "accounting_storage/slurmdbd":
            set_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
        if modify:
            backup_accounting_database()
    else:
        if modify:
            require_auto_config("wants to modify the accounting database")
        elif get_config_parameter("AccountingStorageType", live=False, quiet=True) != "accounting_storage/slurmdbd":
            pytest.skip("This test requires accounting to be configured", allow_module_level=True)


def get_user_name():
    return pwd.getpwuid(os.getuid()).pw_name


def cancel_jobs(job_list, timeout=5, poll_interval=.1, fatal=False, quiet=False):
    """Cancels a list of jobs and waits for them to complete.

    Args:
        job_list (list): A list of job ids to cancel.
        timeout (integer): Number of seconds to wait for jobs to be done before
            timing out.
        poll_interval (float): Number of seconds to wait between job state
            polls.
        fatal (boolean): If True, a timeout will result in the test failing.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    job_list_string = ' '.join(str(i) for i in job_list)

    if job_list_string == '':
        return True

    run_command(f"scancel {job_list_string}", fatal=fatal, quiet=quiet)

    for job_id in job_list:
        status = wait_for_job_state(job_id, 'DONE', timeout=timeout, poll_interval=poll_interval, fatal=fatal, quiet=quiet)
        if not status:
            if fatal:
                log_die(f"Job ({job_id}) was not cancelled within the {timeout} second timeout")
            return status

    return True


def cancel_all_jobs(fatal=True, timeout=5, quiet=False):
    """Cancels all jobs belonging to the test user.

    Args:
        fatal (boolean): If True, a timeout will result in the test failing.
        timeout (integer): If timeout number of seconds expires before the
            jobs are verified to be cancelled, fail.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    user_name = get_user_name()

    results = run_command(f"scancel -u {user_name}", quiet=quiet)
    # Have to account for het scancel bug until bug 11806 is fixed
    if results['exit_code'] != 0 and results['exit_code'] != 60:
        log_die(f"Failure cancelling jobs: {results['stderr']}")

    # Currently inherits 5 second timeout from repeat_until
    return repeat_command_until(f"squeue -u {user_name} --noheader", lambda results: results['stdout'] == '', fatal=fatal, timeout=timeout, quiet=quiet)


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
            directly parsing the slurm configuration file (slurm.conf).
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns: A dictionary of dictionaries where the first level keys are the
        node names and with the their values being a dictionary of
        configuration parameters for the respective node.
    """

    nodes_dict = {}

    if live:
        output = run_command_output("scontrol show nodes -o", fatal=True, quiet=quiet, **run_command_kwargs)

        node_dict = {}
        for line in output.splitlines():
            if line == '':
                continue

            while match := re.search(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', line):
                parameter_name, parameter_value = match.group(1), match.group(2)

                # Remove the consumed parameter from the line
                line = re.sub(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', '', line)

                # Reformat the value if necessary
                if is_integer(parameter_value):
                    parameter_value = int(parameter_value)
                elif is_float(parameter_value):
                    parameter_value = float(parameter_value)
                elif parameter_value == '(null)':
                    parameter_value = None

                # Add it to the temporary node dictionary
                node_dict[parameter_name] = parameter_value

            # Add the node dictionary to the nodes dictionary
            nodes_dict[node_dict['NodeName']] = node_dict

            # Clear the node dictionary for use by the next node
            node_dict = {}

    else:
        # Get the config dictionary
        config_dict = get_config(live=False, quiet=quiet)

        # Convert keys to lower case so we can do a case-insensitive search
        lower_config_dict = dict((key.lower(), value) for key, value in config_dict.items())

        # DEFAULT will be included seperately
        if 'nodename' in lower_config_dict:
            for node_expression, node_expression_dict in lower_config_dict['nodename'].items():
                port_expression = node_expression_dict['Port'] if 'Port' in node_expression_dict else ''

                # Break up the node expression and port expression into lists
                node_list = node_range_to_list(node_expression)
                port_list = range_to_list(port_expression)

                # Iterate over the nodes in the expression
                for node_index in range(len(node_list)):
                    node_name = node_list[node_index]
                    # Add the parameters to the temporary node dictionary
                    node_dict = dict(node_expression_dict)
                    node_dict['NodeName'] = node_name
                    if node_index < len(port_list):
                        node_dict['Port'] = int(port_list[node_index])
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
            directly parsing the slurm configuration file (slurm.conf).

    Returns: The value of the specified node parameter, or the default if not
        found.
    """

    nodes_dict = get_nodes(live=live)

    if node_name in nodes_dict:
        node_dict = nodes_dict[node_name]
    else:
        log_die(f"Node ({node_name}) was not found in the node configuration")

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

    Example:
        >>> set_node_parameter('node1', 'Features', 'f1')
    """

    if not properties['auto-config']:
        require_auto_config("wants to modify node parameters")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(f"cat {config_file}", user=properties['slurm-user'], quiet=True)
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the node among the various NodeName definitions
    found_node_name = False
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        words = re.split(r' +', line.strip())
        if len(words) < 1:
            continue
        parameter_name, parameter_value = words[0].split('=', 1)
        if parameter_name.lower() != 'nodename':
            continue

        # We found a NodeName line. Read in the node parameters
        node_expression = parameter_value
        port_expression = ''
        original_node_parameters = collections.OrderedDict()
        for word in words[1:]:
            parameter_name, parameter_value = word.split('=', 1)
            if parameter_name.lower() == 'port':
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
                    port_list.pop(node_index, None)
                if port_list:
                    remainder_port_expression = list_to_range(port_list)
                    remainder_node_line += f" Port={remainder_port_expression}"
                for parameter_name, parameter_value in original_node_parameters.items():
                    remainder_node_line += f" {parameter_name}={parameter_value}"
                new_config_lines.insert(line_index, remainder_node_line)

            break

    if not found_node_name:
        log_die(f"Invalid node specified in set_node_parameter(). Node ({node_name}) does not exist")

    # Write the config file back out with the modifications
    backup_config_file('slurm')
    new_config_string = "\n".join(new_config_lines)
    run_command(f"echo '{new_config_string}' > {config_file}", user = properties['slurm-user'], fatal=True, quiet=True)

    # Restart slurm controller if it is already running
    if is_slurmctld_running(quiet=True):
        restart_slurm(quiet=True)


def is_super_user():
    uid = os.getuid()

    if uid == 0:
        return True

    user = pwd.getpwuid(uid)[0]
    if get_config_parameter('SlurmUser') == user:
        return True

    return False


def require_sudo_rights():
    if not properties['sudo-rights']:
        pytest.skip("This test requires the test user to have unprompted sudo privileges", allow_module_level=True)


def submit_job(sbatch_args="--wrap \"sleep 60\"", **run_command_kwargs):
    """Submits a job using sbatch and returns the job id.

    The submitted job will automatically be cancelled when the test ends.

    Args*:
        sbatch_args (string): The arguments to sbatch.

    * run_command arguments are also accepted (e.g. fatal) and will be supplied
        to the underlying run_command call.

    Returns: The job id
    """

    output = run_command_output(f"sbatch {sbatch_args}", **run_command_kwargs)

    if match := re.search(r'Submitted \S+ job (\d+)', output):
        if not properties['jobs-submitted']:
            properties['jobs-submitted'] = True
        job_id = int(match.group(1))
        return job_id
    else:
        return 0


# Returns results
def run_job(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the run_command results dictionary.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The srun run_command results dictionary.
    """

    results = run_command(f"srun {srun_args}", **run_command_kwargs)

    if results['exit_code'] == errno.ETIMEDOUT:
        if not properties['jobs-submitted']:
            properties['jobs-submitted'] = True

    return results


# Return exit code
def run_job_exit(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the exit code.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The exit code from srun.
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results['exit_code']


# Return output
def run_job_output(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the standard output.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The standard output from srun.
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results['stdout']


# Return error
def run_job_error(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the standard error.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The standard error from srun.
    """

    results = run_job(srun_args, **run_command_kwargs)

    return results['stderr']


# Return job id
def run_job_id(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the job id.

    This function obtains the job id by adding the -v option to srun
    and parsing out the job id.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The job id from srun.
    """

    results = run_job(" ".join(['-v', srun_args]), **run_command_kwargs)

    if match := re.search(r"jobid (\d+)", results['stderr']):
        return int(match.group(1))


# Return job id (command should not be interactive/shell)
def alloc_job_id(salloc_args, **run_command_kwargs):
    """Submits a job using salloc and returns the job id.

    The submitted job will automatically be cancelled when the test ends.

    Args*:
        salloc_args (string): The arguments to salloc.

    * run_command arguments are also accepted (e.g. fatal) and will be supplied
        to the underlying run_command call.

    Returns: The job id
    """

    results = run_command(f"salloc {salloc_args}", **run_command_kwargs)
    if match := re.search(r'Granted job allocation (\d+)', results['stderr']):
        if not properties['jobs-submitted']:
            properties['jobs-submitted'] = True
        job_id = int(match.group(1))
        return job_id
    else:
        return 0


def run_job_nodes(srun_args, **run_command_kwargs):
    """Runs a job using srun and returns the allocated node list.

    This function obtains the job id by adding the -v option to srun
    and parsing out the allocated node list.

    If the srun command times out, it will automatically be cancelled when the
    test ends.

    Args*:
        srun_args (string): The arguments to srun.

    * run_command arguments are also accepted (e.g. timeout) and will be
        supplied to the underlying run_command call.

    Returns: The allocated node list for the job.
    """

    results = run_command(f"srun -v {srun_args}", **run_command_kwargs)
    node_list = []
    if results['exit_code'] == 0:
        if match := re.search(r"jobid \d+: nodes\(\d+\):`([^']+)'", results['stderr']):
            node_list = node_range_to_list(match.group(1))
    return node_list


def get_jobs(job_id=None, **run_command_kwargs):
    """Returns the job configuration as a dictionary of dictionaries.

    Args*:

    * run_command arguments are also accepted (e.g. quiet) and will be
        supplied to the underlying run_command call.

    Returns: A dictionary of dictionaries where the first level keys are the
        job ids and with the their values being a dictionary of configuration
        parameters for the respective job.
    """

    jobs_dict = {}

    command = "scontrol -d -o show jobs"
    if job_id is not None:
        command += f" {job_id}"
    output = run_command_output(command, fatal=True, **run_command_kwargs)

    job_dict = {}
    for line in output.splitlines():
        if line == '':
            continue

        while match := re.search(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', line):
            param_name, param_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', '', line)

            # Reformat the value if necessary
            if is_integer(param_value):
                param_value = int(param_value)
            elif is_float(param_value):
                param_value = float(param_value)
            elif param_value == '(null)':
                param_value = None

            # Add it to the temporary job dictionary
            job_dict[param_name] = param_value

        # Add the job dictionary to the jobs dictionary
        if job_dict:
            jobs_dict[job_dict['JobId']] = job_dict

            # Clear the job dictionary for use by the next job
            job_dict = {}

    return jobs_dict


def get_job(job_id, quiet=False):
    """Returns job information for a specific job as a dictionary.

    Args:
        job_id (integer): The job id.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns: A dictionary of parameters for the specified job.
    """

    jobs_dict = get_jobs(job_id, quiet=quiet)

    return jobs_dict[job_id] if job_id in jobs_dict else {}


def get_job_parameter(job_id, parameter_name, default=None, quiet=False):
    """Obtains the value for a job parameter.

    Args:
        job_id (integer): The job id.
        parameter_name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.
        quiet (boolean): If True, logging is performed at the TRACE log level.

    Returns: The value of the specified job parameter, or the default if not
        found.
    """

    jobs_dict = get_jobs(quiet=quiet)

    if job_id in jobs_dict:
        job_dict = jobs_dict[job_id]
    else:
        log_die(f"Job ({job_id}) was not found in the job configuration")

    if parameter_name in job_dict:
        return job_dict[parameter_name]
    else:
        return default


def wait_for_job_state(job_id, desired_job_state, timeout=5, poll_interval=None, fatal=False, quiet=False):
    """Waits for the specified job state to be reached.

    This function polls the job state every poll interval seconds, waiting up
    to the timeout for the specified job state to be reached.

    Supported target states include:
        DONE, PENDING, PREEMPTED, RUNNING, SPECIAL_EXIT, SUSPENDED

    Some of the supported job states are aggregate states, and may be satisfied
    by multiple discrete states. Some logic is built-in to fail if a job
    reaches a state that makes the desired job state impossible to reach.

    Args:
        job_id (integer): The job id.
        desired_job_state (string): The desired job state.
        timeout (integer): Number of seconds to poll before timing out.
        poll_interval (float): Number of seconds to wait between job state
            polls.
        fatal (boolean): If True, a timeout will result in the test failing.
        quiet (boolean): If True, logging is performed at the TRACE log level.
    """

    # Verify the desired state is supported
    if desired_job_state not in [
        'DONE',
        'PENDING',
        'PREEMPTED',
        'RUNNING',
        'SPECIAL_EXIT',
        'SUSPENDED',
    ]:
        message = f"The specified desired job state ({desired_job_state}) is not supported"
        if fatal:
            log_die(message)
        else:
            logging.warning(message)
            return False

    if poll_interval is None:
        if timeout <= 5:
            poll_interval = .1
        elif timeout <= 10:
            poll_interval = .2
        else:
            poll_interval = 1

    if quiet:
        log_level = logging.TRACE
    else:
        log_level = logging.DEBUG

    # We don't use repeat_until here because we support pseudo-job states and
    # we want to allow early return (e.g. for a DONE state if we want RUNNING)
    begin_time = time.time()
    logging.log(log_level, f"Waiting for job ({job_id}) to reach state {desired_job_state}")

    while time.time() < begin_time + timeout:
        job_state = get_job_parameter(job_id, 'JobState', default='NOT_FOUND', quiet=True)

        if job_state in [
            'NOT_FOUND',
            'BOOT_FAIL',
            'CANCELLED',
            'COMPLETED',
            'DEADLINE',
            'FAILED',
            'NODE_FAIL',
            'OUT_OF_MEMORY',
            'TIMEOUT',
        ]:
            if desired_job_state == 'DONE':
                logging.log(log_level, f"Job ({job_id}) is in desired state {desired_job_state}")
                return True
            else:
                message = f"Job ({job_id}) is in state {job_state}, but we wanted {desired_job_state}"
                if fatal:
                    log_die(message)
                else:
                    logging.warning(message)
                    return False
        elif job_state in [
            'PENDING',
            'PREEMPTED',
            'RUNNING',
            'SPECIAL_EXIT',
            'SUSPENDED',
        ]:
            if job_state == desired_job_state or (job_state == 'PREEMPTED' and desired_job_state == 'DONE'):
                logging.log(log_level, f"Job ({job_id}) is in desired state {desired_job_state}")
                return True
            else:
                logging.log(log_level, f"Job ({job_id}) is in state {job_state}, but we are waiting for {desired_job_state}")
        else:
                logging.log(log_level, f"Job ({job_id}) is in state {job_state}, but we are waiting for {desired_job_state}")

        time.sleep(poll_interval)

    message = f"Job ({job_id}) did not reach the {desired_job_state} state within the {timeout} second timeout"
    if fatal:
        log_die(message)
    else:
        logging.warning(message)
        return False


def create_node(node_dict):
    """Creates a node with the properties described by the supplied dictionary.

    Currently this function is only used as a helper function within other
    library functions (e.g. require_nodes).

    This function modifies the slurm configuration file and restarts
    the relevant slurm daemons. A backup is automatically created and the
    original configuration is restored after the test completes.

    This function may only be used in auto-config mode.

    Args:
        node_dict (dictionary): A dictionary containing the desired node
            properties.
    """

    if not properties['auto-config']:
        require_auto_config("wants to add a node")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(f"cat {config_file}", user=properties['slurm-user'], quiet=True)
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the last NodeName definition
    last_node_line_index = 0
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        if re.search(r'(?i)^ *NodeName', line) is not None:
            last_node_line_index = line_index
    if last_node_line_index == 0:
        last_node_line_index = line_index

    # Build up the new node line
    node_line = ''
    if 'NodeName' in node_dict:
        node_line = f"NodeName={node_dict['NodeName']}"
        node_dict.pop('NodeName')
    if 'Port' in node_dict:
        node_line += f" Port={node_dict['Port']}"
        node_dict.pop('Port')
    for parameter_name, parameter_value in sorted(node_dict.items()):
        node_line += f" {parameter_name}={parameter_value}"

    # Add the new node line
    new_config_lines.insert(last_node_line_index + 1, node_line)

    # Write the config file back out with the modifications
    backup_config_file('slurm')
    new_config_string = "\n".join(new_config_lines)
    run_command(f"echo '{new_config_string}' > {config_file}", user=properties['slurm-user'], fatal=True, quiet=True)

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

    Example:
        >>> require_nodes(2, [('CPUs', 4), ('RealMemory', 40)])
    """

    # If using local-config and slurm is running, use live node information
    # so that a test is not incorrectly skipped when slurm derives a non-single
    # CPUTot while the slurm.conf does not contain a CPUs property.
    if not properties['auto-config'] and is_slurmctld_running(quiet=True):
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
        if node_name == 'DEFAULT':
            default_node = nodes_dict[node_name]
        else:
            original_nodes[node_name] = {}

    # Populate with any default parameters
    if default_node:
        for node_name in original_nodes:
            for parameter_name, parameter_value in default_node.items():
                if parameter_name.lower() != 'nodename':
                    original_nodes[node_name][parameter_name] = parameter_value

    # Merge in parameters from nodes_dict
    for node_name in original_nodes:
        for parameter_name, parameter_value in nodes_dict[node_name].items():
            if parameter_name.lower() != 'nodename':
                # Translate CPUTot to CPUs for screening qualifying nodes
                if parameter_name.lower() == 'cputot':
                    parameter_name = 'CPUs'
                original_nodes[node_name][parameter_name] = parameter_value

    # Check to see how many qualifying nodes we have
    qualifying_node_count = 0
    node_count = 0
    nonqualifying_node_count = 0
    first_node_name = ''
    first_qualifying_node_name = ''
    node_indices = {}
    augmentation_dict = {}
    for node_name in sorted(original_nodes):
        lower_node_dict = dict((key.lower(), value) for key, value in original_nodes[node_name].items())
        node_count += 1

        if node_count == 1:
            first_node_name = node_name

        # Build up node indices for use when having to create new nodes
        match = re.search(r'^(.*?)(\d*)$', node_name)
        node_prefix, node_index = match.group(1), match.group(2)
        if node_index == '':
            node_indices[node_prefix] = node_indices.get(node_prefix, [])
        else:
            node_indices[node_prefix] = node_indices.get(node_prefix, []) + [int(node_index)]

        node_qualifies = True
        for requirement_tuple in requirements_list:
            parameter_name, parameter_value = requirement_tuple[0:2]
            if parameter_name in ['CPUs', 'RealMemory']:
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
            elif parameter_name == 'Cores':
                boards = lower_node_dict.get('boards', 1)
                sockets_per_board = lower_node_dict.get('socketsperboard', 1)
                cores_per_socket = lower_node_dict.get('corespersocket', 1)
                sockets = boards * sockets_per_board
                cores = sockets * cores_per_socket
                if cores < parameter_value:
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict['CoresPerSocket'] = math.ceil(parameter_value / sockets)
            elif parameter_name == 'Gres':
                if parameter_name.lower() in lower_node_dict:
                    if match := re.search(r'^(\w+):(\d+)$', parameter_value):
                        (required_gres_name, required_gres_value) = (match.group(1), match.group(2))
                    else:
                        log_die("Gres requirement must be of the form <name>:<count>")
                    if match := re.search(rf"{required_gres_name}:(\d+)", lower_node_dict[parameter_name.lower()]):
                        if match.group(1) < required_gres_value:
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
                else:
                    if node_qualifies:
                        node_qualifies = False
                        nonqualifying_node_count += 1
                    if nonqualifying_node_count == 1:
                        augmentation_dict[parameter_name] = parameter_value
            else:
                log_die(f"{parameter_name} is not a supported requirement type")
        if node_qualifies:
            qualifying_node_count += 1
            if first_qualifying_node_name == '':
                first_qualifying_node_name = node_name

    # Not enough qualifying nodes
    if qualifying_node_count < requested_node_count:

        # If auto-config, configure what is required
        if properties['auto-config']:

            # Create new nodes to meet requirements
            new_node_count = requested_node_count - qualifying_node_count

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

            base_port = int(nodes_dict[template_node_name]['Port'])

            # Build up a list of available new indices starting after the template
            match = re.search(r'^(.*?)(\d*)$', template_node_name)
            template_node_prefix, template_node_index = match.group(1), int(match.group(2))
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
                new_node_dict['NodeName'] = template_node_prefix + str(new_indices[0])
                new_node_dict['Port'] = base_port - template_node_index + new_indices[0]
            else:
                new_node_dict['NodeName'] = f"{template_node_prefix}[{list_to_range(new_indices)}]"
                new_node_dict['Port'] = list_to_range(list(map(lambda x: base_port - template_node_index + x, new_indices)))
            create_node(new_node_dict)

        # If local-config, skip
        else:
            message = f"This test requires {requested_node_count} nodes"
            if requirements_list:
                message += f" with {requirements_list}"
            pytest.skip(message, allow_module_level=True)


def make_bash_script(script_name, script_contents):
    """Creates an executable bash script with the specified contents.

    Args:
        script_name (string): Name of script to create.
        script_contents (string): Contents of script.
    """

    with open(script_name, 'w') as f:
        f.write("#!/bin/bash\n")
        f.write(script_contents)
    os.chmod(script_name, 0o0700)


def wait_for_file(file_name, **repeat_until_kwargs):
    """Waits for the specified file to be present.

    This function waits up to timeout seconds for the file to be present,
    polling every poll interval seconds.

    Args*:
        file_name (string): The file name.

    * This function also accepts auxilliary arguments from repeat_until, viz.:
        timeout (integer): Number of seconds to poll before timing out.
        poll_interval (float): Number of seconds to wait between polls
        fatal (boolean): If True, a timeout will result in the test failing.
    """

    logging.debug(f"Waiting for file ({file_name}) to be present")
    return repeat_until(lambda : os.path.isfile(file_name), lambda exists: exists, **repeat_until_kwargs)


# Assuming this will only be called internally after validating accounting is configured and auto-config is set
def backup_accounting_database():
    """Backs up the accounting database.

    This function may only be used in auto-config mode.

    The database dump is automatically be restored when the test ends.
    """

    if not properties['auto-config']:
        return

    mysqldump_path = shutil.which('mysqldump')
    if mysqldump_path is None:
        log_die("Unable to backup the accounting database. mysqldump was not found in your path")
    mysql_path = shutil.which('mysql')
    if mysql_path is None:
        log_die("Unable to backup the accounting database. mysql was not found in your path")

    sql_dump_file = f"{str(module_tmp_path / '../../slurm_acct_db.sql')}"

    # We set this here, because we will want to restore in all cases
    properties['accounting-database-modified'] = True

    # If a dump already exists, issue a warning and return (honor existing dump)
    if os.path.isfile(sql_dump_file):
        logging.warning(f"Dump file already exists ({sql_dump_file})")
        return

    slurmdbd_dict = get_config(live=False, source='slurmdbd', quiet=True)
    database_host, database_port, database_name, database_user, database_password = (slurmdbd_dict.get(key) for key in ['StorageHost', 'StoragePort', 'StorageLoc', 'StorageUser', 'StoragePass'])

    mysql_options = ''
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
        database_name = 'slurm_acct_db';

    # If the slurm database does not exist, touch an empty dump file with
    # the sticky bit set. restore_accounting_database will remove the file.
    mysql_command = f"{mysql_path} {mysql_options} -e \"USE '{database_name}'\""
    if run_command_exit(mysql_command, quiet=True) != 0:
        #logging.warning(f"Slurm accounting database ({database_name}) is not present")
        run_command(f"touch {sql_dump_file}", fatal=True, quiet=True)
        run_command(f"chmod 1000 {sql_dump_file}", fatal=True, quiet=True)

    # Otherwise, copy the config file to the backup
    else:

        mysqldump_command = f"{mysqldump_path} {mysql_options} {database_name} > {sql_dump_file}"
        run_command(mysqldump_command, fatal=True, quiet=True)


def restore_accounting_database():
    """Restores the accounting database from the backup.

    This function may only be used in auto-config mode.
    """

    if not properties['accounting-database-modified'] or not properties['auto-config']:
        return

    sql_dump_file = f"{str(module_tmp_path / '../../slurm_acct_db.sql')}"

    # If the dump file doesn't exist, it has probably already been
    # restored by a previous call to restore_accounting_database
    if not os.path.isfile(sql_dump_file):
        logging.warning(f"Slurm accounting database backup ({sql_dump_file}) is s not present. It has probably already been restored.")
        return

    mysql_path = shutil.which('mysql')
    if mysql_path is None:
        log_die("Unable to restore the accounting database. mysql was not found in your path")

    slurmdbd_dict = get_config(live=False, source='slurmdbd', quiet=True)
    database_host, database_port, database_name, database_user, database_password = (slurmdbd_dict.get(key) for key in ['StorageHost', 'StoragePort', 'StorageLoc', 'StorageUser', 'StoragePass'])
    if not database_name:
        database_name = 'slurm_acct_db'

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

    run_command(f"{base_command} -e \"drop database {database_name}\"", fatal=True, quiet=True)
    dump_stat = os.stat(sql_dump_file)
    if not (dump_stat.st_size == 0 and dump_stat.st_mode & stat.S_ISVTX):
        run_command(f"{base_command} -e \"create database {database_name}\"", fatal=True, quiet=True)
        run_command(f"{base_command} {database_name} < {sql_dump_file}", fatal=True, quiet=True)

    # In either case, remove the dump file
    run_command(f"rm -f {sql_dump_file}", fatal=True, quiet=True)

    properties['accounting-database-modified'] = False


def compile_against_libslurm(source_file, dest_file, build_args='', full=False, shared=False):
    """Compiles a test program against either libslurm.so or libslurmfull.so.

    Args:
        source_file (string): The name of the source file.
        dest_file (string): The name of the target binary file.
        build_args (string): Additional string to be appended to the build
            command.
        full (boolean): Use libslurmfull.so instead of libslurm.so.
        shared (boolean): Produces a shared library (adds the -shared compiler
            option and adds a .so suffix to the output file name).
    """

    if full:
        slurm_library = 'slurmfull'
    else:
        slurm_library = 'slurm'
    if os.path.isfile(f"{properties['slurm-prefix']}/lib64/slurm/lib{slurm_library}.so"):
        lib_dir = 'lib64'
    else:
        lib_dir = 'lib'
    if full:
        lib_path = f"{properties['slurm-prefix']}/{lib_dir}/slurm"
    else:
        lib_path = f"{properties['slurm-prefix']}/{lib_dir}"

    command = f"gcc {source_file} -g -pthread"
    if shared:
        command += " -fPIC -shared"
    command += f" -o {dest_file}"
    command += f" -I{properties['slurm-source-dir']} -I{properties['slurm-build-dir']} -I{properties['slurm-prefix']}/include -Wl,-rpath={lib_path} -L{lib_path} -l{slurm_library} -lresolv"
    if build_args != '':
        command += f" {build_args}"
    run_command(command, fatal=True)


def get_partitions(**run_command_kwargs):
    """Returns the partition configuration as a dictionary of dictionaries.

    This function accepts auxilliary arguments from run_command (e.g. quiet,
    fatal, timeout, etc).

    Returns: A dictionary of dictionaries where the first level keys are the
        partition names and with the their values being a dictionary of
        configuration parameters for the respective partition.
    """

    partitions_dict = {}

    output = run_command_output("scontrol show partition -o", fatal=True, **run_command_kwargs)

    partition_dict = {}
    for line in output.splitlines():
        if line == '':
            continue

        while match := re.search(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', line):
            param_name, param_value = match.group(1), match.group(2)

            # Remove the consumed parameter from the line
            line = re.sub(r'^ *([^ =]+)=(.*?)(?= +[^ =]+=| *$)', '', line)

            # Reformat the value if necessary
            if is_integer(param_value):
                param_value = int(param_value)
            elif is_float(param_value):
                param_value = float(param_value)
            elif param_value == '(null)':
                param_value = None

            # Add it to the temporary partition dictionary
            partition_dict[param_name] = param_value

        # Add the partition dictionary to the partitions dictionary
        partitions_dict[partition_dict['PartitionName']] = partition_dict

        # Clear the partition dictionary for use by the next partition
        partition_dict = {}

    return partitions_dict


def get_partition_parameter(partition_name, parameter_name, default=None):
    """Obtains the value for a partition configuration parameter.

    Args:
        partition_name (string): The partition name.
        parameter_name (string): The parameter name.
        default (string or None): This value is returned if the parameter
            is not found.

    Returns: The value of the specified partition parameter, or the default if
        not found.
    """

    partitions_dict = get_partitions()

    if partition_name in partitions_dict:
        partition_dict = partitions_dict[partition_name]
    else:
        log_die(f"Partition ({partition_name}) was not found in the partition configuration")

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

    if not properties['auto-config']:
        require_auto_config("wants to modify partition parameters")

    config_file = f"{properties['slurm-config-dir']}/slurm.conf"

    # Read the original slurm.conf into a list of lines
    output = run_command_output(f"cat {config_file}", user=properties['slurm-user'], quiet=True)
    original_config_lines = output.splitlines()
    new_config_lines = original_config_lines.copy()

    # Locate the partition among the various Partition definitions
    found_partition_name = False
    for line_index in range(len(original_config_lines)):
        line = original_config_lines[line_index]

        words = re.split(r' +', line.strip())
        if len(words) < 1:
            continue
        parameter_name, parameter_value = words[0].split('=', 1)
        if parameter_name.lower() != 'partitionname':
            continue

        if parameter_value == partition_name:
            # We found a matching PartitionName line
            found_partition_name = True

            # Read in the partition parameters
            original_partition_parameters = collections.OrderedDict()
            for word in words[1:]:
                parameter_name, parameter_value = word.split('=', 1)
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
            for parameter_name, parameter_value in modified_partition_parameters.items():
                modified_partition_line += f" {parameter_name}={parameter_value}"
            new_config_lines.insert(line_index, modified_partition_line)

            break

    if not found_partition_name:
        log_die(f"Invalid partition name specified in set_partition_parameter(). Partition {partition_name} does not exist")

    # Write the config file back out with the modifications
    backup_config_file('slurm')
    new_config_string = "\n".join(new_config_lines)
    run_command(f"echo '{new_config_string}' > {config_file}", user=properties['slurm-user'], fatal=True, quiet=True)

    # Reconfigure slurm controller if it is already running
    if is_slurmctld_running(quiet=True):
        run_command("scontrol reconfigure", user=properties['slurm-user'], quiet=True)


def default_partition():
    """Returns the default partition name."""

    partitions_dict = get_partitions()

    for partition_name in partitions_dict:
        if partitions_dict[partition_name]['Default'] == 'YES':
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
                if 'testsuite/python' not in frame_summary.filename:
                    break
            else:
                if 'testsuite/python' in frame_summary.filename:
                    within_atf_context = True
                else:
                    continue

            function = frame_summary.name
            short_filename = frame_summary.filename.rpartition('testsuite/python/')[2]
            lineno = frame_summary.lineno

            call_stack.append(f"{function}@{short_filename}:{lineno}")

        record.traceback = ','.join(call_stack)

        return True


# Add a new traceback LogRecord attribute
logging.getLogger().addFilter(TraceBackFilter())

# Add a custom TRACE logging level
# This has to be done early enough to allow pytest --log-level=TRACE to be used
logging.TRACE = logging.NOTSET + 5
logging.addLevelName(logging.TRACE, 'TRACE')
def _trace(message, *args, **kwargs):
    logging.log(logging.TRACE, message, *args, **kwargs)
logging.trace = _trace
logging.getLogger().trace = _trace

# Add a custom NOTE logging level in between INFO and DEBUG
logging.NOTE = logging.DEBUG + 5
logging.addLevelName(logging.NOTE, 'NOTE')
def _note(message, *args, **kwargs):
    logging.log(logging.NOTE, message, *args, **kwargs)
logging.note = _note
logging.getLogger().note = _note

# The module-level temporary directory is initialized in conftest.py
module_tmp_path = None

# Instantiate and populate testrun-level properties
properties = {}

# Initialize directory properties
testsuite_base_dir = str(pathlib.Path(__file__).resolve().parents[2])
properties['slurm-source-dir'] = str(pathlib.Path(__file__).resolve().parents[3])
properties['slurm-build-dir'] = properties['slurm-source-dir']
properties['slurm-prefix'] = '/usr/local'

# Override directory properties with values from testsuite.conf file
testsuite_config = {}
# The default location for the testsuite.conf file (in SRCDIR/testsuite)
# can be overridden with the SLURM_TESTSUITE_CONF environment variable.
testsuite_config_file = os.getenv('SLURM_TESTSUITE_CONF', f"{testsuite_base_dir}/testsuite.conf")
if not os.path.isfile(testsuite_config_file):
    log_die(f"The python testsuite was expecting testsuite.conf to be found in {testsuite_base_dir}. This file is created in your build directory when running make install. If your build directory is separate from your source directory, set the value of the SLURM_TESTSUITE_CONF environment variable to the absolute path of your testsuite.conf file.")
with open(testsuite_config_file, 'r') as f:
    for line in f.readlines():
        if match := re.search(rf"^\s*(\w+)\s*=\s*(.*)$", line):
            testsuite_config[match.group(1).lower()] = match.group(2)
if 'slurmsourcedir' in testsuite_config:
    properties['slurm-source-dir'] = testsuite_config['slurmsourcedir']
if 'slurmbuilddir' in testsuite_config:
    properties['slurm-build-dir'] = testsuite_config['slurmbuilddir']
if 'slurminstalldir' in testsuite_config:
    properties['slurm-prefix'] = testsuite_config['slurminstalldir']
if 'slurmconfigdir' in testsuite_config:
    properties['slurm-config-dir'] = testsuite_config['slurmconfigdir']

# Set derived directory properties
# The environment (e.g. PATH, SLURM_CONF) overrides configuration
properties['slurm-bin-dir'] = f"{properties['slurm-config-dir']}/bin"
if squeue_path := shutil.which('squeue'):
    properties['slurm-bin-dir'] = os.path.dirname(squeue_path)
properties['slurm-sbin-dir'] = f"{properties['slurm-config-dir']}/sbin"
if slurmctld_path := shutil.which('slurmctld'):
    properties['slurm-sbin-dir'] = os.path.dirname(slurmctld_path)
properties['slurm-config-dir'] = re.sub(r'\${prefix}', properties['slurm-prefix'], properties['slurm-config-dir'])
if slurm_conf_path := os.getenv('SLURM_CONF'):
    properties['slurm-config-dir'] = os.path.dirname(slurm_conf_path)

# Derive the slurm-user value
properties['slurm-user'] = 'root'
slurm_config_file = f"{properties['slurm-config-dir']}/slurm.conf"
if not os.path.isfile(slurm_config_file):
    log_die(f"The python testsuite was expecting your slurm.conf to be found in {properties['slurm-config-dir']}. Please create it or use the SLURM_CONF environment variable to indicate its location.")
if os.access(slurm_config_file, os.R_OK):
    with open(slurm_config_file, 'r') as f:
        for line in f.readlines():
            if match := re.search(rf"^\s*(?i:SlurmUser)\s*=\s*(.*)$", line):
                properties['slurm-user'] = match.group(1)
else:
    # slurm.conf is not readable as test-user. We will try reading it as root
    results = run_command(f"grep -i SlurmUser {slurm_config_file}", user='root', quiet=True)
    if results['exit_code'] == 0:
        log_die(f"Unable to read {slurm_config_file}")
    for line in results['stdout'].splitlines():
        if match := re.search(rf"^\s*(?i:SlurmUser)\s*=\s*(.*)$", line):
            properties['slurm-user'] = match.group(1)

properties['test-user'] = pwd.getpwuid(os.getuid()).pw_name
properties['auto-config'] = False
properties['include-expect'] = False

# Instantiate a nodes dictionary. These are populated in require_slurm_running.
nodes = {}

# Check if user has sudo privileges
results = subprocess.run("sudo -ln | grep -q '(ALL.*) NOPASSWD: ALL'", shell=True, capture_output=True, text=True)
if results.returncode == 0:
        properties['sudo-rights'] = True
else:
        properties['sudo-rights'] = False


