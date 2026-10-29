############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that #SCRON --export=NIL stops a cron job loading the login environment.

scrontab used to set SLURM_GET_USER_ENV=1 for every entry, so slurmd loaded the
user's login environment even when the entry asked for --export=NIL. sbatch
documents NIL as the mode that skips that load. Requires Slurm 26.11+
(ticket 25969).

slurmd merges the login environment over the submitted one, so the job still
sees SLURM_GET_USER_ENV=1 whenever the load was requested. That variable is
the observable here.

The jobs have to run to be observed: the environment is not visible while a
job is pending, and AccountingStoreFlags=job_env only records it at job start.
Every entry runs each minute, so the test waits for the next minute boundary.
"""

import pytest

import atf

# Fires every minute. Unlike test_169_1, these jobs must actually run.
schedule = "* * * * *"

# NIL leaves the job without a PATH, so the command must be an absolute path.
command = "/usr/bin/env"

# One crontab holds every case so the module waits for one cron minute only.
cases = {
    "cron_169_2_nil": ("--export=NIL", False),
    "cron_169_2_none": ("--export=NONE", True),
    "cron_169_2_default": (None, True),
}

pytestmark = pytest.mark.slow


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Ticket 25969: scrontab honors #SCRON --export=NIL in 26.11",
    )
    atf.require_config_parameter_includes("ScronParameters", "enable")
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def clean_crontab():
    """Guarantees every test both starts and ends with no crontab.

    Removing the crontab cancels the entries' pending jobs, so no entry fires
    again after the module.
    """
    atf.run_command("scrontab -r", quiet=True)
    yield
    atf.run_command("scrontab -r", quiet=True)


def output_path(name):
    """Returns the absolute output file of the cron entry named name."""
    return atf.module_tmp_path / f"{name}.out"


def install_crontab(*lines):
    """Installs a crontab built from lines and fatal if the command fails"""
    crontab = "".join(f"{line}\n" for line in lines)
    atf.run_command("scrontab -", input=crontab, fatal=True)


def read_job_env(name):
    """Returns the environment the cron job named name printed, once it ran.

    The output file exists from the moment the job starts, so wait for a
    variable slurmstepd always sets rather than for the file alone.
    """
    text = ""
    # Up to one cron minute, plus scheduling latency, before the job runs.
    for _ in atf.timer(timeout=150):
        text = atf.run_command_output(f"cat {output_path(name)}", quiet=True)
        if "SLURM_JOB_ID=" in text:
            break
    else:
        pytest.fail(f"Cron job {name} did not run and write its environment")

    return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)


def test_export_nil_skips_the_login_environment():
    """Verify only --export=NIL submits a cron job without SLURM_GET_USER_ENV.

    NONE and an entry with no --export are the controls: both must still ask
    slurmd for the login environment. Without them, a scrontab that never set
    the flag at all would pass the NIL check too.
    """

    lines = []
    for name, (export, _) in cases.items():
        lines.append(f"#SCRON --job-name={name}")
        lines.append(f"#SCRON --output={output_path(name)}")
        if export:
            lines.append(f"#SCRON {export}")
        lines.append(f"{schedule} {command}")

    install_crontab(*lines)

    for name, (export, wants_login_env) in cases.items():
        env = read_job_env(name)
        requested = env.get("SLURM_GET_USER_ENV") == "1"
        if wants_login_env:
            assert (
                requested
            ), f"{export or 'No --export'} should load the login environment"
        else:
            assert not requested, f"{export} should not load the login environment"
