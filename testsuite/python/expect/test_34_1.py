############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter("PreemptType", "preempt/partition_prio")
    atf.require_config_parameter("PreemptMode", "requeue")

    # Just to make the test faster
    atf.require_config_parameter_includes("SchedulerParameters", "requeue_delay=5")

    atf.require_nodes(1)
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
