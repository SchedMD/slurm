############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf


def setup_module():
    atf.require_version(
        (26, 5),
        "bin/strigger",
        reason="Ticket 22180: SLUID availability added in 26.05+",
    )
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def test_strigger_sluid():
    """Verify strigger --set/--get/--clear work with a SLUID as --jobid."""

    # Create a dummy trigger program
    trigger_script = atf.module_tmp_path / "fini_trigger.sh"
    atf.make_bash_script(trigger_script, "true")

    job_id = atf.submit_job_sbatch("-n1 --wrap 'srun sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"

    # Set a trigger by SLUID
    atf.run_command(
        f"strigger --set --jobid={sluid} --fini --program={trigger_script}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Get triggers by SLUID and verify
    output = atf.run_command_output(
        f"strigger --get --jobid={sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert re.search(
        r"\bjob\b", output
    ), f"Expected RES_TYPE 'job' in strigger output: {output}"
    assert re.search(
        rf"\b{job_id}\b", output
    ), f"Expected RES_ID {job_id} in strigger output: {output}"
    assert re.search(
        r"\bfini\b", output
    ), f"Expected TYPE 'fini' in strigger output: {output}"
    assert re.search(
        rf"{re.escape(str(trigger_script))}", output
    ), f"Expected program path in strigger output: {output}"

    # Clear the trigger by SLUID
    atf.run_command(
        f"strigger --clear --jobid={sluid}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Verify trigger is gone
    output = atf.run_command_output(f"strigger --get --jobid={sluid}", fatal=True)
    assert not re.search(
        rf"\b{job_id}\b", output
    ), f"Expected no trigger for job {job_id} after clear: {output}"
