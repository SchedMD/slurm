############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify hetjobs with external components run, end and survive restarts on external nodes."""

import re

import pytest

import atf

# Enough external nodes for every component of the hetjobs that
# test_concurrent_external_hetjobs runs at once, so that test does not depend
# on CR_CPU packing their -n1 components onto shared nodes.
component_count = 3
concurrent_count = 2
node_names = [f"extnode{i}" for i in range(1, component_count * concurrent_count + 1)]

# The external nodes live in their own partition so an external job targets
# only them; the compute node with a slurmd stays in the default partition.
partition_name = "external"

# The one node that runs a slurmd, for the regular leader of mixed hetjobs
compute_node = "node1"

# Printed by the mixed hetjob leader's batch script, so the test can tell that
# the script actually ran rather than that the job merely held an allocation
batch_marker = "mixed_leader_ran"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Gating sbatch covers slurmctld, which is always upgraded first
    atf.require_version(
        (26, 5, 3),
        "bin/sbatch",
        reason="Issue 50997: external hetjobs are available in 26.05.3",
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than append: the CR_* allocation types are mutually
    # exclusive, so adding CR_CPU to a config already setting CR_Core_Memory
    # yields "cr_core_memory,CR_CPU", which slurmctld rejects.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # Only the backfill scheduler starts hetjobs, so every job here waits on a
    # backfill cycle. Shorten it rather than spend the session budget waiting.
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    # Either one makes "scancel <leader>+1" cancel every component
    atf.require_config_parameter_excludes("SchedulerParameters", "whole_hetjob")
    atf.require_config_parameter_excludes("SchedulerParameters", "whole_pack")

    # This module starts no slurmdbd, so finished jobs are checked through
    # scontrol
    atf.require_accounting(enabled=False)
    atf.require_config_parameter("MinJobAge", 300, ">=")

    # Define the EXTERNAL nodes statically rather than creating them with
    # "scontrol create". A dynamically created node is also flagged
    # DYNAMIC_NORM, which sends the allocation path down the alias list and
    # address lookup code that has nothing to resolve for an external node.
    # The compute node needs NodeAddr: slurmctld has to open connections to
    # its slurmd, and the bare node name does not resolve here. The external
    # nodes deliberately get no address, since nothing should ever try to
    # contact a node that has no slurmd.
    node_config = {compute_node: {"CPUs": 4, "NodeAddr": "localhost"}}
    for node_name in node_names:
        node_config[node_name] = {"CPUs": 4, "State": "EXTERNAL"}
    atf.require_config_parameter("NodeName", node_config)

    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": compute_node, "Default": "YES"},
            partition_name: {"Nodes": ",".join(node_names)},
        },
    )

    # require_slurm_running() starts a slurmd for every NodeName line, but an
    # EXTERNAL node has none by definition. Start the controller, then only
    # the compute node's slurmd, as test_141_1 does for static CLOUD nodes.
    atf.start_slurmctld(clean=True)
    atf.start_slurmd(compute_node, wait="idle")

    yield

    # Not fatal: scancel refuses a pending non-leader hetjob component
    atf.cancel_all_jobs(quiet=True)

    # stop_slurmctld() reports failures by returning them rather than failing
    failures = atf.stop_slurmctld(also_slurmds=True)
    assert failures is None, f"Failures stopping the daemons: {failures}"


@pytest.fixture(autouse=True)
def cancel_jobs_after_test():
    """Cancel each test's jobs so the next one finds the nodes free.

    Not fatal: scancel refuses a pending non-leader hetjob component.
    """
    yield
    atf.cancel_all_jobs(quiet=True)


def get_components(leader_job_id, count):
    """Return the leader's count component job ids, sorted, leader first."""
    het_job_expression = atf.get_job_parameter(leader_job_id, "HetJobIdSet", fatal=True)
    assert het_job_expression is not None, "Leader is missing HetJobIdSet"

    component_list = sorted(atf.range_to_list(het_job_expression))
    assert (
        len(component_list) == count
    ), f"Expected {count} components, got {component_list}"
    assert (
        component_list[0] == leader_job_id
    ), f"Leader {leader_job_id} is not the lowest component id in {component_list}"
    return component_list


