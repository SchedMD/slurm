############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Node reboot Reason survives a later job allocation (ticket 25784).

make_node_alloc() unconditionally cleared node_ptr->reason whenever a job
was allocated to a node, even while REBOOT_REQUESTED/REBOOT_ISSUED/DRAIN was
still pending. On a shared (Exclusive=NO) node, any job landing on the node
after `scontrol reboot ... reason=...` silently erased the reason, while the
REBOOT_REQUESTED state flag itself remained set -- so `scontrol show node`
stopped reporting why the node was rebooting, even though the reboot was
still outstanding.

Requires: a node with >=2 CPUs so a second job can land on the same node
while the first job (and the pending reboot) are still active, and a
RebootProgram so `scontrol reboot` is accepted.
"""

import pytest

import atf

node_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name

    atf.require_version(
        (26, 5, 6),
        "sbin/slurmctld",
        reason="Ticket 25784: node reboot Reason cleared by make_node_alloc()"
        " on any subsequent job allocation to the node",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    node_name = atf.require_nodes(1, [("CPUs", 2), ("RealMemory", 20)])[0]
    atf.require_config_parameter("RebootProgram", "/bin/true")
    atf.require_slurm_running()


def test_reboot_reason_survives_second_job_allocation():
    """Reason set by `scontrol reboot ... reason=` must survive a second
    job landing on the same (shared) node while the reboot is pending."""

    # Job 1 takes half the node, leaving the other CPU schedulable for job 2.
    job_id_1 = atf.submit_job_sbatch(
        f"-w {node_name} -n1 --mem=5 --time=10 --job-name=test_141_12_job1 "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id_1, "RUNNING", fatal=True)

    # Request a reboot with a reason while the node is still in use.
    atf.run_command(
        f"scontrol reboot nextstate=down reason=UPDATE {node_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node_name, "REBOOT_REQUESTED", fatal=True)

    reason_before = atf.get_node_parameter(node_name, "reason")
    assert reason_before and "UPDATE" in reason_before, (
        "Reason should be UPDATE immediately after the reboot request, "
        f"got {reason_before!r}"
    )

    # Job 2 lands on the node's remaining CPU while the reboot is pending --
    # this is the allocation that used to wipe the reason via
    # make_node_alloc().
    job_id_2 = atf.submit_job_sbatch(
        f"-w {node_name} -n1 --mem=5 --time=10 --job-name=test_141_12_job2 "
        "--wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id_2, "RUNNING", fatal=True)

    node_state = atf.get_node_parameter(node_name, "state")
    assert "REBOOT_REQUESTED" in node_state, (
        "Node should still show REBOOT_REQUESTED after the second job "
        f"starts, got State={node_state!r}"
    )

    reason_after = atf.get_node_parameter(node_name, "reason")
    assert reason_after and "UPDATE" in reason_after, (
        "Reason should still be UPDATE after a second job is allocated to "
        f"the node while a reboot is pending (this is ticket 25784), got "
        f"{reason_after!r}"
    )
