############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import os
import re

import pytest

import atf

register_order = ["node102", "node12", "node101", "node11"]
alpha_order = ["node11", "node12", "node101", "node102"]
node_list_arg = ",".join(alpha_order)
nnodes = len(register_order)
# Distinct CPU counts make the per-node run-length arrays order sensitive: a
# uniform array reads the same whatever order it was built in.
node_cpus = {"node102": 1, "node12": 2, "node101": 3, "node11": 4}
total_cpus = sum(node_cpus.values())
# A dynamic node registering with more CPUs than the machine has is drained as
# "Low CPUs", so fall back to one CPU per node and skip the tests that read the
# per-node arrays. The ordering tests do not care how many CPUs a node has.
host_cpus = os.cpu_count() or 1
uneven_cpus = host_cpus >= max(node_cpus.values())
needs_uneven_cpus = pytest.mark.skipif(
    not uneven_cpus,
    reason=f"needs {max(node_cpus.values())} CPUs, the machine has {host_cpus}",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 5), "sbin/slurmd")
    atf.require_version(
        (25, 5), "bin/scontrol", reason="The topology.yaml option was added in 25.05"
    )
    # Bootstrap with one static node; dynamic nodes register on top.
    atf.require_nodes(1)
    atf.require_config_parameter("MaxNodeCount", nnodes + 1)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    # Required for MaxNodeCount.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.require_config_file(
        "topology.yaml",
        """
- topology: topo_alpha
  cluster_default: true
  flat:
    alpha_step_rank: true
- topology: topo_plain
  cluster_default: false
  flat: true
""",
    )

    atf.require_config_parameter(
        "PartitionName",
        {
            "alpha": {"Nodes": "ALL", "Topology": "topo_alpha"},
            "plain": {"Nodes": "ALL", "Topology": "topo_plain"},
        },
    )

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dynamic_nodes():
    """Register the dynamic nodes once for the whole module in register_order."""
    base_port = 61000
    started = []
    for i, name in enumerate(register_order):
        port = base_port + i
        atf.run_command(
            f"{atf.properties['slurm-sbin-dir']}/slurmd -N {name} -Z -b "
            f"--conf 'Port={port} "
            f"CPUs={node_cpus[name] if uneven_cpus else 1}'",
            user="root",
            fatal=True,
        )
        started.append(name)
        atf.repeat_until(
            lambda n=name: n in atf.get_nodes(quiet=True),
            lambda found: found,
            timeout=30,
            fatal=True,
        )

    for name in register_order:
        assert atf.wait_for_node_state(
            name, "IDLE", timeout=30
        ), f"Dynamic node {name} should reach IDLE state"

    yield

    for name in started:
        # Make sure no jobs are running on the node so we can delete it cleanly.
        atf.repeat_until(
            lambda n=name: atf.get_node_parameter(n, "state"),
            lambda states: "ALLOCATED" not in states and "MIXED" not in states,
            fatal=False,
        )
        pid = atf.run_command_output(
            f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {name}'",
            fatal=False,
        ).strip()
        if pid:
            atf.run_command(f"kill {pid}", user="root", fatal=False)
        atf.run_command(f"scontrol delete NodeName={name}", user="slurm", fatal=False)


def _parse_task_nodes(output):
    """Parse 'tid: nodename' lines, return list of nodes ordered by tid."""
    matches = re.findall(r"(\d+): (\S+)", output)
    matches.sort(key=lambda x: int(x[0]))
    return [name for _, name in matches]


def _run_step_layout(partition):
    return atf.run_job_output(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} -n{nnodes} "
        f"-m block --exclusive --mem=1 -l printenv SLURMD_NODENAME",
        fatal=True,
    )