def verify_external_hetjob(leader_job_id):
    """Assert the leader owns component_count components, each running on an
    external node.

    Returns the component job ids in offset order and the jobs they were
    checked against.
    """
    component_list = get_components(leader_job_id, component_count)

    # Wait for every component to run
    for component_job_id in component_list:
        atf.wait_for_job_state(component_job_id, "RUNNING", fatal=True)

    jobs_dict = atf.get_jobs(fatal=True)

    for offset, component_job_id in enumerate(component_list):
        assert component_job_id in jobs_dict, f"Component {component_job_id} not found"
        component_dict = jobs_dict[component_job_id]
        assert (
            component_dict["HetJobId"] == leader_job_id
        ), f"Component {component_job_id} HetJobId != leader {leader_job_id}"
        assert (
            component_dict["HetJobOffset"] == offset
        ), f"Component {component_job_id} HetJobOffset != {offset}"
        assert (
            component_dict["JobState"] == "RUNNING"
        ), f"Component {component_job_id} is not RUNNING"

        # Each component must have landed on external nodes. NodeList is a
        # hostlist expression, so expand it rather than comparing it whole.
        for component_node in atf.node_range_to_list(component_dict["NodeList"]):
            assert component_node in node_names, (
                f"Component {component_job_id} ran on {component_node}, "
                f"expected an external node ({node_names})"
            )
            node_state = atf.get_node_parameter(component_node, "state")
            assert (
                "EXTERNAL" in node_state
            ), f"Node {component_node} is not in EXTERNAL state"
            assert (
                "IDLE" not in node_state
            ), f"Node {component_node} is IDLE under component {component_job_id}"

    return component_list, jobs_dict


def hetjob_snapshot(leader_job_id):
    """Return what a controller restart must preserve of each component."""
    jobs_dict = atf.get_jobs(fatal=True)
    assert leader_job_id in jobs_dict, f"Leader {leader_job_id} not found"
    het_job_expression = jobs_dict[leader_job_id].get("HetJobIdSet")
    assert het_job_expression is not None, "Leader is missing HetJobIdSet"

    component_list = atf.range_to_list(het_job_expression)
    missing = set(component_list) - set(jobs_dict)
    assert not missing, f"Components {missing} of {leader_job_id} not found"

    return {
        component_job_id: {
            field: jobs_dict[component_job_id].get(field)
            for field in ("HetJobId", "HetJobOffset", "JobState", "NodeList")
        }
        for component_job_id in component_list
    }


def slurmd_start_time(node_name):
    """Return the slurmd_start_time slurmctld reports for node_name, or None."""
    start_time = atf.get_node_parameter(node_name, "slurmd_start_time")
    if isinstance(start_time, dict):
        start_time = start_time.get("number")
    return start_time or None


def backfill_cycles():
    """Return how many backfill cycles slurmctld has completed."""
    output = atf.run_command_output("sdiag", fatal=True, quiet=True)
    match = re.search(r"Backfilling stats.*?Total cycles:\s*(\d+)", output, re.S)
    assert match, f"No backfill cycle count in sdiag output:\n{output}"
    return int(match.group(1))


def test_external_hetjob():
    """
    An external hetjob submits and runs on external nodes via sbatch.
    """

    args = " : ".join([f"--external -n1 -t5 -p {partition_name}"] * component_count)
    leader_job_id = atf.submit_job_sbatch(args, fatal=True)

    atf.wait_for_job_state(leader_job_id, "RUNNING", fatal=True)

    verify_external_hetjob(leader_job_id)


