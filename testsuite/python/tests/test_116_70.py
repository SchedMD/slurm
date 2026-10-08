############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test srun --cpu-bind and --mem-bind task environment."""

import logging

import pytest

import atf

pytestmark = pytest.mark.slow

# These tests cover the path from the command line to the task environment.
#
# Every request below is built from an id the node was measured to hold, so no
# test depends on how srun treats an id that does not exist. Which ids srun
# accepts or rejects is not what these tests cover.
#
# The explicit ldom bindings are documented as unsupported unless the job holds
# the whole node, so the allocations here are exclusive. An exclusive
# allocation still leaves out CPUs the node withholds from jobs, such as
# specialized ones, and the tests that need the whole node skip on such a node.

# The test environment, set once by setup() and read by every test.
node_name = None
node_cpus = None
cpus_on_node = None
task_prog = None
cpu_ids = None


@pytest.fixture(scope="module", autouse=True)
def setup(taskget):
    global node_name, node_cpus, cpus_on_node, task_prog, cpu_ids

    atf.require_config_parameter_includes("TaskPlugin", "task/affinity")

    # task/affinity builds an ldom mask out of the CPUs the node is configured
    # with rather than out of the CPUs the step holds. An exclusive allocation
    # on a node configured with fewer CPUs than it runs on is given every CPU
    # while the ldom mask covers only the configured ones, so require_hardware.
    node_name = atf.require_nodes(1, require_hardware=True)[0]

    atf.require_slurm_running()

    # --exclusive cannot stop a FORCE partition from oversubscribing CPUs
    partition = atf.default_partition()
    oversubscribe = atf.get_partition_parameter(partition, "OverSubscribe", "NO")
    if oversubscribe.startswith("FORCE"):
        atf.set_partition_parameter(partition, "OverSubscribe", "NO")

    task_prog = taskget
    node_cpus = atf.get_node_parameter(node_name, "cpus")

    job_id, cpus_on_node = allocate()
    cpu_ids = probe_cpu_ids(job_id)
    logging.info(f"Usable CPU ids: {cpu_ids}")

    atf.cancel_jobs([job_id], fatal=True)


@pytest.fixture(scope="function")
def allocation(setup):
    """Create a fresh exclusive allocation for the steps a test attaches."""
    job_id, measured = allocate()
    assert measured == cpus_on_node, (
        f"Allocation should hold the {cpus_on_node} CPUs setup() measured, "
        f"not {measured}"
    )
    return job_id


@pytest.fixture(scope="module")
def numa_cache(numaget):
    """Skip without a NUMA build, else hold the ids once a test measures them.

    lscpu runs in a step of its own on the node, so read it here, while no
    allocation holds the node.
    """
    atf.require_build_config("HAVE_NUMA")
    return {"cpu_topo": atf.get_node_cpu_topology(node_name, require_numa=True)}


@pytest.fixture(scope="function")
def numa_ids(allocation, numaget, numa_cache):
    """Return the NUMA ids the memory and ldom tests may bind to, or skip.

    The ids are measured once, in the allocation of the first test that asks.
    """
    if "ids" not in numa_cache:
        ids, skip_reason = probe_numa_ids(allocation, numaget, numa_cache["cpu_topo"])
        logging.info(f"Usable NUMA ids: {ids}")
        numa_cache.update(ids=ids, skip_reason=skip_reason)

    if numa_cache["skip_reason"]:
        pytest.skip(numa_cache["skip_reason"])
    return numa_cache["ids"]


