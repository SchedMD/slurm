############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the CPUs srun steps get with --exact and --overlap."""

import pytest

import atf

NODE_CPUS = 8


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2, [("CPUs", NODE_CPUS), ("ThreadsPerCore", 1)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_slurm_running()


def _allocate(nodes):
    """Return a job that holds whole nodes and has no step running."""
    job_id = atf.submit_job_sbatch(
        f"-N{nodes} -n{nodes * NODE_CPUS} --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # TODO: Issue 50980. A step started too soon after RUNNING can be
    # rejected, so retry a step that holds no resources until one runs.
    probe = f"srun --jobid={job_id} --overlap --mem=0 true"
    for _ in atf.timer(fatal=True):
        if atf.run_command_exit(probe) == 0:
            break
    for _ in atf.timer(fatal=True):
        if not _step_states(job_id):
            break
    return job_id


def _step_states(job_id):
    """Return the sorted states of the srun steps of a job."""
    return sorted(
        step["State"]
        for step_id, step in atf.get_steps(job_id, quiet=True, fatal=True).items()
        if step_id.split(".")[1] not in ("batch", "extern", "interactive")
    )


def _start_step(job_id, step_args, out_file, env_vars=None):
    """Start a step that writes its CPU count to out_file and keeps running."""
    atf.run_command(
        f"srun --jobid={job_id} --mem=0 {step_args} bash -c "
        f"'[ $SLURM_PROCID -ne 0 ] || echo $SLURM_CPUS_ON_NODE > {out_file}; "
        "exec sleep infinity'",
        env_vars=env_vars,
        background=True,
    )


@pytest.mark.parametrize(
    "step_args,env_vars,expected",
    [
        ("-N1 -n1", None, [NODE_CPUS]),
        ("-N1 -n1 --exact", None, [1]),
        ("-N1 -n1 --exclusive", None, [1]),
        ("-N1 -n1", "SLURM_EXACT=1", [1]),
        ("-N1 -n1 -c1", None, [1]),
        ("-N1 -n1 -c2", None, [2]),
        ("-N2 -n2", None, [NODE_CPUS, NODE_CPUS]),
        ("-N2 -n2 --exact", None, [1, 1]),
        ("-N2 -n2 -c3", None, [3, 3]),
    ],
    ids=[
        "default",
        "exact",
        "exclusive",
        "env-exact",
        "cpus-per-task-1",
        "cpus-per-task",
        "default-2-nodes",
        "exact-2-nodes",
        "cpus-per-task-2-nodes",
    ],
)
def test_step_cpus(step_args, env_vars, expected):
    """Test that a step gets all CPUs of its nodes unless --exact is set."""
    job_id = _allocate(2)

    result = atf.run_job(
        f"--jobid={job_id} --mem=0 {step_args} printenv SLURM_CPUS_ON_NODE",
        env_vars=env_vars,
    )
    assert result["exit_code"] == 0, f"srun {step_args} should run"
    cpus = sorted(int(cpu) for cpu in result["stdout"].split())
    assert cpus == expected, f"srun {step_args} should get {expected} CPUs per node"


def test_exact_steps_run_in_parallel():
    """Test that --exact steps run in parallel and a default step waits."""
    job_id = _allocate(1)
    # Leave CPUs free, so the default step waits only for the whole node.
    exact = NODE_CPUS // 2 - 1

    for out_file in ["exact_1", "exact_2"]:
        _start_step(job_id, f"-n{exact} --exact", out_file)
        atf.assert_file_contents(out_file, str(exact))
    _start_step(job_id, "-n1", "default")

    for _ in atf.timer(fatal=True):
        states = _step_states(job_id)
        if len(states) == 3:
            break
    assert states == ["PENDING", "RUNNING", "RUNNING"], "The default step should wait"


def test_exact_steps_do_not_share():
    """Test that --exact steps do not share CPUs without --overlap."""
    job_id = _allocate(1)
    exact = NODE_CPUS - 2

    _start_step(job_id, f"-n{exact} --exact", "exact_1")
    atf.assert_file_contents("exact_1", str(exact))
    _start_step(job_id, f"-n{exact} --exact", "exact_2")

    for _ in atf.timer(fatal=True):
        states = _step_states(job_id)
        if len(states) == 2:
            break
    assert states == ["PENDING", "RUNNING"], "The second step should wait"


@pytest.mark.parametrize("overlap_by", ["option", "env"])
@pytest.mark.parametrize("default_first", [False, True], ids=["last", "first"])
def test_exact_overlap(default_first, overlap_by):
    """Test that --exact --overlap steps share CPUs with a default step."""
    job_id = _allocate(1)
    # Two overlap steps together use more CPUs than the job has.
    overlap = NODE_CPUS - 2
    if overlap_by == "option":
        overlap_args, env_vars = f"-n{overlap} --exact --overlap", None
    else:
        overlap_args, env_vars = f"-n{overlap} --exact", "SLURM_OVERLAP=1"
    steps = [
        ("overlap_1", overlap_args, env_vars, overlap),
        ("overlap_2", overlap_args, env_vars, overlap),
    ]
    default = ("default", "-n1", None, NODE_CPUS)
    steps = [default] + steps if default_first else steps + [default]

    for out_file, step_args, env_vars, cpus in steps:
        _start_step(job_id, step_args, out_file, env_vars)
        atf.assert_file_contents(out_file, str(cpus))
    assert _step_states(job_id) == ["RUNNING"] * 3, "All steps should run together"
