############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re
import time


# Setup
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_config_parameter('FrontendName', None)
    atf.require_auto_config("wants to set the mail program")
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def mail_program_out():
    mail_tmp_dir = str(atf.module_tmp_path)
    mail_program = str(atf.module_tmp_path / 'mail_program.sh')
    mail_program_out = str(atf.module_tmp_path / 'mail_program.out')

    os.chmod(mail_tmp_dir, 0o777)
    atf.make_bash_script(mail_program, rf"""echo SLURM_JOB_ID=$SLURM_JOB_ID SLURM_JOB_USER=$SLURM_JOB_USER SLURM_JOB_MAIL_TYPE=$SLURM_JOB_MAIL_TYPE >> {mail_program_out}""")
    os.chmod(mail_program, 0o777)
    atf.set_config_parameter('MailProg', str(mail_program))

    return mail_program_out


def test_mail_type_and_mail_user(mail_program_out):
    """Test of mail options (--mail-type and --mail-user options)."""

    slurm_user = 'slurm-user'
    job_id = atf.run_job_id(f"--mail-type=all --mail-user={atf.properties[slurm_user]} id")
    atf.wait_for_file(mail_program_out, fatal=True)
    output = atf.run_command_output(f"cat {mail_program_out}")
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Began", output) is not None, "Start mail not sent"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Ended", output) is not None, "End mail not sent"

    job_id = atf.submit_job(f'--wrap="srun --mail-type=END --mail-user={atf.properties[slurm_user]} sleep 100"')
    atf.wait_for_job_state(job_id, 'RUNNING')
    atf.run_command(f"scancel {job_id}")
    output = atf.run_command_output(f"cat {mail_program_out}")
    assert re.findall(rf"SLURM_JOB_ID=\d+ SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Ended", output) is not None, "End mail not sent after job was canceled"

    job_id = atf.run_job_id(f"-t1 --mail-type=ALL,TIME_LIMIT,TIME_LIMIT_90,TIME_LIMIT_80,TIME_LIMIT_50 --mail-user={atf.properties[slurm_user]} sleep 300", timeout=120, xfail=True)
    time.sleep(5)
    output = atf.run_command_output(f"cat {mail_program_out}")
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Began", output) is not None, "Start mail not sent for timeout test"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Failed", output) is not None, "Failed mail not sent for timeout test"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Reached time limit", output) is not None, "Time limit mail not sent for timeout test"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Reached 90% of time limit", output) is not None, "Reached 90% of time limit mail not sent for timeout test"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Reached 80% of time limit", output) is not None, "Reached 80% of time limit mail not sent for timeout test"
    assert re.findall(rf"SLURM_JOB_ID={job_id} SLURM_JOB_USER={atf.properties[slurm_user]} SLURM_JOB_MAIL_TYPE=Reached 50% of time limit", output) is not None, "Reached 50% of time limit mail not sent for timeout test"