def test_concurrent_external_hetjobs():
    """
    Two external hetjobs run at the same time without interfering.

    Both are RUNNING in one snapshot, and each keeps its own components, every
    one running on an external node.
    """

    args = " : ".join([f"--external -n1 -t5 -p {partition_name}"] * component_count)
    leader_job_ids = [
        atf.submit_job_sbatch(args, fatal=True) for _ in range(concurrent_count)
    ]

    for leader_job_id in leader_job_ids:
        atf.wait_for_job_state(leader_job_id, "RUNNING", fatal=True)

    # One snapshot, so this shows the hetjobs running at the same time rather
    # than each having run at some point.
    jobs_dict = atf.get_jobs(fatal=True)
    for leader_job_id in leader_job_ids:
        assert (
            jobs_dict[leader_job_id]["JobState"] == "RUNNING"
        ), f"Leader {leader_job_id} is not RUNNING alongside the others"

    for leader_job_id in leader_job_ids:
        verify_external_hetjob(leader_job_id)


def test_sbatch_mixed_hetjob():
    """
    A mixed hetjob submitted by sbatch runs, leader's batch script and all.

    test_105_22 inspects this shape while it is still pending; this runs it.

    The script trails every component, even though it belongs to the leader:
    sbatch takes each component's options from the front of the command line
    after trimming the script's arguments off the end.
    """

    script_name = "sbatch_mixed_leader.sh"
    atf.make_bash_script(script_name, f"echo {batch_marker}\nsleep infinity\n")

    leader_job_id = atf.submit_job_sbatch(
        f"-n1 -t5 -p primary"
        f" : -n1 -t5 --external -p {partition_name} {script_name}",
        fatal=True,
    )

    component_list = get_components(leader_job_id, 2)

    for component_job_id in component_list:
        atf.wait_for_job_state(component_job_id, "RUNNING", fatal=True)

    jobs_dict = atf.get_jobs(fatal=True)

    leader_nodes = atf.node_range_to_list(jobs_dict[leader_job_id]["NodeList"])
    assert leader_nodes, "Leader was allocated no nodes"
    for leader_node in leader_nodes:
        assert (
            leader_node not in node_names
        ), f"Leader ran on external node {leader_node}"

    external_nodes = atf.node_range_to_list(jobs_dict[component_list[1]]["NodeList"])
    assert external_nodes, "External component was allocated no nodes"
    for external_node in external_nodes:
        assert (
            external_node in node_names
        ), f"External component ran on {external_node}, expected one of {node_names}"

    atf.assert_file_contents(
        jobs_dict[leader_job_id]["StdOut"],
        batch_marker,
        contains=True,
        message=f"Leader {leader_job_id} never ran its batch script",
    )


def test_external_hetjob_component_cancel():
    """
    Cancelling one component of a running external hetjob ends only that
    component and returns its node to IDLE, still EXTERNAL.
    """

    args = " : ".join([f"--external -n1 -t5 -p {partition_name}"] * component_count)
    leader_job_id = atf.submit_job_sbatch(args, fatal=True)
    component_list, jobs_dict = verify_external_hetjob(leader_job_id)

    cancelled_job_id = component_list[1]
    sibling_job_ids = [component_list[0], component_list[2]]
    cancelled_nodes = atf.node_range_to_list(jobs_dict[cancelled_job_id]["NodeList"])
    sibling_nodes = {
        node
        for job_id in sibling_job_ids
        for node in atf.node_range_to_list(jobs_dict[job_id]["NodeList"])
    }
    # The node can only go idle if no sibling is on it
    assert not set(cancelled_nodes) & sibling_nodes, (
        f"Component {cancelled_job_id} on {cancelled_nodes} shares a node with "
        f"its siblings on {sibling_nodes}"
    )

    atf.run_command(f"scancel {leader_job_id}+1", fatal=True)
    atf.wait_for_job_state(cancelled_job_id, "CANCELLED", fatal=True)

    # Exactly IDLE and EXTERNAL, so a lingering COMPLETING fails it too
    atf.wait_for_node_state(
        cancelled_nodes,
        ["IDLE", "EXTERNAL"],
        operator=atf.SetMatch.EQUAL,
        fatal=True,
    )

    jobs_dict = atf.get_jobs(fatal=True)
    for sibling_job_id in sibling_job_ids:
        assert jobs_dict[sibling_job_id]["JobState"] == "RUNNING", (
            f"Component {sibling_job_id} is {jobs_dict[sibling_job_id]['JobState']} "
            f"after {leader_job_id}+1 was cancelled"
        )


