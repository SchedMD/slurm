############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SlurmctldParameters=dynamic_node_dns with a slurmd -F node.

slurm.conf.5 says the option applies to nodes started with "slurmd -F" as well
as "slurmd -Z". test_139_6 covers -Z. A FUTURE NodeName has to be in slurm.conf
before slurmctld starts, and "slurmd -F" takes whichever FUTURE node it is
given, so this needs a cluster of its own with only that node configured.
"""

import pytest

import atf

node_name = "ddns-no-such-future"

# A resolver with a wildcard or a search domain would answer for node_name and
# the test would be meaningless.
pytestmark = pytest.mark.skipif(
    atf.run_command_exit(f"getent hosts {node_name}", quiet=True) == 0,
    reason=f"the resolver answers for {node_name}",
)


@pytest.fixture(scope="module", autouse=True)
def setup(slurmd_c):
    atf.require_version(
        (26, 11), "bin/srun", reason="dynamic_node_dns was added in 26.11"
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("SlurmctldParameters", "dynamic_node_dns")
    atf.require_config_parameter(
        "NodeName",
        {
            node_name: {
                "State": "FUTURE",
                "CPUs": slurmd_c["CPUs"],
                "Boards": slurmd_c["Boards"],
                "SocketsPerBoard": slurmd_c["SocketsPerBoard"],
                "CoresPerSocket": slurmd_c["CoresPerSocket"],
                "ThreadsPerCore": slurmd_c["ThreadsPerCore"],
            },
        },
    )
    atf.require_config_parameter(
        "PartitionName", {"primary": {"Nodes": "ALL", "Default": "YES"}}
    )
    # Only a FUTURE node is configured, so require_slurm_running() has no
    # slurmd to start and would wait for a node that cannot become IDLE.
    atf.start_slurmctld(clean=True)

    yield

    # conftest stops daemons only when it started Slurm itself, so stop them
    # here. This also stops the slurmd from "slurmd -F".
    atf.stop_slurmctld(also_slurmds=True)


@pytest.fixture(scope="module")
def slurmd_c():
    """Run 'slurmd -C' once; it reads the hardware and autodetects GPUs."""
    return atf.get_slurmd_C()


def test_future_node_needs_the_nodename_to_resolve():
    """srun must resolve a slurmd -F node's NodeName, and fail when it cannot."""
    # slurmctld assigns node_name, the only FUTURE node, to this slurmd -F
    atf.start_slurmd(node_name, "-F")
    atf.wait_for_node_state(node_name, "IDLE", fatal=True)

    result = atf.run_command(
        f"srun --mpi=none -t1 -N1 -w {node_name} -o /dev/null /bin/true",
        xfail=True,
    )

    assert (
        result["exit_code"] != 0
    ), f"srun reached {node_name} even though its NodeName does not resolve"
    assert f"can't resolve {node_name}" in result["stderr"], (
        f"srun failed, but not because the NodeName would not resolve: "
        f"{result['stderr']}"
    )
