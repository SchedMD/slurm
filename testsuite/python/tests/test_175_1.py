############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that MailProg=None disables mail notifications."""

import os

import pytest

import atf

pytestmark = pytest.mark.slow


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup(mail_prog):
    atf.require_version(
        (26, 11),
        reason="Issue 51196: MailProg=None is only supported by 26.11+ slurmctld",
    )
    atf.require_config_parameter("MailProg", mail_prog)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def mail_dir():
    """Directory for the recorder and its record, writable by SlurmUser.

    Resolved once: a test does not run in the directory this is created in.
    """
    mail_dir = os.path.abspath("mail_records")
    os.makedirs(mail_dir, exist_ok=True)
    os.chmod(mail_dir, 0o777)
    yield mail_dir


@pytest.fixture(scope="module")
def mail_record(mail_dir):
    return f"{mail_dir}/mail_prog.out"


@pytest.fixture(scope="module")
def mail_prog(mail_dir, mail_record):
    """A MailProg that appends one line per mail it is called for."""
    mail_prog = f"{mail_dir}/mail_prog.sh"
    atf.make_bash_script(
        mail_prog,
        'echo "SLURM_JOB_ID=$SLURM_JOB_ID'
        f' SLURM_JOB_MAIL_TYPE=$SLURM_JOB_MAIL_TYPE" >> {mail_record}\n',
    )
    return mail_prog


def logged(log_file, text):
    """Return the lines of log_file that contain text.

    grep exits 1 when nothing matches, which is what these checks want, and 2
    when it could not search the file, which must not read as "not logged".
    """
    result = atf.run_command(
        f"grep -F '{text}' {log_file}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    rc = result["exit_code"]
    assert rc in (0, 1), f"Could not search {log_file}: {result['stderr']}"
    return result["stdout"]


def test_mail_prog_none(mail_prog, mail_record):
    """MailProg=None runs no program, so a job that asks for every mail type
    gets no mail and slurmctld does not try to run "None"."""

    mail_user = atf.properties["slurm-user"]

    # Restart so the startup MailProg check runs against "None" too.
    atf.set_config_parameter("MailProg", "None", restart=True)
    # Mail is off, which slurmctld reports back as the configured value.
    assert (
        atf.get_config(live=True).get("MailProg") == "None"
    ), "slurmctld did not accept MailProg=None"

    log_file = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    assert not logged(
        log_file, "Configured MailProg is invalid"
    ), "slurmctld reported MailProg=None as an invalid program"

    atf.submit_job_srun(f"--mail-type=ALL --mail-user={mail_user} true", fatal=True)

    # The same job must send mail once MailProg is set again, so the log check
    # below is not passing only because this job never mails.
    atf.set_config_parameter("MailProg", mail_prog)
    enabled_job_id = atf.submit_job_srun(
        f"--mail-type=ALL --mail-user={mail_user} true", fatal=True
    )
    atf.assert_file_contents(
        mail_record,
        f"SLURM_JOB_ID={enabled_job_id} SLURM_JOB_MAIL_TYPE=Ended",
        contains=True,
        message=f"No end mail recorded for job {enabled_job_id} with MailProg set",
    )

    # Catches "None" being handed to slurmscriptd and run.
    assert not logged(
        log_file, "MailProg returned error"
    ), "slurmctld ran MailProg while it was disabled"
