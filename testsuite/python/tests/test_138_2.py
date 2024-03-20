############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

script_name = "./script.sh"
script_out = "./script.out"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs():
    yield
    atf.cancel_all_jobs()


def make_and_run_bash(command) -> None:
    """Make and run the bash script to run a command 1000 times.
    Input is a list of lines to be run as bash script."""
    atf.make_bash_script(
        script_name,
        f"""
        for i in $(seq 1 1000)
            do {command} &
        done > {script_out}
        wait
        """,
    )
    atf.run_command(script_name)


@pytest.mark.parametrize(
    "command, phrase",
    [
        ("sinfo --Format=NODELIST", "^NODELIST"),
        ("scontrol show node | head -1", "^NodeName="),
        ("squeue --only-job-state --Format=NODELIST", "^NODELIST"),
    ],
)
def test_parallel(command, phrase):
    """Test that sinfo and scontrol can be run in parallel. We submit
    1000 user commands to slurm to make sure that it doesn't crash. We then
    check the output that the correct number of commands were run."""

    make_and_run_bash(command)

    output = atf.run_command_output(f"cat {script_out} | grep -c '{phrase}'")
    assert (
        int(output) == 1000
    ), f"We expected 1000 commands to be run in parallel, but got {output}"


def test_squeue_parallel():
    """Test for when lots of squeue calls are made, all the commands still run
    correctly. To test this we submit 100 jobs, then we run 1000 squeue
    commands and make sure that all 1000 worked and produced output."""

    # Submit 100 jobs to fill up the queue
    for i in range(100):
        atf.submit_job_sbatch("--wrap='sleep 100'")

    make_and_run_bash("squeue")

    output = atf.run_command_output(f"cat {script_out} | grep -c 'JOBID'")
    assert (
        int(output) == 1000
    ), f"We expected 1000 user commands to run, but got {int(output)}"


def test_show_jobs_parallel():
    """Test that scontrol show job works in parallel. We submit one job and
    then run 1000 'scontrol show job {job_id} &' commands to make sure that all
    of them provide the correct output"""

    job_id = atf.submit_job_srun("true")

    make_and_run_bash(f"scontrol show job {job_id}")

    output = atf.run_command_output(f'cat {script_out} | grep -c "JobId="')
    assert (
        int(output) == 1000
    ), f"We expected 1000 commands to be run in parallel, but got {output}"
