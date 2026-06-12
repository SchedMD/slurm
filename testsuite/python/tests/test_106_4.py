############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import pytest
import atf


def setup_module():
    atf.require_nodes(1, [("CPUs", 1)])
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def sbcast_job(request):
    """Submit a job that waits for a marker file, and return the job identifier
    based on request.param (JobId or SLUID) for use with sbcast -j."""

    if request.param == "SLUID":
        atf.require_version(
            (26, 5),
            "bin/sbcast",
            reason="Ticket 22180: SLUID availability added in 26.05+",
        )

    marker_file = atf.module_tmp_path / "sbcast_marker"
    src_file = atf.module_tmp_path / "sbcast_src"

    script = "sbcast_wait.sh"
    atf.make_bash_script(
        script,
        f"""for i in $(seq 1 30); do
    if [ -f {marker_file} ]; then
        echo "hola" > {marker_file}
        exit 0
    fi
    sleep 1
done
exit 1""",
    )

    # Create a source file to broadcast
    with open(src_file, "w") as f:
        f.write("sbcast_ok\n")

    job_id = atf.submit_job_sbatch(f"-N1 -n1 {script}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    identifier = str(atf.get_job_parameter(job_id, request.param))

    return identifier, job_id, src_file, marker_file


@pytest.mark.parametrize("sbcast_job", ["JobId", "SLUID"], indirect=True)
def test_sbcast(sbcast_job):
    """Verify sbcast -j <identifier> broadcasts a file to the job's nodes."""

    identifier, job_id, src_file, marker_file = sbcast_job

    atf.run_command(f"sbcast -j {identifier} -f {src_file} {marker_file}", fatal=True)

    # Job should find the file and exit successfully
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Verify the job saw the broadcast file and wrote "hola"
    output = atf.run_command_output(f"cat {marker_file}", fatal=True)
    assert "hola" in output, f"Expected 'hola' in marker file, got: {output}"
    atf.run_command(f"rm -f {marker_file}")
