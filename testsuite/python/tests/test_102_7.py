############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_version((25, 11), "bin/sacctmgr")
    atf.require_config_parameter("AccountingStorageType", "accounting_storage/slurmdbd")
    atf.require_slurm_running()


def test_modify_job_tres_in_accounting():
    """Test that we can alter a job's TRES after it has already run"""

    def _change_tres(job_id, tres_mod):
        atf.run_command(
            f"sacctmgr -i mod job {job_id} set tres={tres_mod}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        output = atf.run_command_output(
            f"sacct -j{job_id} --format=alloctres -PnX",
            fatal=True,
            quiet=True,
        )

        assert (
            output.find(tres_mod) != -1
        ), f"Job {job_id} doesn't have the right TRES. Expecting {tres_mod} to be part of {output} but it isn't"

        return 0

    # Try to run a job
    job_id = atf.submit_job_srun("-N1 -n1 hostname", fatal=True)
    atf.wait_for_job_state(job_id, "DONE")

    logging.debug("Adding energy to the mix (it is probably not there")
    _change_tres(job_id, "energy=1234")

    logging.debug("Modifying energy to the mix")
    _change_tres(job_id, "energy=12345")
