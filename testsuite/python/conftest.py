############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import _pytest

# import inspect
import logging
import os

# import pwd
import pytest
import re

import shutil
import sys

from pathlib import Path

sys.path.append(sys.path[0] + "/lib")
import atf


# Add test description (docstring) as a junit property
def pytest_itemcollected(item):
    node = item.obj
    desc = node.__doc__.strip() if node.__doc__ else node.__name__
    if desc:
        item.user_properties.append(("description", desc))


def pytest_addoption(parser):
    config_group = parser.getgroup("config mode")
    config_group.addoption(
        "--auto-config",
        action="store_true",
        help="the slurm configuration will be altered as needed by the test",
    )
    config_group.addoption(
        "--local-config",
        action="store_false",
        dest="auto_config",
        help="the slurm configuration will not be altered",
    )
    parser.addoption(
        "--no-color",
        action="store_true",
        dest="no_color",
        help="the pytest logs won't include colors",
    )
    parser.addoption(
        "--allow-slurmdbd-modify",
        action="store_true",
        dest="allow_slurmdbd_modify",
        help="allow running in local-config even if require_accounting(modify=True)",
    )


def color_log_level(level: int, **color_kwargs):
    # Adapted from deprecated py.io TerminalWriter source
    # https://py.readthedocs.io/en/latest/_modules/py/_io/terminalwriter.html
    _esctable = dict(
        black=30,
        red=31,
        green=32,
        yellow=33,
        blue=34,
        purple=35,
        cyan=36,
        white=37,
        Black=40,
        Red=41,
        Green=42,
        Yellow=43,
        Blue=44,
        Purple=45,
        Cyan=46,
        White=47,
        bold=1,
        light=2,
        blink=5,
        invert=7,
    )

    for handler in logging.getLogger().handlers:
        if isinstance(handler, _pytest.logging.LogCaptureHandler):
            formatter = handler.formatter
            if match := formatter.LEVELNAME_FMT_REGEX.search(formatter._fmt):
                levelname_fmt = match.group()
                formatted_levelname = levelname_fmt % {
                    "levelname": logging.getLevelName(level)
                }

                esc = []
                for option in color_kwargs:
                    esc.append(_esctable[option])

                colorized_formatted_levelname = (
                    "".join(["\x1b[%sm" % cod for cod in esc])
                    + formatted_levelname
                    + "\x1b[0m"
                )

                formatter._level_to_fmt_mapping[level] = (
                    formatter.LEVELNAME_FMT_REGEX.sub(
                        colorized_formatted_levelname, formatter._fmt
                    )
                )


@pytest.fixture(scope="session", autouse=True)
def session_setup(request):
    # Set the auto-config and other properties from the options
    atf.properties["auto-config"] = request.config.getoption("--auto-config")
    atf.properties["allow-slurmdbd-modify"] = request.config.getoption(
        "--allow-slurmdbd-modify"
    )
    if not request.config.getoption("--no-color"):
        # Customize logging level colors
        color_log_level(logging.CRITICAL, red=True, bold=True)
        color_log_level(logging.ERROR, red=True)
        color_log_level(logging.WARNING, yellow=True)
        color_log_level(logging.INFO, green=True)
        color_log_level(logging.NOTE, cyan=True)
        color_log_level(logging.DEBUG, blue=True, bold=True)
        color_log_level(logging.TRACE, purple=True, bold=True)


def update_tmp_path_exec_permissions(path):
    """
    For pytest versions 6+  the tmp path it uses no longer has
    public exec permissions for dynamically created directories by default.

    This causes problems when trying to read temp files during tests as
    users other than atf (ie slurm).  The tests will fail with permission denied.

    To fix this we check and add the x bit to the public group on tmp
    directories so the files inside can be read. Adding just 'read' is
    not enough

    Bug 16568
    """

    if os.path.isdir(path):
        os.chmod(path, 0o777)
        for root, dirs, files in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), 0o777)

        # Ensure access for parent dirs too
        path = path.resolve()
        if not path.is_relative_to("/tmp"):
            pytest.fail(f"Unexpected tmp path outside /tmp: {path}")

        subdir = Path("/tmp")
        for part in path.relative_to("/tmp").parts:
            subdir = subdir / part
            os.chmod(subdir, 0o777)


