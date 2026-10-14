############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol tasknode against the placement the tasks report."""

import os
import pwd
import re

import pytest

import atf

register_order = ["node102", "node12", "node101", "node11"]
node_list_arg = ",".join(register_order)
nnodes = len(register_order)
ntasks = 6
arbitrary_host_order = [
    register_order[1],
    register_order[1],
    register_order[3],
    register_order[0],
]
# Distinct CPU counts give each node a different capacity, so block placement
# does not fill the nodes evenly.
node_cpus = {"node102": 1, "node12": 2, "node101": 3, "node11": 4}

# A dynamic node registering with more CPUs than the machine has is
# drained as "Low CPUs".
pytestmark = pytest.mark.skipif(
    (os.cpu_count() or 1) < max(node_cpus.values()),
    reason=f"Issue 51002: needs {max(node_cpus.values())} CPUs, the machine has {os.cpu_count()}",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 51002: scontrol tasknode added in 26.11"
    )
    atf.require_config_parameter("MaxNodeCount", nnodes + 1)
    # Required for MaxNodeCount.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # test_tasknode_het runs a het step; only backfill starts het jobs.
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_excludes("SchedulerParameters", "disable_hetjob_steps")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def dynamic_nodes():
    """Register the dynamic nodes once for the whole module in register_order.

    Registration order is deliberately not alphabetical, so a layout that
    wrongly follows node names cannot pass by accident.
    """
    base_port = 62000
    # start_slurmd() waits for each registration, which keeps register_order.
    for i, name in enumerate(register_order):
        atf.start_slurmd(
            name, f"-Z -b --conf 'Port={base_port + i} CPUs={node_cpus[name]}'"
        )

    for name in register_order:
        atf.wait_for_node_state(name, "IDLE", fatal=True)

    yield

    for name in register_order:
        # Make sure no jobs are running on the node so it can be deleted.
        for _ in atf.timer():
            states = atf.get_node_parameter(name, "state")
            if "ALLOCATED" not in states and "MIXED" not in states:
                break
        atf.stop_slurmd(name)
        atf.run_command(
            f"scontrol delete NodeName={name}",
            user=atf.properties["slurm-user"],
        )


