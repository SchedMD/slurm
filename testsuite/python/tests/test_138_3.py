############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
from concurrent.futures import ThreadPoolExecutor, as_completed
import time

max_threads = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(max_threads)
    atf.require_slurm_running()


# Defines the work done by each executor thread in the stress test
def run_stress_task(thread_id, iterations, sleep_time):
    """
    Simulates the work of a single stress thread.
    Each thread runs a series of Slurm commands (sinfo, sbatch, squeue) in a loop.
    This function is designed to be run by an executor thread (ThreadPoolExecutor).

    Args:
        thread_id (int): A unique identifier for this run/thread.
        iterations (int): The number of times to loop through the command sequence.
        sleep_time (float): The duration in seconds to sleep between command executions.

    Returns:
        tuple: A tuple containing the thread_id and an exit code (0 for success, non-zero for failure).
    """
    # Loop for the specified number of iterations
    for i in range(1, iterations + 1):
        # Run sinfo command
        result_sinfo = atf.run_command("sinfo")
        if result_sinfo["exit_code"] != 0:
            return thread_id, result_sinfo["exit_code"]

        # Pause execution for the specified sleep time
        time.sleep(sleep_time)

        # Construct the sbatch command to execute /bin/true.
        # Job name includes thread_id and iteration number for uniqueness.
        # Node and task counts for sbatch vary with the iteration number.
        sbatch_args = f"--job-name=test_task{thread_id}_{i} -N1-{i} -n{i} -O -s -t1 --wrap='/bin/true'"
        job_id = atf.submit_job_sbatch(sbatch_args)
        if job_id == 0:
            return thread_id, 1

        # Pause execution
        time.sleep(sleep_time)

        # Run squeue command
        result_squeue = atf.run_command("squeue")
        if result_squeue["exit_code"] != 0:
            return thread_id, result_squeue["exit_code"]

        # Pause execution
        time.sleep(sleep_time)

    # If all iterations complete successfully, return 0 for success
    return thread_id, 0


@pytest.mark.parametrize(
    "threads_count, iterations, sleep_time",
    [
        (2, 3, 1),  # A smaller run for quick testing
        (
            max_threads,
            int(max_threads / 2),
            1,
        ),  # Parameters mimicking the original expect test's default values
        (5, 2, 0.5),  # A quicker, moderate run
    ],
)
def test_stress_slurm_commands(threads_count, iterations, sleep_time):
    """
    Stress test multiple simultaneous Slurm commands via multiple threads.
    Each thread executes the `run_stress_task` function, which runs sinfo,
    sbatch /bin/true, and squeue in a loop.
    The test verifies that all concurrently run tasks complete successfully.
    """
    # Calculate a timeout for the entire test.
    # This is based on the original expect script's timeout logic: max_job_delay * iterations * thread_cnt.
    # Assuming max_job_delay is 120 seconds as often seen in Slurm expect tests.
    total_timeout = 120 * iterations * threads_count

    # Initialize counters and lists for tracking task results
    successful_tasks = 0
    failed_thread_details = []

    # Use ThreadPoolExecutor to run tasks concurrently
    with ThreadPoolExecutor(max_workers=threads_count) as executor:
        # Create a dictionary to map future objects to their thread_ids
        # This helps in identifying which thread corresponds to a completed future
        futures = {
            executor.submit(
                run_stress_task,
                thread_id,
                iterations,
                sleep_time,
            ): thread_id
            for thread_id in range(
                threads_count
            )  # Create `threads_count` number of threads
        }

        try:
            # Wait for threads to complete, with an overall timeout for the block
            # as_completed yields futures as they complete (or raise exceptions)
            for future in as_completed(futures, timeout=total_timeout):
                # Get the original thread_id for this future
                thread_id_from_future_map = futures[future]
                try:
                    # Get the result from the completed future. This will
                    # re-raise any exception that occurred in the worker thread.
                    returned_thread_id, exit_code = future.result()
                    if exit_code == 0:
                        successful_tasks += 1
                    else:
                        # If the thread reported a non-zero exit code, record it
                        # as a failure
                        error_msg = f"Thread {returned_thread_id} failed with exit code {exit_code}."
                        failed_thread_details.append(error_msg)
                except Exception as e:
                    # If future.result() raises an exception (e.g., an unhandled
                    # error in run_stress_task), record this as a failure.
                    error_msg = f"Thread {thread_id_from_future_map} generated an exception: {type(e).__name__} - {e}"
                    failed_thread_details.append(error_msg)
        except TimeoutError:
            # If as_completed times out, it means not all tasks finished within total_timeout
            error_msg = f"Stress test timed out after {total_timeout:.2f} seconds. Not all tasks completed."
            failed_thread_details.append(error_msg)
            # Attempt to cancel any tasks that are still running
            for fut in futures:
                if not fut.done():
                    fut.cancel()
        except Exception as e:
            # Catch any other unexpected errors during as_completed processing
            error_msg = f"An unexpected error occurred during task execution: {type(e).__name__} - {e}"
            failed_thread_details.append(error_msg)

    # Assert that all tasks completed successfully
    # If not, provide a detailed error message listing the failures.
    assert (
        successful_tasks == threads_count
    ), f"Only {successful_tasks} of {threads_count} tasks passed. Failures: {'; '.join(failed_thread_details)}"
