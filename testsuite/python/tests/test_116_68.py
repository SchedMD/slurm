############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test memory and ldom affinity as observed on the node for NUMA systems."""

import logging
import re
from dataclasses import dataclass

import pytest

import atf

# Every test attaches steps with --jobid to a fresh exclusive single-node
# allocation. A probe step measures the CPUs and NUMA nodes a step holds
# because node configuration and lscpu output report distinct quantities. Every
# expectation is phrased against that measured set, and lscpu, through
# atf.get_node_cpu_topology(), only maps the measured CPUs to their NUMA nodes.
#
# The explicit ldom bindings are documented as unsupported unless the job holds
# the whole node, so the allocations here are exclusive. An exclusive
# allocation still leaves out CPUs the node withholds from jobs, such as
# specialized ones, and the tests of those bindings skip on such a node.

pytestmark = pytest.mark.slow

cpu_count = None
file_prog = None
task_prog = None
topo = None

MEM_NODE = "usable NUMA node"
LDOM = "CPU-bearing locality domain"
LOCAL_LDOM = "CPU- and memory-bearing locality domain"
MAX_SAMPLED_IDS = 4


@dataclass(frozen=True)
class Topology:
    """What a step on the test node can reach, measured on that node."""

    node_name: str

    # The CPUs a step on that node holds.
    held_cpu_mask: int

    # Every NUMA node the step may bind memory to, including nodes with no CPUs.
    mem_numa_ids: list

    # ldom id -> mask of the CPUs the step holds in that locality domain.
    ldom_cpu_masks: dict

    @property
    def held_cpu_cnt(self):
        return len(atf.mask_to_list(self.held_cpu_mask))

    @property
    def cpu_ldom_ids(self):
        """The locality domains holding CPUs the step can use."""
        ids = []
        for ldom_id, cpus in self.ldom_cpu_masks.items():
            if cpus & self.held_cpu_mask:
                ids.append(ldom_id)
        return ids

    @property
    def mem_mask(self):
        """Mask view of every NUMA node the step may bind memory to."""
        mask = 0
        for numa_id in self.mem_numa_ids:
            mask |= 1 << numa_id
        return mask

    @property
    def local_ldom_ids(self):
        """The domains holding both CPUs and memory the step can use.

        Only these can satisfy a local binding.
        """
        return [n for n in self.cpu_ldom_ids if n in self.mem_numa_ids]

    def cpu_to_ldom_mask(self, cpu_mask):
        """Mask of the locality domains holding any CPU in cpu_mask."""
        mask = 0
        for ldom_id, cpus in self.ldom_cpu_masks.items():
            if cpus & cpu_mask:
                mask |= 1 << ldom_id
        return mask

    def ldom_to_cpu_mask(self, ldom_mask):
        """Mask of the CPUs the step holds in the domains in ldom_mask."""
        mask = 0
        for ldom_id in atf.mask_to_list(ldom_mask):
            mask |= self.ldom_cpu_masks.get(ldom_id, 0)
        return mask

    def __str__(self):
        ldoms = []
        for ldom_id, cpus in self.ldom_cpu_masks.items():
            ldoms.append(f"\t{ldom_id}: 0x{cpus:x}")
        ldom_str = "\n".join(ldoms)
        return (
            f"node: {self.node_name}\n"
            f"held_cpu_mask: 0x{self.held_cpu_mask:x}\n"
            f"mem_numa_ids: {self.mem_numa_ids}\n"
            f"cpu_ldom_ids: {self.cpu_ldom_ids}\n"
            f"ldom_cpu_masks {{\n{ldom_str}\n}}"
        )


