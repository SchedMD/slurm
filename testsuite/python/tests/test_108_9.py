############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import re
import atf


def setup_module():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


def test_listpids():
    """Validate scontrol listpids with numeric job_id, step_id, and non-existent step."""

    file_out = atf.module_tmp_path / "listpids_output"
    script = atf.module_tmp_path / "listpids.sh"
    atf.make_bash_script(
        script,
        """srun sleep 5 &
        sleep 1
        scontrol listpids $SLURM_JOB_ID.10 $SLURMD_NODENAME
        scontrol listpids $SLURM_JOB_ID.0 $SLURMD_NODENAME
        scontrol listpids $SLURM_JOB_ID $SLURMD_NODENAME
        wait""",
    )

    job_id = atf.submit_job_sbatch(f"--output={file_out} {script}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(file_out, fatal=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    # Non-existent step .10 should produce an error
    assert re.search(
        rf"StepId={job_id}\.10 does not exist on", output
    ), f"Expected error for non-existent step .10: {output}"

    # listpids for step .0 and for all steps should both show job_id with step 0
    matches = re.findall(rf"{job_id}\s+0\s+0\s+0", output)
    assert (
        len(matches) == 2
    ), f"Expected 2 listpids entries for {job_id} step 0, got {len(matches)}: {output}"


def test_listpids_sluid():
    """Validate scontrol listpids with SLUID and SLUID.stepid."""

    file_out = atf.module_tmp_path / "listpids_sluid_output"
    script = atf.module_tmp_path / "listpids_sluid.sh"
    atf.make_bash_script(
        script,
        """SLUID=$(scontrol -d show job $SLURM_JOB_ID | grep -oP 'SLUID=\\K\\S+')
        srun sleep 3 &
        sleep 1
        scontrol listpids $SLUID.10 $SLURMD_NODENAME
        scontrol listpids $SLUID.0 $SLURMD_NODENAME
        scontrol listpids $SLUID $SLURMD_NODENAME
        wait""",
    )

    job_id = atf.submit_job_sbatch(f"--output={file_out} {script}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    sluid = atf.get_job_parameter(job_id, "SLUID")
    assert sluid is not None, f"Job {job_id} has no SLUID"

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(file_out, fatal=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    # Non-existent step .10 should produce an error with SLUID
    assert re.search(
        rf"StepId={re.escape(sluid)}\.10 does not exist on", output
    ), f"Expected error with SLUID for non-existent step .10: {output}"

    # listpids for SLUID.0 and SLUID (all steps) should both show job_id
    # with step 0
    matches = re.findall(rf"{job_id}\s+0\s+0\s+0", output)
    assert (
        len(matches) == 2
    ), f"Expected 2 listpids entries for {job_id} step 0, got {len(matches)}: {output}"
