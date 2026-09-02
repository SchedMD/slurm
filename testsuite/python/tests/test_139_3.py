############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that a dynamic future node gets a valid NodeAddr and NodeHostName.

slurm.conf.5 "Dynamic Future Nodes" says that the node's NodeAddr and
NodeHostname are retrieved from the slurmd that registers with "slurmd -F".
"""

import re

import pytest

import atf

node_name = "future1"


@pytest.fixture(scope="module", autouse=True)
def setup(slurmd_c):
    atf.require_version(
        (26, 5, 5),
        "sbin/slurmctld",
        reason="dynamic future node NodeAddr was uninitialized before 26.05.5",
    )
    atf.require_config_parameter(
        "SlurmctldDebug", ["debug2", "debug3", "debug4", "debug5"]
    )

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

    # conftest stops daemons only when it started Slurm itself, which it keys
    # off properties["slurm-started"]. start_slurmctld() does not set that, so
    # stop them here. This also stops the slurmd from "slurmd -F".
    atf.stop_slurmctld(also_slurmds=True)

    # slurmd -F has no NodeName, so it derives one from its own hostname
    # (slurmd.c) and %n/%h in SlurmdSpoolDir expand to that, not to
    # node_name. conftest only cleans spool dirs for configured node names,
    # so remove this one here.
    spool_dir = atf.get_config_parameter("SlurmdSpoolDir", live=False, quiet=True)
    spool_dir = spool_dir.replace("%n", slurmd_c["NodeName"]).replace(
        "%h", slurmd_c["NodeName"]
    )
    atf.run_command(f"rm -rf '{spool_dir}'", user="root", fatal=True)


@pytest.fixture(scope="module")
def slurmd_c():
    """Run 'slurmd -C' once; it reads the hardware and autodetects GPUs."""
    return atf.get_slurmd_C()


def test_dynamic_future_node_addr(slurmd_c):
    """The address taken when the node is mapped must come from the slurmd."""
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -F", user="root", fatal=True
    )
    atf.wait_for_node_state(node_name, "DYNAMIC_FUTURE", fatal=True)

    # The mapping does not set the node record from the slurmd. Only the
    # second registration does, in validate_node_specs(). Wait for a field
    # that function sets, so the record below is the settled one. It also
    # sets NodeAddr, and the write lock makes both visible together.
    settled = None
    for _ in atf.timer(fatal=True):
        if atf.get_node_parameter(node_name, "version"):
            settled = atf.get_node_parameter(node_name, "address")
            break
    assert settled, f"{node_name} never completed a second registration"

    # The mapping address survives nowhere but this line, so read it there.
    log_file = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    mapped = re.search(
        rf"dynamic future node \S+/\S+/(\S+) assigned to node {re.escape(node_name)}",
        atf.run_command_output(
            f"grep -F 'assigned to node {node_name}' {log_file}",
            user=atf.properties["slurm-user"],
        ),
    )
    assert mapped, f"slurmctld did not map a dynamic future node to {node_name}"

    # Compare against the address the slurmd registered with. Testing only
    # for 0.0.0.0 would pass whenever the uninitialized bytes happen to
    # render as some other address.
    assert mapped.group(1) == settled, (
        f"{node_name} was mapped with {mapped.group(1)}, but the slurmd "
        f"registered from {settled}"
    )
    # The slurmd sends the short hostname, from gethostname_short(). Take it
    # from "slurmd -C", which reports the same value, so a host with a domain
    # in its hostname does not fail here.
    assert (
        atf.get_node_parameter(node_name, "hostname") == slurmd_c["NodeName"]
    ), f"{node_name} NodeHostName was not taken from the slurmd"
