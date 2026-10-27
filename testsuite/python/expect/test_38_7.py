############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Just to make the test faster
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    atf.require_nodes(5, [("CPUs", 2)])
    atf.require_slurm_running()

    atf.require_lmod()
    atf.module_load("openmpi")


def test_expect():
    reason, rc = atf.run_expect_test(fail=False)
    if rc:
        if (
            atf.get_version("sbin/slurmd") < (25, 11, 2)
            and "No MPI communications occurred" in reason
        ):
            pytest.xfail(
                f"Ticket 24292: PMIx 5.0.8 and 5.0.9 clients hang. Fixed in 25.11.2+. {reason}"
            )
        else:
            pytest.fail(reason)
