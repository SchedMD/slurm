############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify controller cancellation reaches srun while its prolog holds launch."""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/srun",
        reason="Issue 50935: Prelaunch RPC receipt logging requires the srun conmgr listener",
    )
    atf.require_slurm_running()


def test_cancel_during_srun_prolog():
    """Retain a cancellation received before launch RPC handlers are ready."""
    ready = "prolog_ready"
    release = "prolog_release"
    output = "srun.out"
    prolog = "./prolog.sh"
    script = "job.sh"

    atf.make_bash_script(
        prolog,
        f"""touch {ready}
while [ ! -f {release} ]; do
    sleep 0.5
done
""",
    )
    atf.make_bash_script(
        script,
        f"""SLURM_DEBUG_FLAGS=AuditRPCs srun -vvv --mpi=none --job-name=test_116_67 --prolog={prolog} sleep infinity
echo "SRUN_RC=$?"
""",
    )
    job_id = atf.submit_job(
        "sbatch",
        f"-N1 -n1 -t3 --job-name=test_116_67 --output={output} --error={output}",
        script,
        wrap_job=False,
        fatal=True,
    )

    atf.wait_for_file(ready, fatal=True)
    steps = [
        step_id
        for step_id in atf.get_steps(job_id, fatal=True)
        if re.fullmatch(rf"{job_id}\.\d+", step_id)
    ]
    assert len(steps) == 1, "Exactly one step should be held in srun's prolog"
    atf.run_command(f"scancel --ctld --signal=KILL {steps[0]}", fatal=True)

    # Receipt, not scancel's return, proves the RPC arrived before launch.
    atf.assert_file_contents(output, "msg_type=SRUN_JOB_COMPLETE", contains=True)
    atf.run_command(f"touch {release}", fatal=True)
    atf.assert_file_contents(
        output, "received job step complete message", contains=True
    )
    atf.assert_file_contents(output, "SRUN_RC=", contains=True)
    text = atf.run_command_output(f"cat {output}", fatal=True)
    result = re.search(r"SRUN_RC=(\d+)", text)
    assert result and int(result.group(1)) != 0, "Cancelled srun must fail"