def _run_step_env(job_args, env=None):
    """Run a step and report each task's placement and the emitted lists.

    Returns (nodes, node_ids, step_list, job_list, cpus_per_node): the nodes
    and node ids ordered by task id, plus the three job/step values, which
    every task reports identically. SLURM_TASKS_PER_NODE is not reported: a
    step's copy is derived from its own layout, so it cannot disagree with the
    list next to it.
    """
    output = atf.run_job_output(
        f"{job_args} -l bash -c 'echo $SLURM_NODEID $SLURMD_NODENAME "
        "$SLURM_STEP_NODELIST $SLURM_JOB_NODELIST $SLURM_JOB_CPUS_PER_NODE "
        "$SLURM_NODELIST $SLURM_GTIDS'",
        env_vars=env,
        fatal=True,
    )

    matches = re.findall(r"(\d+): (\d+) (\S+) (\S+) (\S+) (\S+) (\S+) (\S+)", output)
    matches.sort(key=lambda x: int(x[0]))
    assert matches, f"Expected tasks to report their placement, got: {output}"
    # An empty variable leaves the line one field short, and re.findall() drops
    # it rather than failing, so count the labelled lines the step printed
    labelled = len(re.findall(r"^\d+: ", output, re.M))
    assert (
        len(matches) == labelled
    ), f"Expected {labelled} task lines to parse, got {len(matches)}: {output}"

    for m in matches:
        assert m[6] == m[4], (
            f"SLURM_NODELIST {m[6]} should match SLURM_JOB_NODELIST {m[4]} "
            f"on {m[2]}"
        )

    # SLURM_GTIDS holds the task ids on the reporting node. A node earlier in
    # the emitted list runs earlier tasks, whatever the distribution, so the
    # lowest task id per node rises with the node id.
    lowest_gtid = {}
    for m in matches:
        node_id = int(m[1])
        lowest = min(int(tid) for tid in m[7].split(","))
        lowest_gtid[node_id] = min(lowest_gtid.get(node_id, lowest), lowest)
    by_node_id = [lowest_gtid[node_id] for node_id in sorted(lowest_gtid)]
    assert by_node_id == sorted(by_node_id), (
        f"SLURM_GTIDS should ascend with SLURM_NODEID, got {by_node_id} "
        f"for node ids {sorted(lowest_gtid)}"
    )

    return (
        [m[2] for m in matches],
        [int(m[1]) for m in matches],
        matches[0][3],
        matches[0][4],
        matches[0][5],
    )


def _expand_reps(compressed):
    """Expand a run-length list such as '2(x2),1(x2)' into [2, 2, 1, 1]"""
    values = []
    for part in compressed.split(","):
        if "(x" in part:
            value, reps = part.split("(x")
            values += [int(value)] * int(reps.rstrip(")"))
        else:
            values.append(int(part))

    return values


def _require_rank_ordered_lists():
    """Emitting node lists in topology-rank order is 26.11+.

    stepmgr builds the step lists, so with SlurmStepMgr this depends on the
    slurmd version too.
    """
    for component in ["sbin/slurmctld", "sbin/slurmd"]:
        atf.require_version(
            (26, 11),
            component,
            reason=f"Issue 50993: {component} emits node lists in topology-rank order in 26.11+",
        )


def test_alpha_step_rank_enabled(dynamic_nodes):
    """With alpha_step_rank, tasks are laid out in alphabetical node order."""

    output = _run_step_layout("alpha")
    actual = _parse_task_nodes(output)
    assert actual == alpha_order, (
        f"Tasks should follow alphabetical node order: "
        f"expected {alpha_order}, got {actual}"
    )


def test_alpha_step_rank_disabled(dynamic_nodes):
    """Without alpha_step_rank, tasks follow node-table (registration) order."""

    output = _run_step_layout("plain")
    actual = _parse_task_nodes(output)
    assert actual == register_order, (
        f"Tasks should follow node-table order: "
        f"expected {register_order}, got {actual}"
    )


