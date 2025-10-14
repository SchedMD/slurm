############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import json
import pytest
import re


# Global variables that will be set by tests
file_prog = None
task_cnt = None
job_id = None


@pytest.fixture(scope="module", autouse=True)
def setup(taskget):
    global file_prog

    # Require CPU affinity support
    atf.require_config_parameter_includes("TaskPlugin", "affinity")

    # Make sure there's no OverSubscribe=FORCE
    atf.require_config_parameter_excludes("OverSubscribe", "FORCE")

    atf.require_nodes(1, [("CPUs", 4)])

    file_prog = taskget

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def allocation():
    """Create a single allocation to be used by all tests."""
    global job_id, task_cnt

    # Create allocation with exclusive access to node
    job_id = atf.submit_job_salloc("-N1 --exclusive -t5", background=True, fatal=True)

    # Wait for the job to be running
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Determine task count by running basic test within the allocation
    task_data = run_affinity_test_in_allocation()
    task_cnt = len(task_data)

    if task_cnt > 32:
        pytest.fail("Cannot work with more than 32-bit numbers")

    yield job_id


def uint2hex(value):
    """Convert integer to hexadecimal string format (8 digits with leading zeros)."""
    return f"{value:08x}"


def parse_task_output(output):
    """Parse task output and return task_id to mask mapping."""
    task_data = {}
    for line in output.strip().split("\n"):
        if line.strip():  # Skip empty lines
            data = json.loads(line)
            task_data[data["task_id"]] = data["mask"]
    return task_data


def run_affinity_test_in_allocation(cpu_bind_args=""):
    """Run srun within the allocation with cpu-bind options and return parsed task data."""
    # Run srun within the existing allocation
    cmd = f"srun --jobid={job_id} -c1 {cpu_bind_args} {file_prog}"
    output = atf.run_command_output(cmd, fatal=True)
    return parse_task_output(output)


def get_available_cpu_ids():
    """Get list of CPU IDs that are actually allocated to the job."""
    # Run a basic test to see what CPUs tasks actually get
    task_data = run_affinity_test_in_allocation()

    # Extract unique CPU IDs from the masks
    cpu_ids = []
    for mask in task_data.values():
        # Find which CPU this mask represents (find the bit position)
        cpu_id = 0
        temp_mask = mask
        while temp_mask > 1:
            temp_mask >>= 1
            cpu_id += 1
        if mask == (1 << cpu_id):  # Ensure it's a single-bit mask
            cpu_ids.append(cpu_id)

    return sorted(set(cpu_ids))


def test_basic_affinity(allocation):
    """Test basic task affinity without cpu-bind options."""
    # Verify we got reasonable task count and mask
    assert task_cnt > 0, "Should have at least one task"

    # Run a basic affinity test to verify it works
    task_data = run_affinity_test_in_allocation()

    # Verify we got the expected number of tasks
    assert (
        len(task_data) == task_cnt
    ), f"Should have {task_cnt} tasks, got {len(task_data)}"

    # Get the mask from any task
    mask = list(task_data.values())[0] if task_data else 0
    assert mask > 0, "Mask should be non-zero"


def test_rank_affinity(allocation):
    """Test affinity with --cpu-bind=rank (note: rank binding is obsolete but test should still work)."""
    # Get the basic affinity allocation to determine what CPUs are actually available
    basic_task_data = run_affinity_test_in_allocation()

    # Calculate expected mask based on actual allocated CPUs from basic run
    # Since rank binding is obsolete, it should behave like a normal affinity run
    expected_mask = sum(basic_task_data.values())

    # This might generate a warning about rank binding being obsolete, but should still work
    result = atf.run_command_output(
        f"srun --jobid={job_id} -c1 --cpu-bind=rank {file_prog}"
    )

    # Parse the output despite potential warnings
    task_data = parse_task_output(result)

    # Sum all the masks
    total_mask = sum(task_data.values())

    assert (
        total_mask == expected_mask
    ), f"Affinity mask should be consistent for a job step with affinity: {total_mask} != {expected_mask}"


def test_invalid_map_cpu_arguments(allocation):
    """Test that invalid map_cpu arguments fail appropriately."""
    # Test with NaN value
    result = atf.run_command_error(
        f"srun --jobid={job_id} -c1 --cpu-bind=verbose,map_cpu:NaN hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: NaN" in result
    ), "Should report validation error for NaN, got {result}"

    # Test with hex value (0x0)
    result = atf.run_command_error(
        f"srun --jobid={job_id} -c1 --cpu-bind=verbose,map_cpu:0x0 hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: 0x0" in result
    ), "Should report validation error for hex, got {result}"