def _run_layout_step(components, task_cnt, env=None, job="$SLURM_JOB_ID"):
    """Run a step whose tasks report their placement and query tasknode.

    components holds the srun options of each het component, one entry for a
    regular step. job names the job of the explicit task= queries.

    Returns (expected, reported_by, output, queried): the node each task saw,
    the layout each task got from the no-argument form, the raw output, and
    the node each task got from its task= query of each task id, keyed by
    (querying task, queried task id).
    """
    # Every task asks for every task id, not only its own, so an answer for a
    # task on another node, or in another het component, is checked too.
    task_ids = " ".join(str(tid) for tid in range(task_cnt))
    # Without SLURM_JOB_SLUID tasknode falls back to SLURM_JOB_ID, which only
    # names the first component of a het step.
    fallback = ""
    if len(components) == 1:
        fallback = '; env -u SLURM_JOB_SLUID scontrol tasknode 2>&1 | sed "s/^/JOB /"'
    cmd = (
        "--exclusive --mem=1 -l bash -c '"
        "echo REPORT $SLURM_PROCID $SLURMD_NODENAME $SLURM_STEP_NODELIST "
        "$SLURM_JOB_ID.$SLURM_STEP_ID; "
        f"for t in {task_ids}; do scontrol tasknode {job}.$SLURM_STEP_ID "
        'task=$t 2>&1 | sed "s/^/ONE $t /"; done; '
        'scontrol tasknode 2>&1 | sed "s/^/ENV /"'
        f"{fallback}'"
    )
    output = atf.run_job_output(
        " : ".join(f"{component} {cmd}" for component in components),
        env_vars=env,
        fatal=True,
    )

    expected = {}
    reported_by = {}
    queried = {}
    step_lists = set()
    steps = set()
    for tid, node, step_list, step in re.findall(
        r"^(\d+): REPORT \1 (\S+) (\S+) (\S+)$", output, re.M
    ):
        expected[int(tid)] = node
        step_lists.add(step_list)
        steps.add(step)
    # Keep each task's answer so a wrong one cannot hide behind a right one,
    # and catch a repeated line before the dicts collapse it.
    for label, tid, node in re.findall(
        r"^(\d+): ENV \tTaskId=(\d+) Node=(\S+)$", output, re.M
    ):
        layout = reported_by.setdefault(int(label), {})
        assert int(tid) not in layout, f"task {label} got TaskId={tid} twice: {output}"
        layout[int(tid)] = node
    for label, qtid, tid, node in re.findall(
        r"^(\d+): ONE (\d+) \tTaskId=(\d+) Node=(\S+)$", output, re.M
    ):
        key = (int(label), int(qtid))
        assert key not in queried, f"task {label} got task={qtid} twice: {output}"
        assert tid == qtid, f"task {label} asked task={qtid}, got {tid}: {output}"
        queried[key] = node

    # A task id seen twice would collapse in the dicts and pass silently.
    assert len(expected) == task_cnt, (
        f"Expected {task_cnt} distinct task ids, tasks reported "
        f"{sorted(expected)}: {output}"
    )
    assert len(reported_by) == task_cnt, (
        f"Expected layouts from all {task_cnt} tasks, got "
        f"{sorted(reported_by)}: {output}"
    )
    assert len(queried) == task_cnt * task_cnt, (
        f"Expected each of the {task_cnt} tasks to answer task= for all "
        f"{task_cnt} task ids, got {sorted(queried)}: {output}"
    )
    assert len(step_lists) == 1, f"Tasks disagreed on SLURM_STEP_NODELIST: {step_lists}"
    assert len(steps) == 1, f"Tasks disagreed on their job and step ids: {steps}"
    # tasknode prints the tasks sorted by task id.
    for label, layout in reported_by.items():
        assert list(layout) == sorted(
            layout
        ), f"task {label} got tasks out of order {list(layout)}: {output}"
    # Each answer starts with one StepId= header.
    labels = re.findall(r"^(\d+): ENV StepId=\S+$", output, re.M)
    assert (
        len(labels) == len(set(labels)) == task_cnt
    ), f"Expected one ENV StepId= header per task, got labels {labels}: {output}"
    labels = re.findall(r"^(\d+): ONE (\d+) StepId=\S+$", output, re.M)
    assert (
        len(labels) == len(set(labels)) == task_cnt * task_cnt
    ), f"Expected one ONE StepId= header per task= query, got {labels}: {output}"
    # The header names the step by the job id of the local slurmstepd. In a
    # het step that is SLURM_JOB_ID only for the first component, which has
    # task 0.
    job_id, step_id = steps.pop().split(".")
    for label, header in re.findall(
        r"^(\d+): (?:ENV|ONE \d+) StepId=(\S+)$", output, re.M
    ):
        if len(components) == 1 or label == "0":
            name = rf"{job_id}\.{step_id}"
        else:
            name = rf"\d+\.{step_id}"
        assert re.fullmatch(
            name, header
        ), f"task {label} got StepId={header} for step {job_id}.{step_id}: {output}"
    if fallback:
        # The step found by SLURM_JOB_ID must give the same answer as by SLUID.
        for label in range(task_cnt):
            by_sluid = re.findall(rf"^{label}: ENV (.*)$", output, re.M)
            by_job_id = re.findall(rf"^{label}: JOB (.*)$", output, re.M)
            assert by_job_id == by_sluid, (
                f"task {label} got {by_job_id} by SLURM_JOB_ID and {by_sluid} "
                f"by SLURM_JOB_SLUID: {output}"
            )

    return expected, reported_by, output, queried