@pytest.mark.parametrize(
    ("partition", "expected_order"),
    [("alpha", alpha_order), ("plain", register_order)],
)
@needs_uneven_cpus
def test_emitted_lists_match_task_order(dynamic_nodes, partition, expected_order):
    """SLURM_STEP_NODELIST and SLURM_JOB_NODELIST follow task placement."""

    _require_rank_ordered_lists()

    nodes, node_ids, step_list, job_list, cpus_per_node = _run_step_env(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} -n{nnodes} "
        f"-m block --exclusive --mem=1"
    )

    expected_cpus = [node_cpus[node] for node in expected_order]
    assert nodes == expected_order, f"Tasks should follow {expected_order}, got {nodes}"
    assert (
        atf.node_range_to_list(step_list) == expected_order
    ), f"SLURM_STEP_NODELIST should be {expected_order}, got {step_list}"
    assert (
        atf.node_range_to_list(job_list) == expected_order
    ), f"SLURM_JOB_NODELIST should be {expected_order}, got {job_list}"
    assert node_ids == list(range(nnodes)), (
        f"SLURM_NODEID should index SLURM_STEP_NODELIST: expected "
        f"{list(range(nnodes))}, got {node_ids}"
    )
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )


@pytest.mark.parametrize(
    ("partition", "expected_order"),
    [("alpha", alpha_order), ("plain", register_order)],
)
@needs_uneven_cpus
def test_external_launcher_list_matches_task_order(
    dynamic_nodes, partition, expected_order
):
    """An external launcher maps its own ranks onto SLURM_STEP_NODELIST, so it
    has to be emitted in the same order as a regular step's."""

    _require_rank_ordered_lists()

    nodes, node_ids, step_list, job_list, cpus_per_node = _run_step_env(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} "
        f"--external-launcher --exclusive --mem=1"
    )

    expected_cpus = [node_cpus[node] for node in expected_order]
    assert nodes == expected_order, f"Tasks should follow {expected_order}, got {nodes}"
    assert node_ids == list(range(nnodes)), (
        f"SLURM_NODEID should index SLURM_STEP_NODELIST: expected "
        f"{list(range(nnodes))}, got {node_ids}"
    )
    assert (
        atf.node_range_to_list(step_list) == expected_order
    ), f"SLURM_STEP_NODELIST should be {expected_order}, got {step_list}"
    assert (
        atf.node_range_to_list(job_list) == expected_order
    ), f"SLURM_JOB_NODELIST should be {expected_order}, got {job_list}"
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )


@pytest.mark.parametrize(
    ("partition", "expected_order"),
    [("alpha", alpha_order), ("plain", register_order)],
)
@needs_uneven_cpus
def test_job_arrays_align_with_nodelist(dynamic_nodes, partition, expected_order):
    """SLURM_JOB_CPUS_PER_NODE and SLURM_TASKS_PER_NODE are positional.

    Both come from the allocation response, so they have to be read outside of
    a step: a step's copies are derived from its own layout and so cannot
    disagree with the list next to them. Allocating every CPU of nodes with
    distinct CPU counts makes both arrays uneven, and one task per CPU makes
    the task counts match them.
    """
    _require_rank_ordered_lists()

    output = atf.run_command_output(
        f"salloc -p {partition} --nodelist={node_list_arg} -N{nnodes} "
        f"-n{total_cpus} --exclusive --mem=1 "
        "bash -c 'echo ARRAYS $SLURM_JOB_NODELIST $SLURM_JOB_CPUS_PER_NODE "
        "$SLURM_TASKS_PER_NODE'",
        fatal=True,
    )

    match = re.search(r"ARRAYS (\S+) (\S+) (\S+)", output)
    assert match, f"Expected the allocation to report its arrays, got: {output}"
    job_list, cpus_per_node, tasks_per_node = match.groups()

    expected_cpus = [node_cpus[node] for node in expected_order]
    assert (
        atf.node_range_to_list(job_list) == expected_order
    ), f"SLURM_JOB_NODELIST should be {expected_order}, got {job_list}"
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )
    assert _expand_reps(tasks_per_node) == expected_cpus, (
        f"SLURM_TASKS_PER_NODE {tasks_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )


@pytest.mark.parametrize(
    ("partition", "expected_order"),
    [("alpha", alpha_order), ("plain", register_order)],
)
@needs_uneven_cpus
def test_batch_env_matches_task_order(dynamic_nodes, partition, expected_order):
    """A batch script's node list and per-node arrays agree with each other.

    The batch launch builds its own copies, so it needs its own coverage.
    """
    _require_rank_ordered_lists()

    output = f"{atf.module_tmp_path}/batch-{partition}.out"
    job_id = atf.submit_job_sbatch(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} -n{total_cpus} "
        f"--exclusive --mem=1 -o {output} "
        "--wrap 'echo ARRAYS $SLURM_JOB_NODELIST $SLURM_JOB_CPUS_PER_NODE "
        "$SLURM_TASKS_PER_NODE'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    assert atf.wait_for_file(output), f"No output from job {job_id}"

    # wait_for_file() only proves the inode exists, so re-read until the line
    # the script wrote has actually been flushed. The trailing newline is what
    # makes that test sound: without it the last \S+ group is satisfied by a
    # token that is still being written.
    arrays = r"ARRAYS (\S+) (\S+) (\S+)\n"
    assert atf.repeat_until(
        lambda: atf.run_command_output(f"cat {output}", fatal=True),
        lambda text: re.search(arrays, text) is not None,
    ), f"Expected the batch script to report its arrays in {output}"

    match = re.search(arrays, atf.run_command_output(f"cat {output}", fatal=True))
    job_list, cpus_per_node, tasks_per_node = match.groups()

    expected_cpus = [node_cpus[node] for node in expected_order]
    assert (
        atf.node_range_to_list(job_list) == expected_order
    ), f"SLURM_JOB_NODELIST should be {expected_order}, got {job_list}"
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )
    assert _expand_reps(tasks_per_node) == expected_cpus, (
        f"SLURM_TASKS_PER_NODE {tasks_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )


@pytest.mark.parametrize(
    ("partition", "expected_order"),
    [("alpha", alpha_order), ("plain", register_order)],
)
@needs_uneven_cpus
def test_job_info_response_matches_task_order(dynamic_nodes, partition, expected_order):
    """A step joining an existing allocation gets the same order.

    srun --jobid asks the controller for allocation info, which builds its
    own node list and cpu counts.
    """
    _require_rank_ordered_lists()

    job_id = atf.submit_job_sbatch(
        f"-p {partition} --nodelist={node_list_arg} -N{nnodes} -n{total_cpus} "
        f"--exclusive --mem=1 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    nodes, node_ids, step_list, job_list, cpus_per_node = _run_step_env(
        f"--jobid={job_id} --overlap -N{nnodes} --ntasks-per-node=1 --mem=1"
    )
    atf.cancel_jobs([job_id])

    expected_cpus = [node_cpus[node] for node in expected_order]
    assert nodes == expected_order, f"Tasks should follow {expected_order}, got {nodes}"
    assert (
        atf.node_range_to_list(step_list) == expected_order
    ), f"SLURM_STEP_NODELIST should be {expected_order}, got {step_list}"
    assert (
        atf.node_range_to_list(job_list) == expected_order
    ), f"SLURM_JOB_NODELIST should be {expected_order}, got {job_list}"
    assert node_ids == list(range(nnodes)), (
        f"SLURM_NODEID should index SLURM_STEP_NODELIST: expected "
        f"{list(range(nnodes))}, got {node_ids}"
    )
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {expected_order}"
    )


def test_arbitrary_job_rejects_task_count_update(dynamic_nodes):
    """scontrol cannot change the task count of an arbitrary job.

    The count is the number of entries in the requested node list, and
    arbitrary_tpn is derived from it, so it cannot be set independently.
    """
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 50993: the task count of an arbitrary job is rejected in 26.11+",
    )

    hostfile = f"{atf.module_tmp_path}/hostfile_numtasks"
    requested = [alpha_order[0], alpha_order[1]]
    atf.run_command(
        f"bash -c \"printf '%s\\n' {' '.join(requested)} > {hostfile}\"", fatal=True
    )

    # Held so the job stays pending: the count is only updatable while pending
    job_id = atf.submit_job_sbatch(
        f"-p plain --hold -n{len(requested)} -m arbitrary --mem=1 " f"--wrap 'true'",
        fatal=True,
        env_vars=f"SLURM_HOSTFILE={hostfile}",
    )

    output = atf.run_command_error(
        f"scontrol update jobid={job_id} NumTasks=1", xfail=True, fatal=True
    )
    atf.cancel_jobs([job_id])

    assert (
        "not supported" in output.lower()
    ), f"Updating NumTasks of an arbitrary job should be refused, got: {output}"