def test_map_cpu_all_tasks_cpu_zero(allocation):
    """Test --cpu-bind=map_cpu:0 binding all tasks to CPU 0."""
    task_data = run_affinity_test_in_allocation("--cpu-bind=verbose,map_cpu:0")

    # All tasks should be on CPU 0 (mask = 1)
    total_mask = sum(task_data.values())
    expected_total = task_cnt  # Each task has mask=1, so sum = task_cnt

    assert (
        total_mask == expected_total
    ), f"Affinity mask should be consistent for all tasks on CPU 0: {total_mask} != {expected_total}"

    # Check verbose output
    result = atf.run_command_error(
        f"srun --jobid={job_id} -c1 --cpu-bind=verbose,map_cpu:0 {file_prog}",
        fatal=True,
    )

    # Verbose output goes to stderr
    verbose_count = len(
        re.findall(
            r"cpu-bind=MAP|cpu-bind-cores=MAP|cpu-bind-sockets=MAP|cpu-bind-threads=MAP",
            result,
        )
    )

    # Both task/affinity and task/cpu may generate verbose messages,
    # so check for double messages in case both plugins are configured.
    assert (
        verbose_count == task_cnt or verbose_count == task_cnt * 2
    ), f"Verbose messages count should be consistent: {verbose_count} != {task_cnt}"


def test_map_cpu_individual_cpus(allocation):
    """Test binding all tasks to individual CPUs using map_cpu for all available CPUs."""
    available_cpus = get_available_cpu_ids()

    for cpu_id in available_cpus:
        task_data = run_affinity_test_in_allocation(f"--cpu-bind=map_cpu:{cpu_id}")

        mask = 1 << cpu_id
        expected_total = task_cnt * mask
        total_mask = sum(task_data.values())

        assert (
            total_mask == expected_total
        ), f"Affinity mask should be consistent for all tasks bound to CPU {cpu_id}: {total_mask} != {expected_total}"


def test_invalid_mask_cpu_arguments(allocation):
    """Test that invalid mask_cpu arguments fail appropriately."""
    result = atf.run_command_error(
        f"srun --jobid={job_id} -c1 --cpu-bind=verbose,mask_cpu:NaN hostname",
        xfail=True,
        fatal=True,
    )
    assert (
        "Failed to validate number: NaN" in result
    ), "Should report validation error for NaN"


def test_mask_cpu_individual_cpus(allocation):
    """Test binding all tasks to individual CPUs using mask_cpu for all available CPUs."""
    available_cpus = get_available_cpu_ids()

    for cpu_id in available_cpus:
        mask = 1 << cpu_id
        mask_str = uint2hex(mask)

        task_data = run_affinity_test_in_allocation(f"--cpu-bind=mask_cpu:{mask_str}")

        expected_total = task_cnt * mask
        total_mask = sum(task_data.values())

        assert (
            total_mask == expected_total
        ), f"Affinity mask should be consistent for all tasks bound to CPU {cpu_id} with mask: {total_mask} != {expected_total}"


def test_cpu_map_patterns(allocation):
    """Test various CPU mapping patterns (forward, reverse, alternating)."""
    available_cpus = get_available_cpu_ids()

    # Generate forward pattern using available CPUs
    fwd_map = ",".join(str(i) for i in available_cpus)

    # Generate reverse pattern using available CPUs
    rev_map = ",".join(str(i) for i in reversed(available_cpus))

    # Generate alternating pattern - odd positions descending, then even ascending
    odd_cpus = [str(i) for i in reversed(available_cpus) if i % 2 == 1]
    even_cpus = [str(i) for i in available_cpus if i % 2 == 0]
    alt_map = ",".join(odd_cpus + even_cpus)

    # Calculate expected full mask based on available CPUs
    full_mask = sum(1 << cpu_id for cpu_id in available_cpus)

    # Test forward map
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=map_cpu:{fwd_map}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Forward map affinity should cover all available CPUs: {total_mask} != {full_mask}"

    # Test reverse map
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=map_cpu:{rev_map}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Reverse map affinity should cover all available CPUs: {total_mask} != {full_mask}"

    # Test alternating map
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=map_cpu:{alt_map}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Alternating map affinity should cover all available CPUs: {total_mask} != {full_mask}"


def test_cpu_mask_patterns(allocation):
    """Test various CPU masking patterns (forward, reverse, alternating)."""
    available_cpus = get_available_cpu_ids()

    # Generate forward mask pattern using available CPUs
    fwd_mask = ",".join([uint2hex(1 << i) for i in available_cpus])

    # Generate reverse mask pattern using available CPUs
    rev_mask = ",".join([uint2hex(1 << i) for i in reversed(available_cpus)])

    # Generate alternating mask pattern - odd positions descending, then even ascending
    odd_cpus = [uint2hex(1 << i) for i in reversed(available_cpus) if i % 2 == 1]
    even_cpus = [uint2hex(1 << i) for i in available_cpus if i % 2 == 0]
    alt_mask = ",".join(odd_cpus + even_cpus)

    # Calculate expected full mask based on available CPUs
    full_mask = sum(1 << cpu_id for cpu_id in available_cpus)

    # Test forward masks
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=mask_cpu:{fwd_mask}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Forward mask affinity should cover all available CPUs: {total_mask} != {full_mask}"

    # Test reverse masks
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=mask_cpu:{rev_mask}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Reverse mask affinity should cover all available CPUs: {total_mask} != {full_mask}"

    # Test alternating masks
    task_data = run_affinity_test_in_allocation(f"--cpu-bind=mask_cpu:{alt_mask}")
    total_mask = sum(task_data.values())
    assert (
        total_mask == full_mask
    ), f"Alternating mask affinity should cover all available CPUs: {total_mask} != {full_mask}"