@pytest.mark.parametrize("dist", ["block", "cyclic"])
def test_tasknode_layouts(dynamic_nodes, dist):
    """scontrol tasknode maps tasks to the nodes the tasks saw themselves."""

    expected, reported_by, output, queried = _run_layout_step(
        [f"--nodelist={node_list_arg} -N{nnodes} -n{ntasks} -m {dist}"], ntasks
    )

    for label, layout in reported_by.items():
        assert layout == expected, (
            f"{dist}: task {label} saw map {layout}, the placement "
            f"the tasks reported was {expected}: {output}"
        )
    for (label, qtid), rnode in queried.items():
        assert rnode == expected[qtid], (
            f"{dist}: task {label} got node {rnode} for task={qtid}, "
            f"the task ran on {expected[qtid]}: {output}"
        )


def test_tasknode_arbitrary(dynamic_nodes):
    """An arbitrary layout from a hostfile is reported as placed, including a
    repeated host."""

    hostfile = "hostfile_tasknode"
    atf.run_command(
        f"bash -c \"printf '%s\\n' {' '.join(arbitrary_host_order)} > {hostfile}\"",
        fatal=True,
    )

    # An arbitrary layout comes from the hostfile and may not take -N.
    task_cnt = len(arbitrary_host_order)
    expected, reported_by, output, queried = _run_layout_step(
        [f"-n{task_cnt} -m arbitrary"], task_cnt, env=f"SLURM_HOSTFILE={hostfile}"
    )

    # Prove the step followed the hostfile: task i must have run on
    # arbitrary_host_order[i], repeated host included.
    assert expected == dict(
        enumerate(arbitrary_host_order)
    ), f"Step did not follow the hostfile {arbitrary_host_order}: {expected}"
    for label, layout in reported_by.items():
        assert layout == expected, (
            f"arbitrary: task {label} saw map {layout}, the placement "
            f"the tasks reported was {expected}: {output}"
        )
    for (label, qtid), rnode in queried.items():
        assert rnode == expected[qtid], (
            f"arbitrary: task {label} got node {rnode} for task={qtid}, "
            f"the task ran on {expected[qtid]}: {output}"
        )


def test_tasknode_het(dynamic_nodes):
    """Task ids of a het step are het-wide and match SLURM_PROCID, also on the
    node of a non-leader component."""

    leader, other = register_order[0], register_order[3]
    # SLURM_JOB_ID is the leader's job id in every component, so the query
    # selects the component's own step by its SLUID.
    expected, reported_by, output, queried = _run_layout_step(
        [f"-w {leader} -N1 -n1", f"-w {other} -N1 -n2"], 3, job="$SLURM_JOB_SLUID"
    )

    assert expected == {
        0: leader,
        1: other,
        2: other,
    }, f"Het step tasks should be 0 on {leader}, 1-2 on {other}: {expected}"
    for label, layout in reported_by.items():
        assert layout == expected, (
            f"het: task {label} saw map {layout}, the placement "
            f"the tasks reported was {expected}: {output}"
        )
    for (label, qtid), rnode in queried.items():
        assert rnode == expected[qtid], (
            f"het: task {label} got node {rnode} for task={qtid}, "
            f"the task ran on {expected[qtid]}: {output}"
        )