@needs_uneven_cpus
def test_external_launcher_arbitrary_keeps_hostfile_order(dynamic_nodes):
    """An external launcher step built from an arbitrary list holds each host
    once, in the hostfile's order.

    The list is built separately from a regular step's, so a repeated host
    has to collapse there too or it names more hosts than the layout. An
    external launcher runs one task per node, so the repeat costs a task
    rather than placing two on the same node.
    """
    _require_rank_ordered_lists()

    requested = [alpha_order[1], alpha_order[1], register_order[0], alpha_order[0]]
    distinct = [alpha_order[1], register_order[0], alpha_order[0]]

    hostfile = f"{atf.module_tmp_path}/hostfile_ext"
    atf.run_command(
        f"bash -c \"printf '%s\\n' {' '.join(requested)} > {hostfile}\"", fatal=True
    )

    nodes, node_ids, step_list, job_list, cpus_per_node = _run_step_env(
        f"-p plain -n{len(requested)} -m arbitrary --external-launcher --mem=1",
        env=f"SLURM_HOSTFILE={hostfile}",
    )

    assert (
        nodes == distinct
    ), f"Tasks should follow the hostfile order {distinct}, got {nodes}"
    assert node_ids == list(range(len(distinct))), (
        f"SLURM_NODEID should index SLURM_STEP_NODELIST: expected "
        f"{list(range(len(distinct)))}, got {node_ids}"
    )
    assert (
        atf.node_range_to_list(step_list) == distinct
    ), f"SLURM_STEP_NODELIST should be {distinct}, got {step_list}"
    assert (
        atf.node_range_to_list(job_list) == distinct
    ), f"SLURM_JOB_NODELIST should be {distinct}, got {job_list}"

    # The list is built separately here, so the array beside it needs its own
    # check. One CPU per task, so the counts follow the repeated host.
    expected_cpus = [requested.count(node) for node in distinct]
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {distinct}"
    )


@needs_uneven_cpus
def test_arbitrary_keeps_hostfile_order(dynamic_nodes):
    """Arbitrary distribution emits nodes in the hostfile's order.

    --nodes cannot be combined with --distribution=arbitrary, so the task
    count alone drives the layout. A repeated host keeps the position of its
    first occurrence.
    """
    _require_rank_ordered_lists()

    # Neither alphabetical nor registration order, with a repeated host
    requested = [alpha_order[1], alpha_order[1], register_order[0], alpha_order[0]]
    distinct = [alpha_order[1], register_order[0], alpha_order[0]]

    hostfile = f"{atf.module_tmp_path}/hostfile"
    atf.run_command(
        f"bash -c \"printf '%s\\n' {' '.join(requested)} > {hostfile}\"", fatal=True
    )

    nodes, node_ids, step_list, job_list, cpus_per_node = _run_step_env(
        f"-p plain -n{len(requested)} -m arbitrary --mem=1",
        env=f"SLURM_HOSTFILE={hostfile}",
    )

    assert (
        nodes == requested
    ), f"Tasks should follow the hostfile order {requested}, got {nodes}"
    assert (
        atf.node_range_to_list(step_list) == distinct
    ), f"SLURM_STEP_NODELIST should be {distinct}, got {step_list}"
    assert node_ids == [
        0,
        0,
        1,
        2,
    ], f"SLURM_NODEID should index SLURM_STEP_NODELIST, got {node_ids}"
    assert (
        atf.node_range_to_list(job_list) == distinct
    ), f"SLURM_JOB_NODELIST should be {distinct}, got {job_list}"

    # One CPU per task, so the counts follow the repeated host
    expected_cpus = [requested.count(node) for node in distinct]
    assert _expand_reps(cpus_per_node) == expected_cpus, (
        f"SLURM_JOB_CPUS_PER_NODE {cpus_per_node} should be {expected_cpus} "
        f"for {distinct}"
    )
