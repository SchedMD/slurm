############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A ResumeTimeout too large for slurmscriptd does not kill the ResumeProgram.

slurmscriptd waits for a power save program in milliseconds, in an int, which
holds at most 2147483 seconds (about 24.9 days). A larger ResumeTimeout used to
overflow that conversion. 4294968 seconds is 704 milliseconds after the int wraps
around, so the old code killed the ResumeProgram almost at once.
"""

import os

import pytest

import atf

# More than the 2147483 seconds an int number of milliseconds can hold, and 704
# milliseconds once the conversion wraps around
resume_timeout = 4294968
# The ResumeProgram outlasts the wrapped timeout, and writes the marker file only
# if it was not killed first
resume_duration = 5


def marker_file():
    return atf.module_tmp_path / "marker" / "resume_done"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Everything that reads slurm.conf must accept the large ResumeTimeout
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 51038: ResumeTimeout above 65533 seconds added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51038: ResumeTimeout above 65533 seconds added in 26.11",
    )
    if not atf.properties["auto-config"]:
        pytest.skip("Needs auto-config to rewrite the power save configuration")

    # The ResumeProgram runs as SlurmUser, so give it a directory it can write
    marker_file().parent.mkdir()
    marker_file().parent.chmod(0o777)

    resume_script = os.path.abspath("resume.sh")
    atf.make_bash_script(
        resume_script,
        f"""
sleep {resume_duration}
touch {marker_file()}
""",
    )

    atf.require_nodes(1)
    # SuspendTime has to be a real value: INFINITE turns automatic power save
    # off. Make it long enough that no node suspends on its own.
    atf.require_config_parameter("SuspendTime", 3600)
    atf.require_config_parameter("SuspendTimeout", 5)
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeProgram", resume_script)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_slurm_running()


def test_resume_program_not_killed():
    """The ResumeProgram runs to completion when ResumeTimeout is too large for
    slurmscriptd to enforce."""

    node_name = atf.properties["nodes"][0]
    scontrol = {"user": atf.properties["slurm-user"], "fatal": True}
    marker_file().unlink(missing_ok=True)

    atf.run_command(
        f"scontrol update nodename={node_name} state=POWER_DOWN", **scontrol
    )
    assert atf.wait_for_node_state(
        node_name, "POWERED_DOWN", timeout=30
    ), f"{node_name} should finish powering down"

    atf.run_command(f"scontrol update nodename={node_name} state=POWER_UP", **scontrol)
    assert atf.wait_for_file(
        marker_file(), timeout=resume_duration + 20
    ), "The ResumeProgram should not be killed before it finishes"