@pytest.fixture(scope="module", autouse=True)
def module_setup(request, tmp_path_factory):
    atf.properties["slurm-started"] = False
    atf.properties["slurmrestd-started"] = False
    atf.properties["influxdb-started"] = False
    atf.properties["configurations-modified"] = set()
    atf.properties["orig-environment"] = dict(os.environ)
    atf.properties["orig-pypath"] = list(sys.path)
    atf.properties["forced_upgrade_setup"] = False
    if "old-slurm-prefix" in atf.properties.keys():
        del atf.properties["old-slurm-prefix"]
    if "new-slurm-prefix" in atf.properties.keys():
        del atf.properties["new-slurm-prefix"]

    # Ensure that slurm-spool-dir, slurm-tmpfs and nodes are set.
    atf.properties["slurm-spool-dir"] = atf.get_config_parameter(
        "SlurmdSpoolDir", live=False, quiet=True
    )
    atf.properties["slurm-tmpfs"] = atf.get_config_parameter(
        "TmpFS", live=False, quiet=True
    )
    atf.properties["nodes"] = []

    # Creating a module level tmp_path mimicking what tmp_path does
    name = request.node.name
    name = re.sub(r"[\W]", "_", name)
    name = name[:30]
    atf.properties["test_name"] = name
    atf.module_tmp_path = tmp_path_factory.mktemp(name, numbered=True)
    update_tmp_path_exec_permissions(atf.module_tmp_path)

    # Module-level fixtures should run from within the module_tmp_path
    os.chdir(atf.module_tmp_path)

    # Stop Slurm if using auto-config and Slurm is already running
    if atf.properties["auto-config"] and atf.is_slurmctld_running(quiet=True):
        logging.warning(
            "Auto-config requires Slurm to be initially stopped but Slurm was found running. Stopping Slurm"
        )
        atf.stop_slurm(quiet=True)

    if atf.properties["auto-config"]:
        # Cleanup StateSaveLocation for auto-config
        atf.properties["statesaveloc"] = atf.get_config_parameter(
            "StateSaveLocation", live=False, quiet=True
        )
        if os.path.exists(atf.properties["statesaveloc"]):
            if os.path.exists(atf.properties["statesaveloc"] + name):
                logging.warning(
                    f"Backup for StateSaveLocation already exists ({atf.properties['statesaveloc']+name}). Removing it."
                )
                atf.run_command(
                    f"rm -rf {atf.properties['statesaveloc']+name}",
                    user="root",
                    quiet=True,
                )
            atf.run_command(
                f"mv {atf.properties['statesaveloc']} {atf.properties['statesaveloc']+name}",
                user="root",
                quiet=True,
            )

        # Create the required node directories for node0
        node_name = "node0"
        spool_dir = atf.properties["slurm-spool-dir"].replace("%n", node_name)
        tmpfs_dir = atf.properties["slurm-tmpfs"].replace("%n", node_name)
        atf.properties["nodes"].append(node_name)
        atf.run_command(f"sudo mkdir -p {spool_dir}", fatal=True, quiet=True)
        atf.run_command(f"sudo mkdir -p {tmpfs_dir}", fatal=True, quiet=True)

    yield

    # Return to the folder from which pytest was executed
    os.chdir(request.config.invocation_dir)

    # Teardown
    module_teardown()


def module_teardown():
    failures = []

    if atf.properties["auto-config"]:
        if atf.properties["slurm-started"] is True:
            # Cancel all jobs
            if not atf.cancel_all_jobs(quiet=True):
                failures.append("Not all jobs were successfully cancelled")

            # Stop Slurm if we started it
            if not atf.stop_slurm(fatal=False, quiet=True):
                failures.append("Not all Slurm daemons were successfully stopped")

        # Restore the Slurm database
        atf.restore_accounting_database()

        # Restore StateSaveLocation for auto-config
        atf.run_command(
            f"rm -rf {atf.properties['statesaveloc']}", user="root", quiet=True
        )
        if os.path.exists(atf.properties["statesaveloc"] + atf.properties["test_name"]):
            atf.run_command(
                f"mv {atf.properties['statesaveloc']+atf.properties['test_name']} {atf.properties['statesaveloc']}",
                user="root",
                quiet=True,
            )

        # Remove Nodes directories:
        if "nodes" not in atf.properties:
            atf.properties["nodes"] = ["node0"]
        for node_name in atf.properties["nodes"]:
            spool_dir = atf.properties["slurm-spool-dir"].replace("%n", node_name)
            tmpfs_dir = atf.properties["slurm-tmpfs"].replace("%n", node_name)
            atf.run_command(f"sudo rm -rf {spool_dir}", quiet=True)
            atf.run_command(f"sudo rm -rf {tmpfs_dir}", quiet=True)

        # Restore upgrade setup
        if atf.properties.get("forced_upgrade_setup"):
            logging.debug("Restoring upgrade setup...")
            if not os.path.exists(f"{atf.module_tmp_path}/upgrade-sbin"):
                pytest.fail(
                    f"Can't restore upgrade setup, {atf.module_tmp_path}/upgrade-sbin doesn't exists."
                )
            if not os.path.exists(f"{atf.module_tmp_path}/upgrade-bin"):
                pytest.fail(
                    f"Can't restore upgrade setup, {atf.module_tmp_path}/upgrade-bin doesn't exists."
                )
            atf.run_command(
                f"sudo rm -rf {atf.properties['slurm-sbin-dir']} {atf.properties['slurm-bin-dir']}",
                quiet=True,
                fatal=True,
            )
            atf.run_command(
                f"sudo mv {atf.module_tmp_path}/upgrade-sbin {atf.properties['slurm-sbin-dir']}",
                quiet=True,
                fatal=True,
            )
            atf.run_command(
                f"sudo mv {atf.module_tmp_path}/upgrade-bin {atf.properties['slurm-bin-dir']}",
                quiet=True,
                fatal=True,
            )

        # Restore any backed up configuration files
        for config in set(atf.properties["configurations-modified"]):
            atf.restore_config_file(config)

        # Clean influxdb
        if atf.properties["influxdb-started"]:
            atf.request_influxdb(f"DROP DATABASE {atf.properties['influxdb_db']}")
    else:
        atf.cancel_jobs(atf.properties["submitted-jobs"])

    # Restore the prior environment
    os.environ.clear()
    os.environ.update(atf.properties["orig-environment"])
    sys.path = atf.properties["orig-pypath"]

    if failures:
        pytest.fail(failures[0])


