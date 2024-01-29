############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def make_and_run_bash(lines: list[str]) -> None:
    """Make and run the bash script.
    Input is a list of lines to be run as bash script."""
    script_name = "script.sh"
    script = "\n".join(lines)
    atf.make_bash_script(script_name, script)
    atf.run_command(f"bash {script_name}")


@pytest.mark.parametrize(
    "command, phrase",
    [
        ("sinfo", "NODELIST"),
        ("scontrol show node", "NodeName="),
    ],
)
def test_parallel(command, phrase):
    """Test that sinfo and scontrol can be run in parallel. We submit
    1000 user commands to slurm to make sure that it doesn't crash. We then
    check the output that the correct number of commands were run."""

    script_out = str(atf.module_tmp_path / f"{command[:5]}.out")
    # Cancel all jobs so that the the queue is empty
    atf.cancel_all_jobs()

    script_lines = [
        "for i in $(seq 1 1000)",
        f"    do {command} &",
        f"done > {script_out}",
        "wait",
    ]
    make_and_run_bash(script_lines)

    output = atf.run_command_output(f"cat {script_out} | grep -c '{phrase}'")
    assert (
        int(output) == 1000
    ), f"We expected 1000 commands to be run in parallel, but got {output}"


def test_squeue_parallel():
    """Test for when lots of squeue calls are made, all the commands still run
    correctly. To test this we submit 100 jobs, then we run 1000 squeue
    commands and make sure that all 1000 worked and produced output."""

    script_out = str(atf.module_tmp_path / "squeue.out")
    # Cancel all jobs so that the the queue is empty
    atf.cancel_all_jobs()
    # Submit 100 jobs to fill up the queue
    for i in range(100):
        atf.submit_job_sbatch("--wrap='sleep 100'")

    script_lines = [
        "for i in $(seq 1 1000)",
        "    do squeue &",
        f"done > {script_out}",
        "wait",
    ]
    make_and_run_bash(script_lines)

    output = atf.run_command_output(f"cat {script_out} | grep -c 'JOBID'")
    assert (
        int(output) == 1000
    ), f"We expected 1000 user commands to run, but got {int(output)}"


def test_show_jobs_parallel():
    """Test that scontrol show job works in parallel. We submit one job and
    then run 1000 'scontrol show job {job_id} &' commands to make sure that all
    of them provide the correct output"""

    script_out = str(atf.module_tmp_path / "job.out")
    # Cancel all jobs so that the the queue is empty
    atf.cancel_all_jobs()
    job_id = atf.submit_job_srun("true")

    script_lines = [
        "for i in $(seq 1 1000)",
        f"    do scontrol show job {job_id} &",
        f"done > {script_out}",
        "wait",
    ]
    make_and_run_bash(script_lines)

    output = atf.run_command_output(f'cat {script_out} | grep -c "JobId="')
    assert (
        int(output) == 1000
    ), f"We expected 1000 commands to be run in parallel, but got {output}"