def test_tasknode_errors(dynamic_nodes):
    """Wrong job, step and task ids are errors; the batch step has no
    SLURM_STEP_ID and is skipped when listing all steps of a job."""

    file_out = "tasknode_errors.out"
    job_id = atf.submit_job_sbatch(
        f"--nodelist={register_order[0]} -N1 --exclusive --mem=1 "
        f"--output={file_out} "
        "--wrap='echo STEP_ID=${SLURM_STEP_ID-unset} STEPID=${SLURM_STEPID-unset}; "
        'scontrol tasknode 2>&1 | sed "s/^/PLAIN /"; '
        "scontrol tasknode $SLURM_JOB_ID.batch 2>&1; "
        "scontrol tasknode $SLURM_JOB_ID 2>&1; "
        'srun -n1 bash -c "scontrol tasknode $SLURM_JOB_ID.10; '
        "scontrol tasknode task=99\" 2>&1'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(file_out, "STEP_ID=unset STEPID=unset", contains=True)
    atf.assert_file_contents(
        file_out, f"StepId={job_id}.batch has no task layout", contains=True
    )
    atf.assert_file_contents(
        file_out,
        f"There are no steps with a task layout for JobId={job_id} on this node",
        contains=True,
    )
    atf.assert_file_contents(
        file_out, f"StepId={job_id}.10 does not exist on this node", contains=True
    )

    output = atf.run_command_output(f"cat {file_out}", fatal=True)
    # Without SLURM_STEP_ID the batch script lists all steps of its job.
    assert re.search(
        r"^PLAIN There are no steps with a task layout for JobId=\S+ on this node$",
        output,
        re.M,
    ), f"plain tasknode in the batch script should list the job's steps: {output}"
    # The env fallback selects the step by SLURM_JOB_SLUID.
    assert re.search(
        r"Task 99 does not exist in StepId=s\w+\.0", output
    ), f"unknown task should error: {output}"

    # Only a numeric job id or a SLUID is taken, not an array task or a het
    # component.
    for job in ["1_1", "1+1"]:
        result = atf.run_command(f"scontrol tasknode {job}")
        assert result["exit_code"] == 1, f"tasknode {job} should fail: {result}"
        assert (
            f'"{job}" does not look like a job id' in result["stderr"]
        ), f"tasknode {job} should reject the job id: {result['stderr']}"

    # An empty SLURM_STEP_ID is not taken as step 0.
    result = atf.run_command(
        "scontrol tasknode", env_vars="SLURM_JOB_ID=1 SLURM_STEP_ID="
    )
    assert result["exit_code"] == 1, f"An empty SLURM_STEP_ID should fail: {result}"
    assert (
        'SLURM_STEP_ID "" does not look like a step id' in result["stderr"]
    ), f"An empty SLURM_STEP_ID should be rejected: {result['stderr']}"


def test_tasknode_job_steps(dynamic_nodes):
    """A job id without a step id lists each step of the job on the local
    node with its whole layout, but not the batch step or the steps that
    only run on other nodes."""

    nodes = {register_order[1], register_order[3]}
    file_out = "tasknode_job_steps.out"
    script = "tasknode_job_steps.sh"
    report = (
        "bash -c 'echo REPORT $SLURM_STEP_ID $SLURM_PROCID $SLURMD_NODENAME; "
        "exec sleep infinity'"
    )
    # Relative node 0 is the batch host. Wait for each step to be registered
    # before starting the next, so the steps get the ids 0, 1 and 2 in order.
    atf.make_bash_script(
        script,
        f"srun --overlap -N1 -r0 -n1 {report} &\n"
        + atf.wait_for_step_snippet(0)
        + f"srun --overlap -N2 -n2 {report} &\n"
        + atf.wait_for_step_snippet(1)
        + f"srun --overlap -N1 -r1 -n1 {report} &\n"
        + "wait\n",
    )
    job_id = atf.submit_job_sbatch(
        f"-w {','.join(nodes)} -N2 --exclusive --mem=1 --output={file_out} {script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    node_a = atf.get_job_parameter(job_id, "BatchHost", fatal=True)
    node_b = (nodes - {node_a}).pop()
    sluid = atf.get_job_parameter(job_id, "SLUID", fatal=True)

    # A task only runs once its slurmstepd listens, so wait for all of them.
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(f"cat {file_out}")
        placed = re.findall(r"^REPORT (\d+) (\d+) (\S+)$", output, re.M)
        if len(placed) == 4:
            break
    layouts = {}
    for step, tid, node in placed:
        layouts.setdefault(int(step), {})[int(tid)] = node
    assert [set(layouts.get(step, {}).values()) for step in range(3)] == [
        {node_a},
        {node_a, node_b},
        {node_b},
    ], f"Steps should run on {node_a}, on both nodes and on {node_b}: {output}"

    # Only the steps on node_a, each with all of its tasks.
    expected = [
        f"StepId={job_id}.{step}\n"
        + "".join(
            f"\tTaskId={tid} Node={node}\n"
            for tid, node in sorted(layouts[step].items())
        )
        for step in (0, 1)
    ]
    env = f"SLURMD_NODENAME={node_a}"
    for job in (job_id, sluid):
        result = atf.run_command(f"scontrol tasknode {job}", env_vars=env)
        assert result["exit_code"] == 0, f"tasknode {job} failed: {result}"
        # Split the output at each header; the steps come in no fixed order.
        blocks = re.split(r"^(?=StepId=)", result["stdout"], flags=re.M)
        assert sorted(block for block in blocks if block) == expected, (
            f"tasknode {job} on {node_a} should list just {expected}: "
            f"{result['stdout']}"
        )

    result = atf.run_command(f"scontrol tasknode {job_id}.2", env_vars=env)
    assert result["exit_code"] == 1, f"Step 2 is not on {node_a}: {result}"
    assert (
        f"StepId={job_id}.2 does not exist on this node" in result["stderr"]
    ), f"Step 2 is not on {node_a}: {result['stderr']}"


@pytest.mark.skipif(
    "daemon" not in {p.pw_name for p in pwd.getpwall()},
    reason="Issue 51002: needs a local user that does not own the job",
)
def test_tasknode_permissions(dynamic_nodes):
    """Only the job owner, root and SlurmUser may query a step."""

    node = register_order[0]
    job_id = atf.submit_job_sbatch(
        f"-w {node} -N1 --exclusive --mem=1 --wrap='srun -n1 sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, fatal=True)
    query = f"{atf.properties['slurm-bin-dir']}/scontrol tasknode {job_id}.0"
    env = f"SLURMD_NODENAME={node}"
    expected = f"TaskId=0 Node={node}"

    # The step is RUNNING in slurmctld before its slurmstepd listens.
    for _ in atf.timer(fatal=True):
        if expected in atf.run_command_output(query, env_vars=env):
            break
    for user in ["root", atf.properties["slurm-user"]]:
        result = atf.run_command(query, env_vars=env, user=user)
        assert result["exit_code"] == 0, f"{user} should see the layout: {result}"
        assert (
            result["stdout"] == f"StepId={job_id}.0\n\t{expected}\n"
        ), f"{user} should see the layout: {result}"

    result = atf.run_command(query, env_vars=env, user="daemon")
    assert result["exit_code"] != 0, f"Another user should be refused: {result}"
    assert (
        f"Unable to get task layout for StepId={job_id}.0" in result["stderr"]
    ), f"Another user should be refused: {result['stderr']}"


@pytest.fixture
def start_slurmctld_after():
    """Start slurmctld again if the test stopped it."""

    yield
    if not atf.is_slurmctld_running(quiet=True):
        atf.start_slurmctld()


def test_tasknode_without_slurmctld(dynamic_nodes, start_slurmctld_after):
    """tasknode only asks the local slurmstepd, so it keeps working while
    slurmctld is down."""

    node = register_order[1]
    job_id = atf.submit_job_sbatch(
        f"-w {node} -N1 --exclusive --mem=1 --wrap='srun -n2 sleep infinity'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, fatal=True)
    query = f"scontrol tasknode {job_id}.0"
    env = f"SLURMD_NODENAME={node}"

    # The step is RUNNING in slurmctld before its slurmstepd listens.
    for _ in atf.timer(fatal=True):
        if atf.run_command_exit(query, env_vars=env) == 0:
            break
    assert not atf.stop_slurmctld(), "slurmctld should be stopped"

    result = atf.run_command(query, env_vars=env)
    assert result["exit_code"] == 0, f"tasknode failed without slurmctld: {result}"
    assert (
        result["stdout"]
        == f"StepId={job_id}.0\n\tTaskId=0 Node={node}\n\tTaskId=1 Node={node}\n"
    ), f"tasknode without slurmctld should print the layout: {result['stdout']}"
