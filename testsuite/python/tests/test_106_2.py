############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Globals
ns_dir = "/tmp/test_106_2_private"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("JobContainerType", "job_container/tmpfs")
    atf.require_config_parameter_includes("PrologFlags", "Contain")

    basepath = "/tmp/%h_%n_base_path"
    atf.require_config_parameter("BasePath", basepath, source="job_container")
    atf.require_config_parameter("AutoBasePath", "true", source="job_container")
    atf.require_config_parameter("Dirs", ns_dir, source="job_container")

    # Note: assume the same file system is visible for us and the slurmd
    atf.run_command(f"mkdir -p {ns_dir}", fatal=True)

    atf.require_slurm_running()


@pytest.mark.xfail(
    atf.get_version("bin/sbcast") == (24, 11, 0),
    reason="Ticket 21634: In 24.11.1 a regression with sbcast+job_container/tmpfs was fixed",
)
def test_sbcast_contained():
    """
    Verify sbcast only transfers the file into the container
    """

    # Clear out the private mount and create a file outside the container
    atf.run_command(f"rm -rf {ns_dir}/*", fatal=True)
    atf.run_command(f"touch {ns_dir}/public_file", fatal=True)

    atf.make_bash_script(
        "job.sh",
        f"""
        # Check to make sure job_container/tmpfs made a private mount
        if [[ -f {ns_dir}/public_file ]]; then
            echo "job_container/tmpfs failed to create private mount"
        else
            echo "job_container/tmpfs created private mount"
        fi

        # Wait for file from sbcast
        while [ ! -f {ns_dir}/test ]; do
            sleep 0.5
        done

        # Report that the file is visible
        echo "Found test file in private mount"
        cat {ns_dir}/test
        """,
    )

    # Wait for job to run
    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    # Send file
    expected_content = "Successful sbcast"
    atf.run_command(f"echo '{expected_content}' > test", fatal=True)
    for t in atf.timer():
        output = atf.run_command_output("cat test")
        if expected_content in output:
            break
    else:
        pytest.fail("Unable to create the test file.")
    atf.run_command(f"sbcast -j {job_id} test {ns_dir}/test", fatal=True)

    # Wait for job to finish
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    # Make sure output file exists
    atf.wait_for_file("output", fatal=True)

    # Verify the job found files created by each SPANK plugin hook
    for t in atf.timer():
        content = atf.run_command_output("cat output", fatal=True)

        found = []
        found.append("Found test file in private mount" in content)
        found.append(expected_content in content)
        found.append("job_container/tmpfs created private mount" in content)

        if all(found):
            break

    assert all(
        found
    ), f"Output file should contain all the expected output, but {found}"

    assert not (
        atf.run_command_output(f"ls {ns_dir}/test")
    ), "Files created in container should not appear in the host"