@pytest.fixture(scope="function", autouse=True)
def function_setup(request, monkeypatch, tmp_path):
    # Log function docstring (test description) if present
    if request.function.__doc__ is not None:
        logging.info(request.function.__doc__)

    # Start each test inside the tmp_path
    update_tmp_path_exec_permissions(tmp_path)
    monkeypatch.chdir(tmp_path)


@pytest.fixture(scope="class", autouse=True)
def class_setup(request):
    # Log class docstring (test description) if present
    if request.cls.__doc__ is not None:
        logging.info(request.cls.__doc__)


def pytest_keyboard_interrupt(excinfo):
    """Called for keyboard interrupt"""
    module_teardown()


@pytest.fixture(scope="module")
def mpi_program(module_setup):
    """Create the MPI program from the mpi_program.c in scripts directory.
    Returns the bin path of the mpi_program."""

    # Check for MPI setup
    atf.require_mpi("pmix", "mpicc")

    # Use the external C source file
    src_path = atf.properties["testsuite_scripts_dir"] + "/mpi_program.c"
    bin_path = os.getcwd() + "/mpi_program"

    # Compile the MPI program
    atf.run_command(f"mpicc -o {bin_path} {src_path}", fatal=True)

    yield bin_path

    atf.run_command(f"rm -f {bin_path}", fatal=True)


@pytest.fixture(scope="module")
def use_memory_program(module_setup):
    """
    Returns the bin path of a program that allocates a certain amount of MB for some seconds.
    """

    atf.require_tool("python3")

    src_path = atf.properties["testsuite_scripts_dir"] + "/use_memory_program.py"
    bin_path = os.getcwd() + "/use_memory_program.py"

    # Ensure x permissions
    atf.run_command(f"cp {src_path} {bin_path}")
    atf.run_command(f"chmod a+x {bin_path}")

    yield bin_path

    atf.run_command(f"rm -f {bin_path}", fatal=True)


@pytest.fixture(scope="module")
def spank_fail_lib(module_setup):
    """
    Returns the bin path of the spank .so that will fail if configured.
    """

    # The plugin uses ESPANK_NODE_FAILURE, so it needs to compile against 25.05+
    # It also needs to be built against the same version of slurmd and submit
    # clients like sbatch
    new_prefixes = False
    if not atf.is_upgrade_setup():
        atf.require_version((25, 5), "config.h")
    else:
        slurmd_version = atf.get_version("sbin/slurmd")
        sbatch_version = atf.get_version("bin/sbatch")

        if slurmd_version != sbatch_version:
            pytest.skip(
                f"We need to build SPANK against Slurm version of submit clients as sbatch {sbatch_version} and slurmd {slurmd_version}, but they diffear."
            )

        if slurmd_version < (25, 5):
            pytest.skip(
                f"This SPANK plugin needs a Slurm 25.05+, but slurmd version is {slurmd_version}"
            )

        if (
            atf.get_version("config.h", slurm_prefix=atf.properties["new-build-prefix"])
            == slurmd_version
        ):
            new_prefixes = True
        elif (
            not atf.get_version(
                "config.h", slurm_prefix=atf.properties["old-build-prefix"]
            )
            == slurmd_version
        ):
            # This should never happen, slurmd should be one of those versions
            pytest.fail(
                "Unable to find build dir to match slurmd version {slurmd_version}"
            )

    src_path = atf.properties["testsuite_scripts_dir"] + "/spank_fail_test.c"
    bin_path = os.getcwd() + "/spank_fail_test.so"

    atf.compile_against_libslurm(
        src_path, bin_path, full=True, shared=True, new_prefixes=new_prefixes
    )

    yield bin_path

    atf.run_command(f"rm -f {bin_path}", fatal=True)