@pytest.fixture(scope="module", autouse=True)
def setup(taskget, numaget):
    global cpu_count, file_prog, task_prog, topo

    atf.require_config_parameter_includes("TaskPlugin", "task/affinity")
    atf.require_build_config("HAVE_NUMA")

    # The ldom expectations are built from lscpu, and task/affinity builds an
    # ldom mask out of the CPUs the node is configured with rather than out of
    # the CPUs the step holds. An exclusive allocation on a node configured
    # with fewer CPUs than it runs on is given every CPU while the ldom mask
    # covers only the configured ones, so require_hardware.
    node = atf.require_nodes(1, require_hardware=True)[0]

    atf.require_slurm_running()

    # --exclusive cannot stop a FORCE partition from oversubscribing CPUs
    partition = atf.default_partition()
    oversubscribe = atf.get_partition_parameter(partition, "OverSubscribe", "NO")
    if oversubscribe.startswith("FORCE"):
        atf.set_partition_parameter(partition, "OverSubscribe", "NO")

    task_prog = taskget
    file_prog = numaget
    cpu_count = atf.get_node_parameter(node, "cpus")

    # Measure from a step holding a whole allocation, which is what every test
    # runs in. A step of its own would hold only what it asked for.
    probe_job, held_cpu_mask, mem_numa_ids, cpus_on_node = allocate(node)

    # Cross-check against an independent documented path. Otherwise the probe
    # covering every CPU of the allocation is only an assumption.
    held_cpu_cnt = len(atf.mask_to_list(held_cpu_mask))
    assert (
        held_cpu_cnt == cpus_on_node
    ), f"Measured {held_cpu_cnt} CPUs but SLURM_CPUS_ON_NODE reports {cpus_on_node}"

    atf.cancel_jobs([probe_job], fatal=True)

    # Classify the measured CPUs by the NUMA node lscpu reports for each.
    ldom_cpu_masks = {}
    node_cpu_topo = atf.get_node_cpu_topology(node, require_numa=True)
    for cpu_id, cpu_topo in node_cpu_topo.items():
        if (1 << cpu_id) & held_cpu_mask:
            ldom_id = cpu_topo["numa_node"]
            mask = ldom_cpu_masks.get(ldom_id, 0) | (1 << cpu_id)
            ldom_cpu_masks[ldom_id] = mask

    assert mem_numa_ids, "Should discover at least one usable NUMA node"
    topo = Topology(node, held_cpu_mask, mem_numa_ids, ldom_cpu_masks)
    assert topo.cpu_ldom_ids, "Should discover at least one locality domain with CPUs"
    logging.info(f"Detected topology\n{topo}")


def allocation():
    job_id, held_cpu_mask, mem_numa_ids, _cpus_on_node = allocate(topo.node_name)
    assert (
        held_cpu_mask == topo.held_cpu_mask
    ), f"Allocation should hold the CPUs setup() measured: 0x{held_cpu_mask:x} != 0x{topo.held_cpu_mask:x}"
    assert (
        mem_numa_ids == topo.mem_numa_ids
    ), f"Allocation should hold the NUMA nodes setup() measured: {mem_numa_ids} != {topo.mem_numa_ids}"

    return job_id


