############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Stepmgr/async-step integration tests.

These tests pin operational properties of async steps that go beyond the
srun CLI surface (covered in test_116_55.py): scontrol visibility of the
running step, and stepmgr state persistence across `scontrol reconfigure`.
"""

import re

import pytest

import atf

PARSABLE_RE = re.compile(r"^(\d+)\.(\d+)$", re.MULTILINE)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/srun",
        reason="Issue 50739: srun --async was added in 26.05",
    )
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_accounting()
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


def test_async_step_state_visible_in_scontrol():
    """A running async step is reported by `scontrol show steps` with State=RUNNING.

    Confirms stepmgr maintains the step record after srun has exited and
    that the basic JobId/StepId fields match the values srun reported.
    """
    out_file = "state.out"

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 -t1 --wrap 'srun --async -o {out_file} sleep 30; sleep 30'",
        fatal=True,
    )
    step_key = f"{job_id}.0"

    state = None
    for _t in atf.timer(quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if step_key in steps:
            state = steps[step_key].get("State")
            if state == "RUNNING":
                break
    else:
        pytest.fail(
            f"Async step {step_key} did not reach RUNNING in scontrol; "
            f"last observed State={state!r}"
        )

    step = steps[step_key]
    assert step["StepId"] == step_key, f"Step StepId should be {step_key}: {step!r}"

    atf.run_command(f"scancel {job_id}", quiet=True)
    atf.wait_for_job_state(job_id, "DONE")


def test_async_step_survives_reconfigure():
    """A queued async step still launches after `scontrol reconfigure`.

    Stepmgr state (the pending async step held in the job's step list)
    must be preserved across a controller reconfigure.
    """
    out1 = "rcfg1.out"
    out2 = "rcfg2.out"

    marker = "rcfg_marker"

    script = "rcfg_submit.sh"
    release = "rcfg_release"
    # Step 1 holds the exclusive resource until the release file is written,
    # giving deterministic control over when step 2 can promote from PENDING.
    atf.make_bash_script(
        script,
        f"""srun --async --parsable --exclusive -o {out1} \\
    bash -c 'until [ -f {release} ]; do sleep 0.2; done' > {marker} 2>&1
srun --async --parsable --exclusive -o {out2} bash -c 'echo second-ran' >> {marker} 2>&1
swait
""",
    )
    job_id = atf.submit_job_sbatch(f"-N1 -n1 -t2 {script}", fatal=True)
    atf.wait_for_file(marker, fatal=True)

    text = ""
    for _t in atf.timer():
        text = atf.run_command_output(f"cat {marker}", quiet=True)
        if len(PARSABLE_RE.findall(text)) >= 2:
            break
    else:
        pytest.fail(f"Expected two parsable step-id lines, got: {text!r}")

    matches = PARSABLE_RE.findall(text)
    second_step = int(matches[1][1])
    second_key = f"{job_id}.{second_step}"

    saw_pending = False
    # Poll fast: the queued step may be PENDING only briefly.
    for _t in atf.timer(poll_interval=0.5, quiet=True):
        steps = atf.get_steps(job_id, quiet=True)
        if steps.get(second_key, {}).get("State") == "PENDING":
            saw_pending = True
            break
    assert saw_pending, (
        f"Second async step {second_key} did not reach PENDING before "
        f"reconfigure; cannot exercise the survives-reconfigure path"
    )

    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    steps_after = atf.get_steps(job_id, quiet=True)
    state_after = steps_after.get(second_key, {}).get("State")
    assert state_after == "PENDING", (
        f"Second async step {second_key} should still be PENDING "
        f"immediately after scontrol reconfigure; got State={state_after!r}"
    )

    atf.run_command(f"touch {release}", fatal=True)

    assert atf.wait_for_step_accounted(
        job_id, second_step, fatal=True
    ), f"async step {job_id}.{second_step} should be recorded in accounting"
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(out2, fatal=True)
    out2_text = atf.run_command_output(f"cat {out2}", fatal=True)
    assert (
        "second-ran" in out2_text
    ), f"Queued async step should run after reconfigure; got {out2_text!r}"
