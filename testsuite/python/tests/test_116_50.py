############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""Test CPU affinity/binding as observed on the node (--cpu-bind)."""

import collections
import re

import pytest

import atf

# Every test attaches steps with --jobid to a fresh exclusive single-node
# allocation and reads taskget, not srun output, to detect the affinity each
# task actually got. --cpu-bind=verbose is used only to confirm srun reported
# the mask it applied, and to catch one it failed to apply.
#
# CPUs the step holds are measured, not inferred. CPU IDs may not be contiguous.
# The node configuration and lscpu present distinct views of the set of CPUs,
# and are not assumed equal to the CPUs the step holds. Every expectation is
# phrased against that measured set. lscpu, through atf.get_node_cpu_topology(),
# only answers which socket or core a measured CPU belongs to.
#
# The ldom bind types are covered in test_116_68, which requires a libnuma build.

pytestmark = pytest.mark.slow

# The test environment, set once by setup() and read by every test.
file_prog = None
test_node = None
topo = None

# cpus and mask name the same CPUs.
# task_cnt is how many tasks a step that covers the allocation launches.
Allocation = collections.namedtuple("Allocation", "job_id cpus mask task_cnt")


@pytest.fixture(scope="module", autouse=True)
def setup(taskget):
    global file_prog, test_node, topo

    atf.require_config_parameter_includes("TaskPlugin", "task/affinity")

    # Four CPUs keeps the binding patterns and CPU pairs distinct.
    # The socket/core groups come from lscpu,
    # so the node has to be configured with the hardware it runs on.
    test_node = atf.require_nodes(1, [("CPUs", 4)], require_hardware=True)[0]

    atf.require_slurm_running()

    # --exclusive cannot stop a FORCE partition from oversubscribing CPUs
    partition = atf.default_partition()
    oversubscribe = atf.get_partition_parameter(partition, "OverSubscribe", "NO")
    if oversubscribe.startswith("FORCE"):
        atf.set_partition_parameter(partition, "OverSubscribe", "NO")

    file_prog = taskget

    topo = atf.get_node_cpu_topology(test_node)


