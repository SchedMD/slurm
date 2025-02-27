############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import inspect
import logging
import os
import pathlib
import pwd
import pytest
import _pytest
import re
import shutil
import sys

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


def color_log_level(level, **color_kwargs):
    # Adapted from depricated py.io TerminalWriter source
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

                formatter._level_to_fmt_mapping[
                    level
                ] = formatter.LEVELNAME_FMT_REGEX.sub(
                    colorized_formatted_levelname, formatter._fmt
                )


@pytest.fixture(scope="session", autouse=True)
def session_setup(request):
    # Set the auto-config and other properties from the opetions
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


def update_tmp_path_exec_permissions():
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

    user_name = atf.get_user_name()
    path = f"/tmp/pytest-of-{user_name}"

    if os.path.isdir(path):
        os.chmod(path, 0o777)
        for root, dirs, files in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), 0o777)


@pytest.fixture(scope="function", autouse=True)
def tmp_path_setup(request):
    update_tmp_path_exec_permissions()


@pytest.fixture(scope="module", autouse=True)
def module_setup(request, tmp_path_factory):
    atf.properties["slurm-started"] = False
    atf.properties["slurmrestd-started"] = False
    atf.properties["configurations-modified"] = set()
    atf.properties["orig-environment"] = dict(os.environ)
    atf.properties["orig-pypath"] = list(sys.path)

    # Creating a module level tmp_path mimicing what tmp_path does
    name = request.node.name
    name = re.sub(r"[\W]", "_", name)
    name = name[:30]
    atf.module_tmp_path = tmp_path_factory.mktemp(name, numbered=True)
    update_tmp_path_exec_permissions()

    # Module-level fixtures should run from within the module_tmp_path
    os.chdir(atf.module_tmp_path)

    # Stop Slurm if using auto-config and Slurm is already running
    if atf.properties["auto-config"] and atf.is_slurmctld_running(quiet=True):
        logging.warning(
            "Auto-config requires Slurm to be initially stopped but Slurm was found running. Stopping Slurm"
        )
        atf.stop_slurm(quiet=True)

    # Cleanup StateSaveLocation for auto-config
    if atf.properties["auto-config"]:
        statesaveloc = atf.get_config_parameter(
            "StateSaveLocation", live=False, quiet=True
        )
        if os.path.exists(statesaveloc):
            if os.path.exists(statesaveloc + name):
                logging.warning(
                    f"Backup for StateSaveLocation already exists ({statesaveloc+name}). Removing it."
                )
                atf.run_command(f"rm -rf {statesaveloc+name}", user="root", quiet=True)
            atf.run_command(
                f"mv {statesaveloc} {statesaveloc+name}", user="root", quiet=True
            )

    yield

    # Return to the folder from which pytest was executed
    os.chdir(request.config.invocation_dir)

    # Teardown
    module_teardown()

    # Restore StateSaveLocation for auto-config
    if atf.properties["auto-config"]:
        atf.run_command(f"rm -rf {statesaveloc}", user="root", quiet=True)
        if os.path.exists(statesaveloc + name):
            atf.run_command(
                f"mv {statesaveloc+name} {statesaveloc}", user="root", quiet=True
            )


def module_teardown():
    failures = []

    if atf.properties["auto-config"]:
        if atf.properties["slurm-started"] == True:
            # Cancel all jobs
            if not atf.cancel_all_jobs(quiet=True):
                failures.append("Not all jobs were successfully cancelled")

            # Stop Slurm if we started it
            if not atf.stop_slurm(fatal=False, quiet=True):
                failures.append("Not all Slurm daemons were successfully stopped")

        # Restore any backed up configuration files
        for config in set(atf.properties["configurations-modified"]):
            atf.restore_config_file(config)

        # Restore the Slurm database
        atf.restore_accounting_database()

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
    monkeypatch.chdir(tmp_path)


@pytest.fixture(scope="class", autouse=True)
def class_setup(request):
    # Log class docstring (test description) if present
    if request.cls.__doc__ is not None:
        logging.info(request.cls.__doc__)


def pytest_fixture_setup(fixturedef, request):
    # Log fixture docstring when invoked if present
    if fixturedef.func.__doc__ is not None:
        logging.info(fixturedef.func.__doc__)


def pytest_keyboard_interrupt(excinfo):
    """Called for keyboard interrupt"""
    module_teardown()
