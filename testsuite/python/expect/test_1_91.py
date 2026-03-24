############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_expect()

    # Use the actual hardware config
    box = atf.get_slurmd_C()

    atf.require_config_parameter("TaskPlugin", "task/affinity")
    atf.require_nodes(
        1,
        [
            ("CPUS", box["CPUs"]),
            ("Sockets", int(box["Boards"]) * int(box["SocketsPerBoard"])),
            ("CoresPerSocket", box["CoresPerSocket"]),
            ("ThreadsPerCore", box["ThreadsPerCore"]),
        ],
    )
    atf.require_slurm_running()


def test_expect():
    atf.run_expect_test()