@pytest.fixture(scope="module")
def spank_tmp_lib(module_setup):
    """
    Compiles a SPANK plugin that will write files in a /tmp directory.
    Returns the tmp_spank dir and the bin path of the spank .so that will write
    files in the tmp_spank dir if configured.
    """

    # The plugin uses ESPANK_NODE_FAILURE, so it needs to compile against 25.05+
    # It also needs to be built against the same version of slurmd and submit
    # clients like sbatch
    new_prefixes = False
    if atf.is_upgrade_setup():
        slurmd_version = atf.get_version("sbin/slurmd")
        sbatch_version = atf.get_version("bin/sbatch")

        if slurmd_version != sbatch_version:
            pytest.skip(
                f"We need to build SPANK against Slurm version of submit clients as sbatch {sbatch_version} and slurmd {slurmd_version}, but they diffear."
            )
        if (
            atf.get_version("config.h", slurm_prefix=atf.properties["new-build-prefix"])
            == slurmd_version
        ):
            new_prefixes = True
        elif (
            not atf.get_version(
                "config.h", slurm_prefix=atf.properties["old-build-prefix"]
            )
            == slurmd_version
        ):
            # This should never happen, slurmd should be one of those versions
            pytest.fail(
                "Unable to find build dir to match slurmd version {slurmd_version}"
            )

    src_path = atf.properties["testsuite_scripts_dir"] + "/spank_tmp_plugin.c"
    bin_path = os.getcwd() + "/spank_tmp_plugin.so"

    atf.compile_against_libslurm(
        src_path, bin_path, full=True, shared=True, new_prefixes=new_prefixes
    )

    tmp_spank = "/tmp/spank"
    atf.run_command(f"mkdir -p {tmp_spank}", fatal=True)

    yield tmp_spank, bin_path

    atf.run_command(f"rm -f {bin_path}", fatal=True)
    atf.run_command(f"rm -rf {tmp_spank}", fatal=True)