def test_mixed_hetjob_completes():
    """
    A mixed hetjob ends when its leader's script exits: every component
    completes and the external node goes back to IDLE. The script sees the
    external component as het group 1, though that component has no script.
    """

    script_name = "mixed_leader_exits.sh"
    atf.make_bash_script(script_name, f"env\necho {batch_marker}\n")

    leader_job_id = atf.submit_job_sbatch(
        f"-n1 -t5 -p primary"
        f" : -n1 -t5 --external -p {partition_name} {script_name}",
        fatal=True,
    )
    component_list = get_components(leader_job_id, 2)
    external_job_id = component_list[1]

    # The script exits as soon as it starts, so RUNNING can come and go
    # between two polls. Wait for the end state directly.
    for component_job_id in component_list:
        atf.wait_for_job_state(component_job_id, "COMPLETED", fatal=True)

    jobs_dict = atf.get_jobs(fatal=True)

    external_nodes = atf.node_range_to_list(jobs_dict[external_job_id]["NodeList"])
    assert external_nodes, "External component was allocated no nodes"
    for external_node in external_nodes:
        assert (
            external_node in node_names
        ), f"External component ran on {external_node}, expected one of {node_names}"
    atf.wait_for_node_state(
        external_nodes, ["IDLE", "EXTERNAL"], operator=atf.SetMatch.EQUAL, fatal=True
    )

    stdout_path = jobs_dict[leader_job_id]["StdOut"]
    atf.assert_file_contents(
        stdout_path,
        batch_marker,
        contains=True,
        message=f"Leader {leader_job_id} never ran its batch script",
    )
    output = atf.run_command_output(f"cat {stdout_path}", fatal=True)

    expected = {
        "SLURM_HET_SIZE": len(component_list),
        "SLURM_JOB_ID_HET_GROUP_1": external_job_id,
        "SLURM_JOB_NODELIST_HET_GROUP_1": jobs_dict[external_job_id]["NodeList"],
    }
    for name, value in expected.items():
        assert re.search(
            rf"^{name}={re.escape(str(value))}$", output, re.MULTILINE
        ), f"Missing {name}={value} in the leader's environment"


def test_external_nodes_reused():
    """
    A hetjob pinned to the nodes of a running external hetjob waits for them,
    then runs on exactly those nodes once the first one is cancelled.
    """

    # --exclusive so each component holds its whole node; a -n1 component
    # would leave CPUs the second hetjob could start on alongside it
    first_args = " : ".join(
        [f"--external --exclusive -n1 -t5 -p {partition_name}"] * component_count
    )
    first_leader_job_id = atf.submit_job_sbatch(first_args, fatal=True)
    first_component_list, jobs_dict = verify_external_hetjob(first_leader_job_id)
    first_nodes = [
        jobs_dict[component_job_id]["NodeList"]
        for component_job_id in first_component_list
    ]

    second_args = " : ".join(
        f"--external --exclusive -n1 -t5 -p {partition_name} -w {node}"
        for node in first_nodes
    )
    second_leader_job_id = atf.submit_job_sbatch(second_args, fatal=True)
    second_component_list = get_components(second_leader_job_id, component_count)

    # Only backfill starts hetjobs, and the submission already set Reason, so
    # count cycles instead. The first may have begun before the submission.
    first_cycle = backfill_cycles()
    # Poll at the bf_interval cadence rather than every timeout/10
    for _ in atf.timer(poll_interval=1):
        if backfill_cycles() - first_cycle >= 2:
            break
    else:
        pytest.fail(f"Backfill never evaluated hetjob {second_leader_job_id}")
    jobs_dict = atf.get_jobs(fatal=True)
    for component_job_id in second_component_list:
        assert jobs_dict[component_job_id]["JobState"] == "PENDING", (
            f"Component {component_job_id} is "
            f"{jobs_dict[component_job_id]['JobState']} while {first_nodes} are "
            f"held by hetjob {first_leader_job_id}"
        )

    atf.run_command(f"scancel {first_leader_job_id}", fatal=True)
    for component_job_id in first_component_list:
        atf.wait_for_job_state(component_job_id, "CANCELLED", fatal=True)

    for component_job_id in second_component_list:
        atf.wait_for_job_state(component_job_id, "RUNNING", fatal=True)

    jobs_dict = atf.get_jobs(fatal=True)
    for offset, component_job_id in enumerate(second_component_list):
        second_node = jobs_dict[component_job_id]["NodeList"]
        assert second_node == first_nodes[offset], (
            f"Component {component_job_id} ran on {second_node}, expected "
            f"{first_nodes[offset]} from cancelled hetjob {first_leader_job_id}"
        )


