############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(1, [("CPUs", 2), ("Gres", "gpu:1")])

    atf.require_config_file(
        "gres.conf",
        "Autodetect=nvml",
    )
    atf.require_slurm_running()


@pytest.mark.skip("Requires Autodetect=nvml")
def test_expect():
    atf.run_expect_test()
