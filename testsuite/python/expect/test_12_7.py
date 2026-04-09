############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Just to make this test faster
    atf.require_config_parameter_includes("SchedulerParameters", "requeue_delay=15")

    atf.require_accounting()
    atf.require_config_parameter("JobRequeue", 1)
    atf.require_nodes(1)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