@pytest.mark.parametrize("shape", ["external", "mixed", "held"])
def test_hetjob_survives_controller_restart(shape):
    """
    A hetjob with external components keeps every component across a
    controller restart, whether running or held, and a held one runs once
    released.

    An external component has no script for slurmctld to save, so recovery
    must keep a pending external leader that has none.
    """

    if shape == "mixed":
        script_name = "restart_mixed_leader.sh"
        atf.make_bash_script(script_name, f"echo {batch_marker}\nsleep infinity\n")
        leader_job_id = atf.submit_job_sbatch(
            f"-n1 -t5 -p primary"
            f" : -n1 -t5 --external -p {partition_name} {script_name}",
            fatal=True,
        )
        for component_job_id in get_components(leader_job_id, 2):
            atf.wait_for_job_state(component_job_id, "RUNNING", fatal=True)

        # Restart only once the script runs, so its launch is not left to the
        # restarted controller
        atf.assert_file_contents(
            atf.get_job_parameter(leader_job_id, "StdOut", fatal=True),
            batch_marker,
            contains=True,
            message=f"Leader {leader_job_id} never ran its batch script",
        )

        registered_start_time = slurmd_start_time(compute_node)
        assert registered_start_time, f"The {compute_node} slurmd never registered"
    else:
        hold = "--hold " if shape == "held" else ""
        args = " : ".join(
            [f"{hold}--external -n1 -t5 -p {partition_name}"] * component_count
        )
        leader_job_id = atf.submit_job_sbatch(args, fatal=True)
        if shape == "external":
            verify_external_hetjob(leader_job_id)

    before = hetjob_snapshot(leader_job_id)
    if shape == "held":
        assert all(
            component["JobState"] == "PENDING" for component in before.values()
        ), f"Held hetjob {leader_job_id} is not PENDING: {before}"

    atf.restart_slurmctld()

    if shape == "mixed":
        # slurmctld does not save slurmd_start_time, so it reads unset until
        # the slurmd registers again, a few seconds after the restart
        for _ in atf.timer(poll_interval=0.5):
            if slurmd_start_time(compute_node) == registered_start_time:
                break
        else:
            pytest.fail(f"The {compute_node} slurmd never registered again")

    after = hetjob_snapshot(leader_job_id)
    if shape == "held":
        assert (
            after[leader_job_id]["JobState"] == "PENDING"
        ), f"Held leader {leader_job_id} is {after[leader_job_id]['JobState']} after the restart"
    assert (
        after == before
    ), f"Hetjob {leader_job_id} changed across the restart: {before} -> {after}"

    if shape == "external":
        verify_external_hetjob(leader_job_id)
    elif shape == "mixed":
        # StdOut is only reported for a batch job
        stdout_path = atf.get_job_parameter(leader_job_id, "StdOut", fatal=True)
        assert stdout_path, f"Leader {leader_job_id} has no StdOut after the restart"
        atf.assert_file_contents(
            stdout_path,
            batch_marker,
            contains=True,
            message=f"Leader {leader_job_id} lost its batch output across the restart",
        )
        external_job_id = sorted(after)[1]
        for node in atf.node_range_to_list(after[external_job_id]["NodeList"]):
            assert "IDLE" not in atf.get_node_parameter(
                node, "state"
            ), f"Node {node} is IDLE under component {external_job_id}"
    else:
        atf.run_command(f"scontrol release {leader_job_id}", fatal=True)
        verify_external_hetjob(leader_job_id)
