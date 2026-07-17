############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that --kill-on-bad-exit controls peer termination for async steps."""

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/srun",
        reason="Issue 50739: srun --async was added in 26.05",
    )
    atf.require_version(
        (26, 5),
        "sbin/slurmd",
        reason="Issue 50739: async --kill-on-bad-exit enforcement (slurmstepd) was added in 26.05",
    )
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_accounting()
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "kill_flag,expect_kill",
    [
        ("--kill-on-bad-exit", True),
        ("--kill-on-bad-exit=0", False),
    ],
)
def test_async_kill_on_bad_exit(kill_flag, expect_kill):
    """--kill-on-bad-exit controls peer termination for async steps.

    A task exits non-zero mid-run. With the flag enabled the peers must be
    killed before they reach a post-sleep marker; with =0 the peers must
    survive and print it. The =0 case isolates the flag as the cause of the
    kill regardless of the site KillOnBadExit default.
    """
    error_text = "SHOULD_NOT_BE_HERE"
    started_text = "started-"
    num_tasks = 4
    bad_task = num_tasks - 2

    tag = "kill" if expect_kill else "nokill"
    out_file = f"kill_bad_exit_{tag}.out"
    task_script = f"kill_bad_exit_{tag}.sh"
    atf.make_bash_script(
        task_script,
        f"""echo "{started_text}$SLURM_PROCID"
if [ "$SLURM_PROCID" -eq {bad_task} ]; then
    # Wait until all tasks prints its "{started_text}N" before exiting bad_task
    for i in $(seq 0 $(({num_tasks} - 1))); do
        until grep -q "{started_text}$i" {out_file}; do
            sleep 0.1
        done
    done
    exit 2
fi
sleep 15
echo "{error_text}"
""",
    )

    script = f"kill_bad_exit_submit_{tag}.sh"
    atf.make_bash_script(
        script,
        f"srun --async --stepmgr {kill_flag} -n{num_tasks} -O -o {out_file} {task_script}\nswait\n",
    )
    job_id = atf.submit_job_sbatch(
        f"-N1 -n{num_tasks} -O -t1 --stepmgr --job-name=test_116_58 {script}",
        fatal=True,
    )
    atf.wait_for_step_accounted(job_id, 0, fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    text = ""
    for _ in atf.timer(fatal=True):
        text = atf.run_command_output(f"cat {out_file}")
        started_all = all(f"{started_text}{i}" in text for i in range(num_tasks))
        if started_all and (expect_kill or error_text in text):
            break

    if expect_kill:
        assert error_text not in text, (
            f"--kill-on-bad-exit should kill peers before the {error_text!r} "
            f"line is written; got {text!r}"
        )
    else:
        assert error_text in text, (
            f"--kill-on-bad-exit=0 should let peers run to completion and print "
            f"{error_text!r}; got {text!r}"
        )
        # Peers ran to completion (rc 0), so the failing task's exit code (2)
        # surfaces to accounting rather than any peer's signal. A completed step
        # is gone from the live controller (scontrol show steps), so the
        # ExitCode must be read from sacct; format is "<rc>:<signal>".
        exit_code = atf.run_command_output(
            f"sacct -j {job_id}.0 --noheader -P -o ExitCode",
            fatal=True,
        ).strip()
        assert exit_code, f"sacct returned no ExitCode for step {job_id}.0"
        assert exit_code.splitlines()[0] == "2:0", (
            "The step should report the failing task's exit code (2:0), "
            f"not a peer's signal; got {exit_code!r}"
        )
