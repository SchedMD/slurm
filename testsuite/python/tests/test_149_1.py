############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import logging
import re
import math


program = "acct_gather"

job_rss = 200  # MB
sleep_secs = 8


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():

    atf.require_influxdb()
    atf.require_slurm_running()


def test_influxdb(use_memory_program):
    """Test base AcctGatherProfileType= acct_gather_profile/influxdb"""

    # Submit a job that will use around job_rss MB
    jobid = atf.submit_job_srun(
        f"--acctg-freq=1 --profile=task -t 1 {use_memory_program} {job_rss} {sleep_secs}",
        fatal=True,
    )

    logging.info(f"Retrieving InfluxDB metrics for job {jobid}")
    output = atf.request_influxdb(
        f"select job,max(value) from \"RSS\" where job = '{jobid}' AND step = '0' AND time > now() - 1h"
    )
    match = re.search(rf"\b{jobid}\b\s+(\d+)\s*$", output)

    # Checking that the gathered values are in tolerance range
    assert match is not None, "influxdb should have data from job {job_id}"
    assert math.isclose(int(match.group(1)) / 1024, job_rss, abs_tol=20)