def allocate(node):
    job_id = atf.submit_job_sbatch(
        f"--nodelist={node} -N1 --exclusive -t5 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    bindings, verbose, cpus_on_node = run_probes(job_id)

    assert_no_failed_binding(verbose)
    assert (
        len(bindings) == cpu_count
    ), f"Probe step should launch {cpu_count} tasks, not {len(bindings)}"

    held_cpu_mask = 0
    allowed = 0
    for binding in bindings:
        held_cpu_mask |= binding["cpu_mask"]
        allowed |= binding["allowed_mask"]

    mem_numa_ids = atf.mask_to_list(allowed)

    return job_id, held_cpu_mask, mem_numa_ids, cpus_on_node


def run_probes(job):
    # TODO: Issue 50980. A step created too soon after the job reports RUNNING
    # is rejected outright rather than waiting, and no job state marks that
    # window, so the only reliable readiness signal is a step that got
    # through. Retry until one does; the probe below can then be run once.
    for _ in atf.timer():
        probe = atf.run_command(
            f"srun --jobid={job} -n1 printenv SLURM_CPUS_ON_NODE", quiet=True
        )
        if probe["exit_code"] == 0:
            break
    else:
        pytest.fail(f"No step could be created in the allocation: {probe['stderr']}")
    cpus_on_node = int(probe["stdout"].strip())

    # One task per configured CPU, each bound to a single thread, so the union
    # of their CPU masks is every CPU the step holds. The count comes from the
    # node configuration to keep it independent of cpus_on_node. The
    # allocation can hold fewer CPUs than that, for example when some are
    # specialized, so --overcommit stays on. The tasks beyond the CPUs the
    # step holds reuse one of them, which leaves the union unchanged. Memory
    # is left unbound, so every task reports every NUMA node the step may bind
    # memory to.
    output = run_step(
        job,
        cpu_count,
        cpu_bind="threads",
        mem_bind="none",
        cpus=True,
        quiet=True,
    )
    verbose = output["stderr"]
    if "numa support not available" in verbose:
        pytest.skip(f"This test requires NUMA support on the node of job {job}")
    assert output["exit_code"] == 0, f"Binding step should succeed: {verbose}"
    assert (
        "mem-bind" in verbose
    ), f"Probe step should report a verbose mem-bind line: {verbose}"

    bindings = parse_mixed_lines(output["stdout"])

    return bindings, verbose, cpus_on_node


def order_ids(ids, pattern):
    """Reorder ids as forward, reverse, or alternating.

    Alternating puts odd positions in descending order before the even
    positions in ascending order.
    """
    if pattern == "forward":
        return list(ids)
    if pattern == "reverse":
        return list(reversed(ids))
    alternating = []
    for pos, value in enumerate(ids):
        if pos % 2:
            alternating = [value] + alternating
        else:
            alternating = alternating + [value]
    return alternating


def fat_pair_masks(ids):
    """Return one two-id mask per pair, so every task gets a fat mask.

    The pairs overlap, so every id takes part and three ids yield two masks
    to compare against each other."""
    return [(1 << a) | (1 << b) for a, b in zip(ids, ids[1:])]


def sample_ids(ids, limit=MAX_SAMPLED_IDS):
    """Return at most limit ids, keeping the first and the last."""
    if len(ids) <= limit:
        return ids
    last = len(ids) - 1
    picks = sorted({round(i * last / (limit - 1)) for i in range(limit)})
    return [ids[i] for i in picks]


def require(cond, what):
    """Skip unless the step reached what."""
    if not cond:
        pytest.skip(f"This test requires a step to reach {what}")


def require_mem_nodes(count, why):
    """Skip unless the step reached count NUMA nodes it may bind memory to."""
    require(len(topo.mem_numa_ids) >= count, f"{count} {MEM_NODE}s to {why}")


def require_ldoms(count, why):
    """Skip unless the step reached count locality domains holding CPUs."""
    require(len(topo.cpu_ldom_ids) >= count, f"{count} {LDOM}s to {why}")


def require_local_ldoms(count, why):
    """Skip unless the step reached count domains holding CPUs and memory."""
    require(len(topo.local_ldom_ids) >= count, f"{count} {LOCAL_LDOM}s to {why}")


def require_whole_node():
    """Skip unless the step holds every CPU the node is configured with.

    rank_ldom, map_ldom and mask_ldom are documented as unsupported otherwise.
    """
    require(
        topo.held_cpu_cnt >= cpu_count,
        f"all {cpu_count} configured CPUs, not {topo.held_cpu_cnt}, to use an "
        "explicit ldom binding",
    )


def rank_task_count(ids, noun=MEM_NODE):
    """Return how many ranks can be checked against ids.

    Rank binding maps task i to NUMA node i, so it can only be verified over
    the ids before the first gap. The kernel does not guarantee a gap-free
    id space.

    Skips when the step never reached id 0, leaving no rank to check.
    """
    ntasks = 0
    while ntasks in ids:
        ntasks += 1
    require(ntasks, f"{noun} 0")
    return ntasks


def assert_no_failed_binding(verbose):
    """Verify srun bound every task it reported."""
    assert "FAILED" not in verbose, f"srun reported a failed binding: {verbose}"


def assert_mem_verbose(verbose, expected_numa_masks, bind_type, prefer=False):
    """Verify slurm intended to bind each task to the mask at its rank index.

    prefer asserts the line reports a preferred node rather than a binding,
    which task/affinity marks with ' PREFER ' where it otherwise writes '='.
    """
    expected = dict(enumerate(expected_numa_masks))
    mode = r"\s+PREFER\s+" if prefer else "="
    pattern = rf"mem-bind{mode}{bind_type}.*task +(\d+).*mask 0x([0-9a-fA-F]+)"
    found = re.findall(pattern, verbose)
    assert len(found) == len(
        expected
    ), f"Verbose should report {len(expected)} tasks, not {len(found)}."
    for task_id, mask in found:
        assert int(task_id) in expected, f"Verbose reports unknown task {task_id}"
        assert int(mask, 16) == expected[int(task_id)], (
            f"Verbose mask for task {task_id} should be the one requested: "
            f"0x{int(mask, 16):x} != 0x{expected[int(task_id)]:x}"
        )


def assert_mem_effect(bindings, expected_numa_masks):
    """Verify the kernel honored the binding Slurm asked for.

    numa_set_membind() reports nothing back to Slurm, so a request the kernel
    drops leaves no trace in the step's own output. Reading the binding back
    in the task is the only way to see that it took.
    """
    expected = dict(enumerate(expected_numa_masks))
    for binding in bindings:
        requested = expected[binding["task_id"]]
        allowed = binding["allowed_mask"]
        assert not requested & ~allowed, (
            f"Task {binding['task_id']} may only use NUMA nodes 0x{allowed:x}, "
            f"which does not cover the requested 0x{requested:x}"
        )
        assert (
            binding["policy_mode"] == "BIND"
        ), f"Task {binding['task_id']} should hold a binding, not {binding['policy_mode']}"

    actual = {b["task_id"]: b["mem_mask"] for b in bindings}
    assert actual == expected, f"Unexpected memory binding: {actual} != {expected}"


def assert_mem_binding(bindings, verbose, expected_numa_masks, bind_type):
    assert_mem_verbose(verbose, expected_numa_masks, bind_type)
    assert_mem_effect(bindings, expected_numa_masks)


def assert_mem_binding_order(bindings, verbose, expected_numa_ids, bind_type):
    masks = [1 << numa_id for numa_id in expected_numa_ids]
    assert_mem_binding(bindings, verbose, masks, bind_type)


def assert_ldom_cpus(bindings, expected_ldom_masks):
    """Verify each task runs on every CPU of the domains at its rank index.

    Explicit ldom bindings require the whole node, where the CPUs of a domain
    are the CPUs the step holds in it.
    """
    task_ids = sorted(b["task_id"] for b in bindings)
    assert task_ids == list(
        range(len(expected_ldom_masks))
    ), f"Should bind {len(expected_ldom_masks)} tasks, not {task_ids}"

    for binding in bindings:
        task_id = binding["task_id"]
        ldom_mask = expected_ldom_masks[task_id]
        expected = topo.ldom_to_cpu_mask(ldom_mask)
        actual = binding["cpu_mask"]
        assert actual == expected, (
            f"Task {task_id} should run on every CPU of locality domain(s) "
            f"{atf.mask_to_list(ldom_mask)}, not of "
            f"{atf.mask_to_list(topo.cpu_to_ldom_mask(actual))}: "
            f"0x{actual:x} != 0x{expected:x}"
        )


def assert_ldom_order(bindings, expected_ldom_ids):
    """Verify each task runs on the locality domain at its rank index."""
    assert_ldom_cpus(bindings, [1 << ldom_id for ldom_id in expected_ldom_ids])


def assert_ldom_verbose(verbose, bindings, bind_type=None):
    """Verify the verbose output reports the CPUs each task runs on.

    bind_type also asserts the type the line reports the binding under.
    """
    assert_no_failed_binding(verbose)

    type_pattern = bind_type if bind_type else r"[A-Z]+"
    pattern = (
        rf"cpu-bind(?:-[a-z]+)?={type_pattern}.*task +(\d+).*mask 0x([0-9a-fA-F]+)"
    )
    found = re.findall(pattern, verbose)
    assert len(found) == len(bindings), (
        f"Verbose should report {len(bindings)} tasks, not {len(found)}. "
        f"Looked for {pattern!r} in: {verbose}"
    )

    cpu_masks = {b["task_id"]: b["cpu_mask"] for b in bindings}
    reported = {}
    for task_id, mask in found:
        assert int(task_id) in cpu_masks, f"Verbose reports unknown task {task_id}"
        reported[int(task_id)] = int(mask, 16)

    # task/affinity builds a domain mask from every CPU of the node in that
    # domain, including ones the step does not hold, and reports that mask.
    # Whether those belong in the report is undocumented, so only the CPUs the
    # step holds are compared.
    for task_id, mask in reported.items():
        held = mask & topo.held_cpu_mask
        expected = cpu_masks[task_id] & topo.held_cpu_mask
        assert held == expected, (
            f"Task {task_id} should be reported with the CPUs it runs on: "
            f"0x{held:x} != 0x{expected:x}"
        )


def parse_mixed_lines(output):
    """Merge each task's taskget and numaget lines into one binding.

    Each binding gains the task's CPU affinity as cpu_mask.
    """
    cpu_lines, numa_lines = [], []
    for line in output.splitlines():
        if line.strip():
            lines = numa_lines if "mem_mask" in line else cpu_lines
            lines.append(line)

    cpu_masks = {}
    for task in atf.parse_taskget("\n".join(cpu_lines)):
        assert (
            task["task_id"] not in cpu_masks
        ), f"Task {task['task_id']} should report its CPU affinity once"
        cpu_masks[task["task_id"]] = task["mask"]

    bindings = atf.parse_numaget("\n".join(numa_lines))
    numa_ids = sorted(b["task_id"] for b in bindings)
    assert numa_ids == sorted(
        cpu_masks
    ), f"Every task should report both probes: {numa_ids} != {sorted(cpu_masks)}"

    for binding in bindings:
        binding["cpu_mask"] = cpu_masks[binding["task_id"]]
    return bindings


def run_step(
    job_id,
    ntasks,
    mem_bind=None,
    cpu_bind=None,
    overcommit=True,
    verbose=True,
    cpus=False,
    **kwargs,
):
    """Run a step.

    Every step holds the whole allocation, so --overcommit only lifts the
    limit of one task per CPU. Most tests ask for a task count that can exceed
    the CPUs the step holds, so it is the default. An automatic CPU binding
    lays tasks out one per CPU only until there are more tasks than CPUs, so
    a test asserting that layout turns it off.

    verbose asks srun to report the binding, which every test needs except the
    ones asserting that it is not reported.

    cpus also runs taskget in each task.
    """
    opts = []
    verbosity = "verbose," if verbose else ""
    if mem_bind:
        opts.append(f"'--mem-bind={verbosity}{mem_bind}'")
    if cpu_bind:
        opts.append(f"'--cpu-bind={verbosity}{cpu_bind}'")
    if overcommit:
        opts.append("--overcommit")

    prog = f"sh -c '{task_prog} && {file_prog}'" if cpus else file_prog
    cmd = [f"srun --jobid={job_id} --ntasks={ntasks}", *opts, prog]
    return atf.run_command(" ".join(cmd), **kwargs)


def bind_tasks(job_id, ntasks, cpus=False, **kwargs):
    """Run a binding step; return (bindings, verbose stderr).

    cpus also reports each task's CPU affinity as cpu_mask.
    """
    output = run_step(job_id, ntasks, cpus=cpus, fatal=True, **kwargs)

    if cpus:
        bindings = parse_mixed_lines(output["stdout"])
    else:
        bindings = atf.parse_numaget(output["stdout"])

    return bindings, output["stderr"]


def bind_list(form, ids, rep_cnt=None):
    """Render ids as a map or mask binding list."""
    strs = []
    for i in ids:
        s = ""
        if form == "map":
            s = str(i)
        elif form == "mask":
            s = f"0x{1 << i:x}"
        if rep_cnt is not None:
            s = f"{s}*{rep_cnt}"
        strs.append(s)
    return ",".join(strs)


def test_mem_bind_rank():
    """Test that --mem-bind=rank binds task i to NUMA node i."""

    # rank's documented contract is only "bind by task rank", and beyond the
    # CPUs the step holds the observed mapping stops being one node per rank.
    # Limit the assertion to documented behavior and detected NUMA nodes.
    ntasks = min(topo.held_cpu_cnt, rank_task_count(topo.mem_numa_ids))
    require(ntasks >= 2, f"2 {MEM_NODE}s to map a second task to a second node")
    job_id = allocation()

    bindings, verbose = bind_tasks(job_id, ntasks, mem_bind="rank")
    assert_mem_binding_order(bindings, verbose, range(ntasks), "RANK")


def test_mem_bind_local():
    """Test that --mem-bind=local binds each task to its own node's memory."""
    require_local_ldoms(2, "spread tasks over more than one domain")
    require_whole_node()
    job_id = allocation()

    # Give each task a locality domain of its own, so the tasks are spread by
    # construction. Tasks sharing one domain would satisfy a local binding
    # whether or not it did anything.
    ntasks = len(topo.local_ldom_ids)
    list_str = ",".join(str(n) for n in topo.local_ldom_ids)
    bindings, verbose = bind_tasks(
        job_id,
        ntasks,
        mem_bind="local",
        cpu_bind=f"map_ldom:{list_str}",
        cpus=True,
    )

    reached = set()
    assert len(bindings) == ntasks, "--mem-bind=local should bind each task"
    for binding in bindings:
        ldom_mask = topo.cpu_to_ldom_mask(binding["cpu_mask"])
        assert (
            ldom_mask == binding["mem_mask"]
        ), "--mem-bind=local should use local memory for each task"
        reached.add(ldom_mask)

    expected = {1 << ldom_id for ldom_id in topo.local_ldom_ids}
    assert (
        reached == expected
    ), f"Tasks should reach every locality domain: {reached} != {expected}"

    # The spread comes from the CPU binding, so check that it didn't fail
    assert_no_failed_binding(verbose)

    # A task's local node is the domain its CPU binding named, so what the
    # step should request is known before it runs.
    assert_mem_binding_order(bindings, verbose, topo.local_ldom_ids, "LOC")


@pytest.mark.parametrize("mem_bind", [None, "none"])
def test_mem_bind_none(mem_bind):
    """Test that no binding and --mem-bind=none leave every task every memory node."""
    job_id = allocation()

    ntasks = len(topo.mem_numa_ids)
    bindings, verbose = bind_tasks(job_id, ntasks, mem_bind=mem_bind)
    assert bindings, "Should launch at least one task"

    for binding in bindings:
        task_id = binding["task_id"]
        allowed = binding["allowed_mask"]
        assert allowed == topo.mem_mask, (
            f"Task {task_id} should keep every NUMA node the step holds: "
            f"0x{allowed:x} != 0x{topo.mem_mask:x}"
        )
        assert (
            binding["policy_mode"] == "DEFAULT"
        ), f"Task {task_id} should hold no policy, not {binding['policy_mode']}"

    if mem_bind == "none":
        # A none binding requests nothing, so its verbose line reports what the
        # task was left with rather than what was asked for.
        by_rank = sorted(bindings, key=lambda b: b["task_id"])
        assert_mem_verbose(verbose, [b["allowed_mask"] for b in by_rank], "NONE")


def test_mem_bind_prefer():
    """Test that --mem-bind=prefer names a node without restricting the rest."""
    job_id = allocation()

    ids = topo.mem_numa_ids
    list_str = ",".join(str(n) for n in ids)
    bindings, verbose = bind_tasks(
        job_id, len(ids), mem_bind=f"prefer,map_mem:{list_str}"
    )

    expected_masks = [1 << numa_id for numa_id in ids]
    assert_mem_verbose(verbose, expected_masks, "MAP", prefer=True)

    for binding in bindings:
        task_id = binding["task_id"]
        mask = expected_masks[task_id]
        assert (
            binding["policy_mode"] == "PREFERRED"
        ), f"Task {task_id} should hold a preference, not {binding['policy_mode']}"
        assert binding["policy_mask"] == mask, (
            f"Task {task_id} should prefer NUMA node {ids[task_id]}: "
            f"0x{binding['policy_mask']:x} != 0x{mask:x}"
        )
        allowed = binding["allowed_mask"]
        assert allowed == topo.mem_mask, (
            f"Task {task_id} should keep every NUMA node the step holds: "
            f"0x{allowed:x} != 0x{topo.mem_mask:x}"
        )


def test_mem_bind_prefer_fat_mask():
    """Test that prefer with a multi-node mask prefers its lowest-numbered node."""
    require_mem_nodes(2, "name more than one node in the preferred mask")
    job_id = allocation()

    # The highest ids keep the lowest one from being node 0 on larger nodes
    ids = topo.mem_numa_ids[-2:]
    mask = (1 << ids[0]) | (1 << ids[1])
    bindings, verbose = bind_tasks(job_id, 1, mem_bind=f"prefer,mask_mem:0x{mask:x}")
    assert_mem_verbose(verbose, [mask], "MASK", prefer=True)

    assert len(bindings) == 1, f"Should launch one task: {bindings}"
    binding = bindings[0]
    assert (
        binding["policy_mode"] == "PREFERRED"
    ), f"Task should hold a preference, not {binding['policy_mode']}"
    assert binding["policy_mask"] == 1 << min(ids), (
        f"Task should prefer NUMA node {min(ids)}, the lowest in 0x{mask:x}: "
        f"0x{binding['policy_mask']:x}"
    )
    allowed = binding["allowed_mask"]
    assert allowed == topo.mem_mask, (
        f"Task should keep every NUMA node the step holds: "
        f"0x{allowed:x} != 0x{topo.mem_mask:x}"
    )


@pytest.mark.parametrize("form", ["map", "mask"])
def test_mem_bind_single(form):
    """Test that a single-element map/mask_mem binds every task to that NUMA node."""
    job_id = allocation()

    ntasks = max(2, len(topo.mem_numa_ids))
    for numa_id in sample_ids(topo.mem_numa_ids):
        list_str = bind_list(form, [numa_id])
        bindings, verbose = bind_tasks(
            job_id, ntasks, mem_bind=f"{form}_mem:{list_str}"
        )
        assert_mem_binding_order(bindings, verbose, [numa_id] * ntasks, form.upper())


@pytest.mark.parametrize("form", ["map", "mask"])
def test_mem_bind_reuse(form):
    """Test that a multi-element map/mask_mem list wraps to its first element."""
    require_mem_nodes(2, "distinguish wrap-to-first from repeat-last")
    job_id = allocation()

    ids = topo.mem_numa_ids[:2]
    ntasks = len(ids) + 1
    expected = [ids[0], ids[1], ids[0]]
    list_str = bind_list(form, ids)

    bindings, verbose = bind_tasks(job_id, ntasks, mem_bind=f"{form}_mem:{list_str}")
    assert_mem_binding_order(bindings, verbose, expected, form.upper())


def test_mask_mem_fat_masks():
    """Test that mask_mem binds each task to every NUMA node in its mask."""
    require_mem_nodes(3, "leave a node outside every pair mask")
    job_id = allocation()

    masks = fat_pair_masks(topo.mem_numa_ids)
    list_str = ",".join(f"0x{mask:x}" for mask in masks)

    bindings, verbose = bind_tasks(job_id, len(masks), mem_bind=f"mask_mem:{list_str}")
    assert_mem_binding(bindings, verbose, masks, "MASK")


@pytest.mark.parametrize("verbosity", ["quiet", ""])
def test_mem_bind_quiet(verbosity):
    """Test that quiet and the default verbosity report no binding."""
    job_id = allocation()

    numa_id = topo.mem_numa_ids[0]
    opts = f"map_mem:{bind_list('map', [numa_id])}"
    if verbosity:
        opts = f"{verbosity},{opts}"

    bindings, verbose = bind_tasks(job_id, 1, mem_bind=opts, verbose=False)
    assert_mem_effect(bindings, [1 << numa_id])
    assert (
        "mem-bind" not in verbose
    ), f"Binding should not be reported without verbose: {verbose}"


@pytest.mark.parametrize("form", ["map", "mask"])
@pytest.mark.parametrize("pattern", ["forward", "reverse", "alternating"])
def test_mem_bind_patterns(form, pattern):
    """Test mem map/mask binding in forward, reverse and alternating order."""
    require_mem_nodes(2, "put the ids in more than one order")
    job_id = allocation()

    order = order_ids(topo.mem_numa_ids, pattern)
    list_str = bind_list(form, order)

    bindings, verbose = bind_tasks(
        job_id, len(order), mem_bind=f"{form}_mem:{list_str}"
    )
    assert_mem_binding_order(bindings, verbose, order, form.upper())


@pytest.mark.parametrize("form", ["map", "mask"])
def test_mem_bind_repetition(form):
    """Test the '*<count>' repetition syntax documented for map_mem/mask_mem."""
    # At least two are required: '0*2,1*2' binds 0,0,1,1 instead of 0,1,0,1.
    require_mem_nodes(2, "distinguish repetition from reuse")
    job_id = allocation()
    ids = sample_ids(topo.mem_numa_ids)

    expected = [numa_id for numa_id in ids for _ in range(2)]
    list_str = bind_list(form, ids, 2)

    bindings, verbose = bind_tasks(
        job_id, len(expected), mem_bind=f"{form}_mem:{list_str}"
    )
    assert_mem_binding_order(bindings, verbose, expected, form.upper())


def test_cpu_bind_rank_ldom():
    """Test that --cpu-bind=rank_ldom binds task i to locality domain i."""
    require_ldoms(2, "map a second task to a second domain")
    require_whole_node()
    job_id = allocation()
    ntasks = rank_task_count(topo.cpu_ldom_ids, LDOM)

    bindings, verbose = bind_tasks(job_id, ntasks, cpu_bind="rank_ldom", cpus=True)
    assert_ldom_order(bindings, range(ntasks))
    assert_ldom_verbose(verbose, bindings, "LDRANK")


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_ldom(form):
    """Test that --cpu-bind=map_ldom/mask_ldom binds tasks by locality domain."""
    require_ldoms(2, "distinguish a domain mask from the whole allocation")
    require_whole_node()
    job_id = allocation()

    list_str = bind_list(form, topo.cpu_ldom_ids)
    bindings, verbose = bind_tasks(
        job_id,
        len(topo.cpu_ldom_ids),
        cpu_bind=f"{form}_ldom:{list_str}",
        cpus=True,
    )
    assert_ldom_order(bindings, topo.cpu_ldom_ids)
    assert_ldom_verbose(verbose, bindings, f"LD{form.upper()}")


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_ldom_single(form):
    """Test that a single-element map/mask_ldom confines every task to it."""
    require_ldoms(2, "leave a domain outside the one the list names")
    require_whole_node()
    job_id = allocation()

    # One task per domain reuses the single element on every task after the
    # first.
    ntasks = len(topo.cpu_ldom_ids)
    for ldom_id in sample_ids(topo.cpu_ldom_ids):
        list_str = bind_list(form, [ldom_id])
        bindings, verbose = bind_tasks(
            job_id,
            ntasks,
            cpu_bind=f"{form}_ldom:{list_str}",
            cpus=True,
        )

        assert_ldom_order(bindings, [ldom_id] * ntasks)
        assert_ldom_verbose(verbose, bindings, f"LD{form.upper()}")


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_ldom_reuse(form):
    """Test that a multi-element map/mask_ldom list wraps to its first element."""
    require_ldoms(2, "distinguish wrap-to-first from repeat-last")
    require_whole_node()
    job_id = allocation()

    ids = topo.cpu_ldom_ids[:2]
    ntasks = 3
    expected = [ids[0], ids[1], ids[0]]
    list_str = bind_list(form, ids)

    bindings, verbose = bind_tasks(
        job_id,
        ntasks,
        cpu_bind=f"{form}_ldom:{list_str}",
        cpus=True,
    )
    assert_ldom_order(bindings, expected)
    assert_ldom_verbose(verbose, bindings, f"LD{form.upper()}")


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_ldom_repetition(form):
    """Test the '*<count>' repetition syntax documented for map_ldom/mask_ldom."""
    require_ldoms(2, "distinguish repetition from reuse")
    require_whole_node()
    job_id = allocation()

    ids = sample_ids(topo.cpu_ldom_ids)

    expected = [ldom_id for ldom_id in ids for _ in range(2)]
    list_str = bind_list(form, ids, 2)

    bindings, verbose = bind_tasks(
        job_id,
        len(expected),
        cpu_bind=f"{form}_ldom:{list_str}",
        cpus=True,
    )
    assert_ldom_order(bindings, expected)
    assert_ldom_verbose(verbose, bindings, f"LD{form.upper()}")


def test_mask_ldom_fat_masks():
    """Test that mask_ldom binds each task to every domain in its mask."""
    require_ldoms(3, "leave a domain outside every pair mask")
    require_whole_node()
    job_id = allocation()

    masks = fat_pair_masks(topo.cpu_ldom_ids)
    list_str = ",".join(f"0x{mask:x}" for mask in masks)

    bindings, verbose = bind_tasks(
        job_id,
        len(masks),
        cpu_bind=f"mask_ldom:{list_str}",
        cpus=True,
    )
    assert_ldom_cpus(bindings, masks)
    assert_ldom_verbose(verbose, bindings, "LDMASK")


def test_cpu_bind_ldoms():
    """Test the cpu-bind type that generates locality domain masks."""
    require_ldoms(2, "distinguish a domain mask from the whole allocation")
    job_id = allocation()

    all_ldoms = 0
    for ldom_id in topo.cpu_ldom_ids:
        all_ldoms |= 1 << ldom_id

    bindings, verbose = bind_tasks(
        job_id, topo.held_cpu_cnt, cpu_bind="ldoms", overcommit=False, cpus=True
    )
    assert len(bindings) == topo.held_cpu_cnt, "Should bind every requested task"

    union = 0
    for binding in bindings:
        task_id = binding["task_id"]
        ldom_mask = topo.cpu_to_ldom_mask(binding["cpu_mask"])
        one_ldom = len(atf.mask_to_list(ldom_mask)) == 1
        assert one_ldom, f"Task {task_id} should run on one locality domain"
        union |= ldom_mask

        # Whether an automatic binding also covers the CPUs of the domain the
        # step does not hold is undocumented, so only held CPUs are compared.
        held = binding["cpu_mask"] & topo.held_cpu_mask
        expected_cpus = topo.ldom_to_cpu_mask(ldom_mask)
        assert held == expected_cpus, (
            f"Task {task_id} should run on every CPU the step holds in "
            f"locality domain {atf.mask_to_list(ldom_mask)}: "
            f"0x{held:x} != 0x{expected_cpus:x}"
        )
    assert (
        union == all_ldoms
    ), f"Tasks should cover every locality domain: 0x{union:x} != 0x{all_ldoms:x}"

    # Bind type for an automatic binding is unspecified, so assert only that
    # every task is reported with the mask it was given.
    assert_ldom_verbose(verbose, bindings)