@pytest.fixture(scope="module")
def sql_statement_repeat(module_setup):
    """Loads statement_repeat sql procedure.

    This function may only be used in auto-config mode and only needs to be
    called once to load the statement_repeat() sql procedure that may be called
    as part of an sql query. See usage and examples below.

    Args:
        None

    Returns:
        mysql_command_base - string filled in with mysql command basic options
                             to be used when running the mysql command
    """

    if not atf.properties["auto-config"]:
        return

    mysql_path = shutil.which("mysql")
    if mysql_path is None:
        pytest.fail(
            "Unable to load statement_repeat sql procedure. mysql was not found in your path"
        )

    """
    Usage:

    statement_repeat(stmt_str, seq_start, seq_end, step, use_trans)

    Execute statement(s) in stmt_str repeatedly at the sql server level to avoid
    repeatedly sending statements from the client.

    Arguments:

    stmt_str  - A single sql statement or multiple ones separated by a semicolon
                to be repeatedly executed.

                Content of each statement in stmt_str is limited to what is
                allowed by the "prepare" statement.

                Note that a simple split on this character is done so care must
                be taken when using it to avoid inadvertently
                splitting the string. One
                way would be to create a procedure containing the desired
                statements and then call statement_repeat() with a single
                statement calling that procedure. See Performance Considerations
                (below).

    seq_start - Starting sequence value. If specified as NULL, 1 will be used.

                The sequence value is accessible to statements in the user
                variable @seq. stmt_str will be repeated N times where
                N=floor(abs(seq_start-seq_end)/step)+1. If nstart > nstop,
                sequence will be decreasing.

    seq_end   - Ending sequence value. If specified as NULL, 1 will be used.

                Actual ending sequence value may be less than seq_end if
                step > 1.

    step      - Sequence step value. If specified as NULL, 1 will be used.
                If step < 0, it will be set to its absolute value to avoid
                an infinite loop.

    use_trans - Execute statement(s) in stmt_str within a single transaction.
                Use NULL or non-zero for true, 0 for false. Generally, a single
                transaction will be faster but may not be desired when bench-
                marking statements (and autocommit is ON).


    User variables visible to statements in stmt_str as set in the procedure:

    @now      - Current timestamp (seconds since epoch) at start of procedure.
    @seq      - Sequence number (between seq_start and seq_end).
    @stmt     - Statement string being executed.

    Note that any user variables set outside the procedure are visible to the
    statements.

    This procedure was inspired my MariaDB's sequence engine which is not
    available in MySQL.


    Performance Considerations:

    When one statement is given in stmt_str, "prepare" is run once then the
    prepared statement is executed N times.

    When multiple statements are given, "prepare" and "execute" are each run once
    per statement and this is repeated N times.


    Examples:

    Decreasing sequence from 5 down to 1 using step 2 in a single transaction:
     call statement_repeat('select @seq+5', 5, 1, 2, 1);

    Run statement 10 times in a single transaction:
     call statement_repeat('select unix_timestamp()', 10, null, null, null);

    Increasing sequence from 1 up to 3 using step 1 not grouped in a
    transaction:
     call statement_repeat('select @seq as \'sequence number\',@now as timestamp,@stmt as statement', 1, 3, 1, 0);

    Populate Slurm user table with 500 users named user1..user500 in a single transaction:
     call statement_repeat('insert into user_table (creation_time,name) values (@now, concat(\'user\', @seq))', 1, 500, 1, 1)

    Populate Slurm job table with 10000 jobs in a single transaction:
     call statement_repeat('insert into mycluster_job_table (cpus_req,job_name,id_assoc,id_job,id_resv,id_wckey,id_user,id_group,het_job_id,het_job_offset,state_reason_prev,nodes_alloc,`partition`,priority,state,time_end,env_hash_inx,script_hash_inx) values (0,concat(\'job\', @seq),0,@seq,0,0,0,0,0,0,0,0,\'\',0,0,@now,@seq,@seq)', 1, 10000, 1, 1)
    """

    statement_repeat_sql = """
        delimiter //
        drop procedure if exists statement_repeat //
        create procedure statement_repeat(stmt_str varchar(500), seq_start bigint, seq_end bigint, step bigint, use_trans int)
        begin
          declare counter bigint;
          declare incr bigint;
          declare max bigint;
          declare multi int default 0;
          declare pos int;
          declare rem_str varchar(500);

          -- ensure sane values
          set seq_start = ifnull(seq_start, 1);
          set counter = seq_start;
          set seq_end = ifnull(seq_end, 1);
          set max = seq_end;
          set step = abs(ifnull(step, 1));
          set incr = step;

          -- user variables visible to statement(s) in stmt_str
          set @now = unix_timestamp();
          set @seq = seq_start;
          set @stmt = stmt_str;

          -- adjust for descending sequence
          if seq_start > seq_end then
            set counter = seq_end;
            set max = seq_start;
            set step = -step;
          end if;

          -- see if we were given multiple statements
          --  they will be tokenized and prepared later
          set multi = locate(';', @stmt);
          if not multi then
            -- have single statement so prepare it once
            prepare tmp_stmt from @stmt;
          end if;

          if use_trans is NULL or use_trans then
            start transaction;
          end if;
          while counter <= max do
            if multi then
              -- tokenize multi-statement string
              set rem_str = stmt_str;
              repeat
                set pos = locate(';', rem_str);
                if pos then
                  set @stmt = substring(rem_str, 1, pos - 1);
                  set rem_str = substring(rem_str, pos + 1);
                else
                  -- last token
                  set @stmt = rem_str;
                  set rem_str = '';
                end if;

                prepare tmp_stmt from @stmt;
                execute tmp_stmt;
              until rem_str = '' end repeat;
            else
              execute tmp_stmt;
            end if;

            set @seq = @seq + step;
            set counter = counter + incr;
          end while;
          if use_trans is NULL or use_trans then
            commit;
          end if;

          deallocate prepare tmp_stmt;
        end //
    """

    slurmdbd_dict = atf.get_config(live=False, source="slurmdbd", quiet=True)
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
        mysql_options += f" -u {atf.properties['slurm-user']}"
    if database_password:
        mysql_options += f" -p {database_password}"
    if not database_name:
        database_name = "slurm_acct_db"
    mysql_options += f" -D {database_name}"

    mysql_command_base = f"{mysql_path}{mysql_options}"
    mysql_command = f'{mysql_command_base} -e "{statement_repeat_sql}"'
    if atf.run_command_exit(mysql_command, quiet=True) != 0:
        logging.debug(f"Slurm accounting database ({database_name}) is not present")

    return mysql_command_base