def allocate():
    """Create an exclusive single-node allocation that is ready to take steps.

    Returns the job id and the CPU count the allocation reports holding.
    """
    job = atf.submit_job_sbatch(
        f"--nodelist={node_name} -N1 --exclusive -t5 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job, "RUNNING", fatal=True)

    # TODO: Issue 50980. A step created too soon after the job reports RUNNING
    # is rejected outright rather than waiting, and no job state marks that
    # window, so the only reliable readiness signal is a step that got
    # through. Retry until one does.
    for _ in atf.timer():
        probe = atf.run_command(
            f"srun --jobid={job} -n1 printenv SLURM_CPUS_ON_NODE", quiet=True
        )
        if probe["exit_code"] == 0:
            break
    else:
        pytest.fail(f"No step could be created in the allocation: {probe['stderr']}")

    return job, int(probe["stdout"].strip())


def whole_node(
    job_id, prog, cpu_bind=None, mem_bind=None, spread=False, label=False, **kwargs
):
    """Run prog in a step holding every CPU of the allocation.

    The step names neither --cpus-per-task nor --exact, so it is given every
    CPU the allocation holds on the node. spread launches one task per CPU
    instead of giving every CPU to one task, and label prefixes each line of
    output with the id of the task that wrote it.
    """
    opts = [f"--jobid={job_id}"]
    if spread:
        opts.append(f"--ntasks={cpus_on_node}")
    if cpu_bind is not None:
        opts.append(f"'--cpu-bind={cpu_bind}'")
    if mem_bind is not None:
        opts.append(f"'--mem-bind={mem_bind}'")
    if label:
        opts.append("--label")
    opts = " ".join(opts)
    return atf.run_command(f"srun {opts} {prog}", **kwargs)


def probe_cpu_ids(job_id):
    """Return the CPU ids a whole-node step is given on this node."""

    # One task per CPU, each bound to a single thread, so the union of the
    # reported masks is every CPU a step may be given. A none binding would
    # report whatever the task happened to inherit instead.
    cpu_probe = whole_node(
        job_id, task_prog, cpu_bind="threads", spread=True, fatal=True, quiet=True
    )
    tasks = atf.parse_taskget(cpu_probe["stdout"])
    assert len(tasks) == cpus_on_node, f"Step should launch {cpus_on_node} tasks"

    cpu_mask = 0
    for task in tasks:
        cpu_mask |= task["mask"]

    return atf.mask_to_list(cpu_mask)


def probe_numa_ids(job_id, numa_prog, cpu_topo):
    """Return the NUMA ids a whole-node step may bind to.

    cpu_topo is atf.get_node_cpu_topology() for the node.

    A node without NUMA support returns None and the reason to skip.
    """
    numa_probe = whole_node(
        job_id,
        numa_prog,
        cpu_bind="verbose,none",
        mem_bind="verbose,none",
        quiet=True,
    )
    if "numa support not available" in numa_probe["stderr"]:
        return None, f"This test requires NUMA support on node {node_name}"
    assert (
        numa_probe["exit_code"] == 0
    ), f"NUMA probe step should succeed: {numa_probe['stderr']}"
    assert (
        "mem-bind" in numa_probe["stderr"]
    ), f"NUMA probe step should report a verbose mem-bind line: {numa_probe['stderr']}"

    bindings = atf.parse_numaget(numa_probe["stdout"])
    assert bindings, "NUMA probe step should launch a task"

    mem_mask = 0
    for binding in bindings:
        mem_mask |= binding["allowed_mask"]

    # A locality domain is a NUMA node holding CPUs the step may run on, while
    # memory may also be bound to a NUMA node that holds no CPUs at all. A
    # domain may also hold no memory, so it is found from its CPUs.
    ldom_ids = {cpu_topo[cpu_id]["numa_node"] for cpu_id in cpu_ids}

    ids = {
        "mem": atf.mask_to_list(mem_mask),
        "ldom": sorted(ldom_ids),
    }
    return ids, None


def any_id(ids, form):
    """Return an id from ids, spelled for a map or a mask binding."""
    assert ids, "Should have measured at least one usable id"

    bind_id = ids[-1]
    if form == "mask":
        # srun.1 asks for a 0x prefix on a mask that does not begin with a
        # digit, so every mask here carries one.
        return f"0x{1 << bind_id:x}"
    elif form == "map":
        return str(bind_id)


def as_env_var(bind_type, var):
    return f"SLURM_{bind_type.upper()}_BIND_{var}"


def task_envs(job_id, bind_type, value, *bind_vars, spread=False):
    """Return a {bind_var: value} dict per task, indexed by task id.

    An unset variable is reported as None. spread launches one task per CPU,
    which is what shows a binding generated for each task rather than the step.
    """
    env_vars = [as_env_var(bind_type, var) for var in bind_vars]

    # SLURM_PROCID is always set, so every task reports a line even when none
    # of the variables asked for is, and grep never exits on finding nothing.
    pattern = "|".join(["SLURM_PROCID"] + env_vars)

    # A task's output is only identified by --label.
    result = whole_node(
        job_id,
        f"""sh -c 'env | grep -E "^({pattern})="'""",
        **{f"{bind_type}_bind": value},
        spread=spread,
        label=True,
        fatal=True,
    )

    reported = {}
    for line in result["stdout"].splitlines():
        label, sep, assignment = line.partition(": ")
        task_id = label.strip()
        assert sep and task_id.isdigit(), f"Should have labeled {line} with a task id"
        env_var, _, env_value = assignment.partition("=")
        reported.setdefault(int(task_id), {})[env_var] = env_value

    task_count = cpus_on_node if spread else 1
    assert sorted(reported) == list(
        range(task_count)
    ), f"Each of the {task_count} tasks should report once: {result['stdout']}"

    # A variable a task does not have is absent from its environment.
    return [
        {env_var: env.get(env_var) for env_var in env_vars}
        for task_id, env in sorted(reported.items())
    ]


def require_whole_node(binding):
    """Skip unless the allocation holds every CPU the node is configured with.

    Without the whole node, rank_ldom, map_ldom and mask_ldom are rejected, and
    none is replaced by a mask of the allocated CPUs.
    """
    if cpus_on_node < node_cpus:
        pytest.skip(
            f"This test requires all {node_cpus} configured CPUs, not "
            f"{cpus_on_node}, to use {binding}"
        )


def single_task_env(job_id, bind_type, value, *bind_vars):
    """Return the bind variables the one task of a whole-node step sees."""
    return task_envs(job_id, bind_type, value, *bind_vars)[0]


def assert_bind_env(env, bind_type, var, expected):
    env_var = as_env_var(bind_type, var)
    assert expected == env[env_var], f"Should have set {env_var} to {expected}"


def assert_bind_list(env, bind_type, form, bind_list):
    """Verify the LIST variable holds the map or mask list that was asked for."""
    if form == "map":
        assert_bind_env(env, bind_type, "LIST", bind_list)
    elif form == "mask":
        # srun does not document the mask spelling it echoes back.
        env_var = as_env_var(bind_type, "LIST")
        assert env[env_var], f"{env_var} should be non-empty"


@pytest.mark.parametrize("bind_type", ["mem", "cpu"])
@pytest.mark.parametrize(
    "verbosity,expected",
    [
        ("verbose", "verbose"),
        ("v", "verbose"),
        ("quiet", "quiet"),
        ("q", "quiet"),
    ],
)
def test_bind_verbose(allocation, bind_type, verbosity, expected):
    """Test that verbose/v and quiet/q both reach the VERBOSE variable."""
    env = single_task_env(allocation, bind_type, f"{verbosity},none", "VERBOSE")
    assert_bind_env(env, bind_type, "VERBOSE", expected)


@pytest.mark.parametrize(
    "value,expected",
    [
        ("none", "none"),
        ("no", "none"),
        ("local", "local"),
        ("rank", "rank"),
    ],
)
def test_mem_bind_type(allocation, value, expected):
    """Test that a mem-bind type reaches the TYPE variable."""
    env = single_task_env(allocation, "mem", value, "TYPE")
    assert_bind_env(env, "mem", "TYPE", expected)


@pytest.mark.parametrize("form", ["map", "mask"])
def test_mem_bind_list(allocation, numa_ids, form):
    """Test that a mem-bind map or mask sets both the TYPE and LIST variables."""
    env_type = f"{form}_mem:"
    bind_list = any_id(numa_ids["mem"], form)
    env = single_task_env(allocation, "mem", env_type + bind_list, "TYPE", "LIST")
    assert_bind_env(env, "mem", "TYPE", env_type)
    assert_bind_list(env, "mem", form, bind_list)


@pytest.mark.parametrize("prefer", ["prefer", "p"])
def test_mem_bind_prefer(allocation, numa_ids, prefer):
    """Test that prefer sets SLURM_MEM_BIND_PREFER."""
    opt = f"{prefer},map_mem:{any_id(numa_ids['mem'], 'map')}"
    env = single_task_env(allocation, "mem", opt, "PREFER")
    assert_bind_env(env, "mem", "PREFER", "prefer")


def test_mem_bind_no_prefer(allocation, numa_ids):
    """Test that SLURM_MEM_BIND_PREFER is unset without a prefer binding."""
    opt = f"map_mem:{any_id(numa_ids['mem'], 'map')}"
    env = single_task_env(allocation, "mem", opt, "PREFER")
    prefer_var = as_env_var("mem", "PREFER")
    assert (
        env[prefer_var] is None
    ), f"{prefer_var} should be unset without a prefer binding"


@pytest.mark.parametrize(
    "value,expected",
    [
        ("none", "none"),
        ("no", "none"),
        ("rank_ldom", "rank_ldom"),
    ],
)
def test_cpu_bind_type(allocation, value, expected):
    """Test that an explicit cpu-bind type reaches the TYPE variable.

    A step holding the whole node is left with the binding it asked for.
    """
    require_whole_node(f"--cpu-bind={value}")
    env = single_task_env(allocation, "cpu", value, "TYPE")
    assert_bind_env(env, "cpu", "TYPE", expected)


@pytest.mark.parametrize("value", ["sockets", "cores", "threads", "ldoms"])
def test_cpu_bind_unit_type(allocation, value):
    """Test that a cpu-bind unit reports a documented TYPE.

    A unit names how to generate a binding and is resolved into an explicit one.
    Which one it picks depends on hardware and the allocation, so only the
    documented set of values can be asserted.
    """
    CPU_BIND_TYPES = [
        "none",
        "map_cpu:",
        "mask_cpu:",
        "rank_ldom",
        "map_ldom:",
        "mask_ldom:",
    ]
    env = single_task_env(allocation, "cpu", value, "TYPE")
    env_var = as_env_var("cpu", "TYPE")
    assert (
        env[env_var] in CPU_BIND_TYPES
    ), f"{env_var} should be one of {CPU_BIND_TYPES}, not {env[env_var]}"


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_list(allocation, form):
    """Test that a cpu-bind map or mask sets both the TYPE and LIST variables.

    Mask SLURM_CPU_BIND_LIST format is not documented, so only check non-empty.
    """
    env_type = f"{form}_cpu:"
    bind_list = any_id(cpu_ids, form)
    env = single_task_env(allocation, "cpu", env_type + bind_list, "TYPE", "LIST")
    assert_bind_env(env, "cpu", "TYPE", env_type)
    assert_bind_list(env, "cpu", form, bind_list)


@pytest.mark.parametrize("form", ["map", "mask"])
def test_ldom_bind_list(allocation, numa_ids, form):
    """Test that an ldom map or mask sets both the TYPE and LIST variables.

    Mask SLURM_CPU_BIND_LIST format is not documented, so only check non-empty.
    """
    env_type = f"{form}_ldom:"
    require_whole_node(f"--cpu-bind={env_type}")
    bind_list = any_id(numa_ids["ldom"], form)
    env = single_task_env(allocation, "cpu", env_type + bind_list, "TYPE", "LIST")
    assert_bind_env(env, "cpu", "TYPE", env_type)
    assert_bind_list(env, "cpu", form, bind_list)


def test_generated_cpu_bind_list(allocation):
    """Test that a generated binding sets a CPU map or mask for the whole step.

    A map or mask the request named is echoed back, so only a binding Slurm
    generates shows what it was resolved into. The binding is generated per
    task, so every task is asked what it was given.
    """
    # TODO: The *_BIND_LIST format is not documented, so the CPUs the list
    # names cannot be read back without guessing its radix. Compare them with
    # the CPUs each task holds once the format is specified.
    type_var = as_env_var("cpu", "TYPE")
    list_var = as_env_var("cpu", "LIST")
    envs = task_envs(allocation, "cpu", "threads", "TYPE", "LIST", spread=True)

    # The list names the binding of the whole step, not of one task within it.
    reported = {(env[type_var], env[list_var]) for env in envs}
    assert (
        len(reported) == 1
    ), f"Every task should see the same {type_var} and {list_var}: {envs}"

    bind_type, env_list = reported.pop()
    assert bind_type in [
        "map_cpu:",
        "mask_cpu:",
    ], f"{type_var} should name a CPU map or mask, not {bind_type}"
    assert env_list, f"{list_var} should be non-empty"