@pytest.fixture(scope="function")
def allocation():
    """Create a fresh exclusive allocation and measure what a step in it holds."""
    job_id = atf.submit_job_sbatch(
        f"--nodelist={test_node} -N1 --exclusive -t5 --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Read the CPU count first: it pins the probe's task count instead of
    # depending on srun's default, which is a function of SelectTypeParameters.
    # The probe's masks are checked against it below, so two tasks landing on
    # one CPU is caught there.

    # TODO: Issue 50980. A step created too soon after the job reports RUNNING
    # is rejected outright rather than waiting, and no job state marks that
    # window, so the only reliable readiness signal is a step that got
    # through. Retry until one does.
    for _ in atf.timer():
        probe = atf.run_command(
            f"srun --jobid={job_id} -n1 printenv SLURM_CPUS_ON_NODE", quiet=True
        )
        if probe["exit_code"] == 0:
            break
    else:
        pytest.fail(f"No step could be created in the allocation: {probe['stderr']}")
    cpus_on_node = probe["stdout"].strip()

    # Measure the allocation from the step itself: --ntasks per CPU with -c1 and
    # threads binds a task to each CPU, so the union of what its tasks report is
    # what the allocation holds. Naming a bind type matters. A none binding
    # applies no affinity at all, so both the task and the verbose line report
    # an inherited cpuset instead of the allocation's CPUs.
    probe = atf.run_command(
        f"srun --jobid={job_id} --ntasks={cpus_on_node} "
        f"--cpu-bind=threads,verbose -c1 {file_prog}",
        fatal=True,
        quiet=True,
    )

    task_data = atf.parse_taskget(probe["stdout"])
    assert task_data, "Step should launch at least one task"
    assert_no_failed_binding(probe["stderr"])

    mask = 0
    for data in task_data:
        mask |= data["mask"]
    cpus = atf.mask_to_list(mask)

    assert len(cpus) == int(
        cpus_on_node
    ), f"Measured {len(cpus)} CPUs but SLURM_CPUS_ON_NODE reports {cpus_on_node}"

    yield Allocation(job_id, cpus, mask, len(task_data))


def bind_tasks(alloc, cpu_bind_args="", ntasks=None, cpus_per_task=1):
    """Run srun within the allocation and return (task data, verbose stderr).

    Pass ntasks when the task count must match the number of CPUs bound.
    Pass cpus_per_task=None when the tasks do not cover every CPU: omitting
    --cpus-per-task keeps them all in the step.
    """
    opts = cpu_bind_args
    if ntasks is not None:
        opts += f" --ntasks={ntasks}"
    if cpus_per_task is not None:
        opts += f" -c{cpus_per_task}"
    cmd = f"srun --jobid={alloc.job_id} {opts} {file_prog}"
    output = atf.run_command(cmd, fatal=True)
    return atf.parse_taskget(output["stdout"]), output["stderr"]


def bind_list(form, cpu_ids, counts=None):
    """Render cpu_ids as the list a map_cpu or mask_cpu binding takes."""
    strs = []
    for c in cpu_ids:
        s = ""
        if form == "map":
            s = str(c)
        elif form == "mask":
            s = f"0x{1 << c:x}"
        strs.append(s)
    if counts is not None:
        strs = [f"{s}*{c}" for s, c in zip(strs, counts)]
    return ",".join(strs)


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


def cover_masks(ids):
    """Return paired masks that together name every id.

    A leftover odd id gets a mask of its own, so the masks name every CPU
    the step holds, which is what an explicit mask needs.
    """
    masks = [(1 << a) | (1 << b) for a, b in zip(ids[::2], ids[1::2])]
    if len(ids) % 2:
        masks.append(1 << ids[-1])
    return masks


def sample_ids(ids, limit=4):
    """Return at most limit ids, always including the first and last."""
    if len(ids) <= limit:
        return ids
    last = len(ids) - 1
    picks = sorted({round(i * last / (limit - 1)) for i in range(limit)})
    return [ids[i] for i in picks]


def cpu_group_masks(bind_type, allocation):
    """Map each CPU in allocation to the mask of its bind_type group."""
    keyed = {}
    for cpu_id in allocation.cpus:
        cpu_topo = topo[cpu_id]
        if bind_type == "sockets":
            key = cpu_topo["socket"]
        elif bind_type == "cores":
            key = (cpu_topo["socket"], cpu_topo["core"])
        else:
            key = cpu_id
        keyed.setdefault(key, []).append(cpu_id)

    group_mask = {}
    for cpu_ids in keyed.values():
        mask = 0
        for cpu_id in cpu_ids:
            mask |= 1 << cpu_id
        for cpu_id in cpu_ids:
            group_mask[cpu_id] = mask
    return group_mask


def assert_no_failed_binding(verbose):
    assert "FAILED" not in verbose, f"srun reported a failed binding: {verbose}"


def assert_verbose(verbose, expected_masks, bind_type=None):
    """Verify slurm intended to bind each task to the mask at its rank index."""
    assert_no_failed_binding(verbose)
    ntasks = len(expected_masks)

    reported = bind_type if bind_type else r"[A-Z]+"
    pattern = rf"cpu-bind(?:-[a-z]+)?={reported}.*task +(\d+).*mask 0x([0-9a-fA-F]+)"
    found = re.findall(pattern, verbose)
    assert (
        len(found) == ntasks
    ), f"Verbose should report {ntasks} tasks, not {len(found)}."
    for task_id, mask in found:
        task_id = int(task_id)
        mask = int(mask, 16)
        assert task_id in range(ntasks), f"Verbose reports unknown task {task_id}"
        assert mask == expected_masks[task_id], (
            f"Verbose mask for task {task_id} should be "
            f"0x{expected_masks[task_id]:x}, got 0x{mask:x}"
        )


def assert_effect(task_data, expected_masks):
    """Verify the kernel honored the binding Slurm asked for."""
    expected = dict(enumerate(expected_masks))
    masks_by_task = {data["task_id"]: data["mask"] for data in task_data}
    assert (
        masks_by_task == expected
    ), f"Unexpected CPU binding: {masks_by_task} != {expected}"


def assert_binding(task_data, verbose, expected_masks, bind_type):
    assert_verbose(verbose, expected_masks, bind_type)
    assert_effect(task_data, expected_masks)


def assert_binding_order(task_data, verbose, expected_cpus, bind_type):
    masks = [1 << cpu for cpu in expected_cpus]
    assert_binding(task_data, verbose, masks, bind_type)


def test_cpu_bind_none(allocation):
    """Test that --cpu-bind=none removes the binding that would apply by default."""

    # Match ntasks to the allocation's CPU count to make an auto-binding likely.
    ntasks = len(allocation.cpus)

    # Measure the default binding first, as the basis for judging none.
    default_data, default_verbose = bind_tasks(
        allocation, "--cpu-bind=verbose", ntasks=ntasks, cpus_per_task=None
    )
    assert_no_failed_binding(default_verbose)

    default_masks = {data["task_id"]: data["mask"] for data in default_data}
    if all(mask == allocation.mask for mask in default_masks.values()):
        pytest.skip("This test requires default binding to restrict CPU masks")

    for task_id, default_mask in sorted(default_masks.items()):
        assert not default_mask & ~allocation.mask, (
            f"Task {task_id} was bound outside the allocation by default: "
            f"0x{default_mask:x} is not within 0x{allocation.mask:x}"
        )

    task_data, verbose = bind_tasks(
        allocation, "--cpu-bind=verbose,none", ntasks=ntasks, cpus_per_task=None
    )
    assert_no_failed_binding(verbose)

    none_masks = {data["task_id"]: data["mask"] for data in task_data}
    assert len(none_masks) == ntasks, f"Should launch {ntasks} tasks: {none_masks}"

    # Every task keeps the one mask the step inherited.
    inherited = set(none_masks.values())
    assert (
        len(inherited) == 1
    ), f"Unbound tasks should all share one mask, got {none_masks}"
    mask = inherited.pop()
    assert not allocation.mask & ~mask, (
        f"Unbound tasks should keep every CPU of the allocation: "
        f"0x{mask:x} does not cover 0x{allocation.mask:x}"
    )

    assert_verbose(verbose, [mask] * ntasks, bind_type="NONE")


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_single(allocation, form):
    """Test that a single-element map/mask_cpu binds every task to that CPU."""
    for cpu_id in sample_ids(allocation.cpus):
        list_str = bind_list(form, [cpu_id])
        task_data, verbose = bind_tasks(
            allocation, f"--cpu-bind={form}_cpu:{list_str},verbose"
        )
        assert_binding_order(
            task_data, verbose, [cpu_id] * allocation.task_cnt, form.upper()
        )


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_reuse(allocation, form):
    """Test that a multi-element map/mask_cpu list wraps to its first element."""
    ids = allocation.cpus[:2]
    list_str = bind_list(form, ids)
    task_data, verbose = bind_tasks(
        allocation,
        f"--cpu-bind={form}_cpu:{list_str},verbose",
        ntasks=len(ids) + 1,
        cpus_per_task=None,
    )
    assert_binding_order(task_data, verbose, [ids[0], ids[1], ids[0]], form.upper())


def test_mask_cpu_fat_masks(allocation):
    """Test that mask_cpu binds each task to every CPU in its mask."""
    assert len(allocation.cpus) >= 2, "Should discover at least 2 bindable CPUs"

    masks = cover_masks(allocation.cpus)
    list_str = ",".join(f"0x{mask:x}" for mask in masks)

    task_data, verbose = bind_tasks(
        allocation,
        f"--cpu-bind=mask_cpu:{list_str},verbose",
        ntasks=len(masks),
        cpus_per_task=None,
    )
    assert_binding(task_data, verbose, masks, "MASK")


@pytest.mark.parametrize("form", ["map", "mask"])
@pytest.mark.parametrize("pattern", ["forward", "reverse", "alternating"])
def test_cpu_bind_patterns(allocation, form, pattern):
    """Test CPU map/mask binding in forward, reverse and alternating order."""
    order = order_ids(allocation.cpus, pattern)
    list_str = bind_list(form, order)

    task_data, verbose = bind_tasks(
        allocation, f"--cpu-bind={form}_cpu:{list_str},verbose", ntasks=len(order)
    )
    assert_binding_order(task_data, verbose, order, form.upper())


@pytest.mark.parametrize("form", ["map", "mask"])
def test_cpu_bind_repetition(allocation, form):
    """Test the '*<count>' repetition syntax documented for map_cpu/mask_cpu."""
    cpus = allocation.cpus[:2]
    counts = [len(allocation.cpus) // len(cpus)] * len(cpus)
    counts[-1] += len(allocation.cpus) - sum(counts)

    expected = [cpu_id for cpu_id, cnt in zip(cpus, counts) for _ in range(cnt)]
    list_str = bind_list(form, cpus, counts)

    task_data, verbose = bind_tasks(
        allocation,
        f"--cpu-bind='{form}_cpu:{list_str},verbose'",
        ntasks=len(expected),
    )
    assert_binding_order(task_data, verbose, expected, form.upper())


@pytest.mark.parametrize("bind_type", ["sockets", "cores", "threads"])
def test_cpu_bind_auto_generated(allocation, bind_type):
    """Test the cpu-bind types that generate the task binding automatically."""
    group_mask = cpu_group_masks(bind_type, allocation)
    if len(set(group_mask.values())) < 2:
        pytest.skip(f"This test requires more than one group of {bind_type}")

    ntasks = len(allocation.cpus)
    task_data, verbose = bind_tasks(
        allocation,
        f"--cpu-bind={bind_type},verbose",
        ntasks=ntasks,
        cpus_per_task=1,
    )
    assert len(task_data) == ntasks, "Should bind every requested task"

    # Which group a rank lands in is not documented, so read the group off
    # the CPUs the task holds and expect the rest of that group with them.
    # That asserts alignment: a mask is exactly one group, never a part of one
    # and never spanning two. The groups do not overlap, so every CPU in a
    # mask answers with the same group and the first one is as good as any.
    expected_masks = []
    for data in sorted(task_data, key=lambda data: data["task_id"]):
        task_id, mask = data["task_id"], data["mask"]
        assert mask, f"Task {task_id} should be bound to at least one CPU"
        assert (
            not mask & ~allocation.mask
        ), f"Task {task_id} bound outside the allocation"
        expected_masks.append(group_mask[atf.mask_to_list(mask)[0]])

    # And covering: one task per CPU, so every group should appear once per
    # CPU it holds. The masks of two tasks sharing a group are equal, not
    # disjoint, which is why this compares multisets.
    assert sorted(expected_masks) == sorted(group_mask.values()), (
        f"Tasks should be spread over the {bind_type} the allocation holds: "
        f"{[hex(m) for m in sorted(expected_masks)]} != "
        f"{[hex(m) for m in sorted(group_mask.values())]}"
    )

    # Bind type for an automatic binding is unspecified, ignore it.
    assert_binding(task_data, verbose, expected_masks, bind_type=None)


def test_cpu_bind_quiet(allocation):
    """Test that quiet binds without reporting."""

    cpu_id = allocation.cpus[0]
    cpu_bind = f"--cpu-bind=quiet,map_cpu:{bind_list('map', [cpu_id])}"
    task_data, verbose = bind_tasks(allocation, cpu_bind)
    assert_effect(task_data, [1 << cpu_id] * allocation.task_cnt)

    assert (
        "cpu-bind" not in verbose
    ), f"Binding should not be reported without verbose: {verbose}"
