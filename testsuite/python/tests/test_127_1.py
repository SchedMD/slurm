############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import os
import pytest
import shutil
import time

epilog_timeout = 10


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set the Epilog")
    atf.require_config_parameter("PrologEpilogTimeout", epilog_timeout)
    atf.require_slurm_running()


def test_epilog(tmp_path):
    """Test Epilog"""

    # Create symlink to help identify pid later
    sleep_symlink = str(tmp_path / f"sleep_symlink_{os.getpid()}")
    os.symlink(shutil.which("sleep"), sleep_symlink)

    # Create epilog script
    epilog = str(tmp_path / "epilog.sh")
    touched_file = str(tmp_path / "touched_file")
    atf.make_bash_script(
        epilog,
        f"""touch {touched_file}
{sleep_symlink} 60
exit 0
""",
    )
    atf.set_config_parameter("Epilog", epilog)

    # Verify that the epilog ran by checking for the file creation
    job_id = atf.submit_job_srun(f"-t1 true", fatal=True)
    assert atf.wait_for_file(touched_file), f"File ({touched_file}) was not created"

    # Verify that the job enters the completing state
    assert atf.wait_for_job_state(job_id, "COMPLETING", fatal=True)

    # Verify that the child processes of the epilog is still running
    assert (
        atf.run_command_exit(f"pgrep -f {sleep_symlink}") == 0
    ), "The epilog child process should continue running after the epilog completes (until the the epilog timeout)"

    # After the epilog timeout, the job should complete and the process should be killed

    # Verify that the job enters the completed state
    assert atf.wait_for_job_state(
        job_id, "DONE", fatal=True, timeout=epilog_timeout + 5
    )

    # Verify that the child processes of the epilog has been killed
    assert (
        atf.run_command_exit(f"pgrep -f {sleep_symlink}", xfail=True) != 0
    ), "The epilog child process should have been killed after the epilog timeout"
