############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(128)
    atf.require_slurm_running()


def test_srun_increasing_job_sizes():
    """Spawn srun immediate jobs with ever larger node counts"""

    if atf.get_config_parameter('FrontendName') != None:
        max_node_cnt = 2
    else:
        max_node_cnt = 1024

    good_errors = ["Immediate execution impossible", "Unable to allocate resources", "Too many open files"]
    node_count = 1
    while node_count < max_node_cnt:
        result = atf.run_job(f"--immediate -c1 -N{node_count} printenv SLURMD_NODENAME")
        if result['exit_code'] != 0:
            good_error_flag = False
            for good_error in good_errors:
                if good_error in result['stderr']:
                    good_error_flag = True
                    break
            if good_error_flag == False:
                pytest.fail(f"Unexpect error occoured: {result['stderr']}")
        node_count *= 2
