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
# The node configuration is not assumed equal to the CPUs the step holds. Every
# expectation is phrased against that measured set.

# The test environment, set once by setup() and read by every test.
file_prog = None
test_node = None

# cpus and mask name the same CPUs.
# task_cnt is how many tasks a step that covers the allocation launches.
Allocation = collections.namedtuple("Allocation", "job_id cpus mask task_cnt")


@pytest.fixture(scope="module", autouse=True)
def setup(taskget):
    global file_prog, test_node

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


def bind_list(form, cpu_ids):
    """Render cpu_ids as the list a map_cpu or mask_cpu binding takes."""
    strs = []
    for c in cpu_ids:
        s = ""
        if form == "map":
            s = str(c)
        elif form == "mask":
            s = f"0x{1 << c:x}"
        strs.append(s)
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


def sample_ids(ids, limit=4):
    """Return at most limit ids, always including the first and last."""
    if len(ids) <= limit:
        return ids
    last = len(ids) - 1
    picks = sorted({round(i * last / (limit - 1)) for i in range(limit)})
    return [ids[i] for i in picks]


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


def test_invalid_map_cpu_arguments(allocation):
    """Test that invalid map_cpu arguments fail appropriately."""
    # Test with NaN value
    result = atf.run_command_error(
        f"srun --jobid={allocation.job_id} -c1 --cpu-bind=verbose,map_cpu:NaN hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: NaN" in result
    ), "Should report validation error for NaN, got {result}"

    # Test with hex value (0x0)
    result = atf.run_command_error(
        f"srun --jobid={allocation.job_id} -c1 --cpu-bind=verbose,map_cpu:0x0 hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: 0x0" in result
    ), "Should report validation error for hex, got {result}"


def test_invalid_mask_cpu_arguments(allocation):
    """Test that invalid mask_cpu arguments fail appropriately."""
    result = atf.run_command_error(
        f"srun --jobid={allocation.job_id} -c1 --cpu-bind=verbose,mask_cpu:NaN hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: NaN" in result
    ), "Should report validation error for NaN"


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
@pytest.mark.parametrize("pattern", ["forward", "reverse", "alternating"])
def test_cpu_bind_patterns(allocation, form, pattern):
    """Test CPU map/mask binding in forward, reverse and alternating order."""
    order = order_ids(allocation.cpus, pattern)
    list_str = bind_list(form, order)

    task_data, verbose = bind_tasks(
        allocation, f"--cpu-bind={form}_cpu:{list_str},verbose", ntasks=len(order)
    )
    assert_binding_order(task_data, verbose, order, form.upper())
