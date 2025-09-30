############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import time
import os
import re

file_out1 = "output1"
file_out2 = "output2"

job_cpus = 2
job_mem = 2

# Big enough to avoid busy systems to detect false suspend times
suspend_time = 5

# To wait for some file content
file_pattern = re.compile(r"01\s+\d+\n" r"02\s+\d+\n")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_excludes("PreemptMode", "GANG")

    # Ensure that memory is tracked
    if atf.get_config_parameter("SelectType", live=False) == "select/linear":
        atf.require_config_parameter("SelectTypeParameters", "CR_Memory")
    else:
        atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")

    # Jobs will use all CPUs and half of the memory
    atf.require_nodes(1, [("CPUs", job_cpus), ("RealMemory", job_mem * 2)])

    atf.require_slurm_running()


# Helper fixtures
@pytest.fixture(scope="module")
def node(setup):
    return next(iter(atf.nodes))


@pytest.fixture(scope="module")
def job_script():
    """Create the test program as a bash script."""
    job_script = "job_script.sh"
    atf.make_bash_script(
        job_script,
        f"""
ts_prev=$(date +%s)
i=1
while [ $i -le 30 ]; do
    ts_curr=$(date +%s)
    printf "%02d  %s" $i $ts_curr
    if (( ts_curr > ts_prev + {suspend_time} )); then
        printf " (JobSuspended)"
        # Run only for extra 2s
        i=28
    fi
    printf "\n"

    sleep 1

    ts_prev=$ts_curr
    ((i++))
done
echo "AllDone"
""",
    )
    return os.path.abspath(job_script)


def test_job_suspend_resume(job_script, node):
    """Test job suspend and resume functionality."""

    # Submit job1 with a srun step, and ensure it runs
    job_id1 = atf.submit_job_sbatch(
        f"-N1 -t2 --output={file_out1} -w {node} -c {job_cpus} --mem={job_mem} --wrap='srun {job_script}'"
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)

    # Submit job2 only with a batch step, and ensure it has no resources to run
    job_id2 = atf.submit_job_sbatch(
        f"-N1 -t2 --output={file_out2} -w {node} -c {job_cpus} --mem={job_mem} {job_script}"
    )
    atf.wait_for_job_state(job_id2, "PENDING", desired_reason="Resources", fatal=True)

    # Before suspending job1, ensure that it already printed something
    atf.repeat_until(
        lambda: atf.run_command_output(f"cat {file_out1}"),
        lambda out: file_pattern.match(out),
        fatal=True,
    )

    # Suspend job1, and verify it is suspended and job2 starts running
    atf.run_command(
        f"scontrol suspend {job_id1}", user=atf.properties["slurm-user"], fatal=True
    )
    assert atf.wait_for_job_state(
        job_id1, "SUSPENDED"
    ), f"Job {job_id1} should be SUSPENDED"
    assert atf.wait_for_job_state(
        job_id2, "RUNNING"
    ), f"Job {job_id2} should start RUNNING"

    # Give sometime to job1 so it can detect that it was suspended
    time.sleep(suspend_time + 1)

    # Before suspending job2, ensure that it already printed something
    atf.repeat_until(
        lambda: atf.run_command_output(f"cat {file_out2}"),
        lambda out: file_pattern.match(out),
        fatal=True,
    )

    # Now switch suspend/running between job1 and job2
    atf.run_command(
        f"scontrol suspend {job_id2}", user=atf.properties["slurm-user"], fatal=True
    )
    atf.run_command(
        f"scontrol resume {job_id1}", user=atf.properties["slurm-user"], fatal=True
    )
    assert atf.wait_for_job_state(
        job_id1, "RUNNING"
    ), f"Job {job_id1} should start RUNNING again"
    assert atf.wait_for_job_state(
        job_id2, "SUSPENDED"
    ), f"Job {job_id2} should be SUSPENDED"

    # Give some time to job2 so it can detect it was suspended
    time.sleep(suspend_time + 1)

    # Let both jobs run until they end
    atf.run_command(
        f"scontrol resume {job_id2}", user=atf.properties["slurm-user"], fatal=True
    )

    atf.wait_for_job_state(job_id1, "DONE", fatal=True)
    atf.wait_for_job_state(job_id2, "DONE", fatal=True)

    # Finally, check that the output files reflect jobs were suspended once
    # and resumed so they finished normally
    output1 = atf.run_command_output(f"cat {file_out1}", fatal=True)
    output2 = atf.run_command_output(f"cat {file_out2}", fatal=True)

    assert (
        output1.count("JobSuspended") == 1
    ), f"Job {job_id1} should detectd being suspended"
    assert (
        output2.count("JobSuspended") == 1
    ), f"Job {job_id2} should detectd being suspended"

    assert "AllDone" in output1, f"Job {job_id1} should finish properly"
    assert "AllDone" in output2, f"Job {job_id2} should finish properly"
