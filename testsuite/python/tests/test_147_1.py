import atf
import pytest
import os

spank_plugin_script = "test_147_1_spank_plugin.c"
spank_compiled_plugin = "test_147_1_spank_plugin.so"


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("JobContainerType", "job_container/tmpfs")
    atf.require_config_parameter_includes("SlurmdParameters", "contain_spank")
    atf.require_config_parameter_includes("PrologFlags", "Contain")

    # Compile SPANK plugin
    spank_plugin_script_path = (
        f"{atf.properties['testsuite_scripts_dir']}/{spank_plugin_script}"
    )
    atf.compile_against_libslurm(
        spank_plugin_script_path, spank_compiled_plugin, full=True, shared=True
    )

    # Ensure the SPANK plugin is included in plugstack.conf
    atf.require_config_parameter(
        "required",
        f"{atf.module_tmp_path}/{spank_compiled_plugin}",
        delimiter=" ",
        source="plugstack",
    )

    # Setup job_container.conf
    atf.require_config_parameter("AutoBasePath", "true", source="job_container")
    atf.require_config_parameter(
        "BasePath", f"/tmp/%h_%n_base_path", source="job_container"
    )

    # Mount /tmp/test_147_1_private in the job container as a private mount
    atf.require_config_parameter(
        "Dirs", "/tmp/test_147_1_private", source="job_container"
    )
    atf.run_command("mkdir -p /tmp/test_147_1_private", fatal=True)

    atf.require_slurm_running()


def test_spank_plugin_tmpfs():
    """
    Test that SPANK plugin hooks execute correctly in the tmpfs job container.
    """
    # Clear out the private mount and create a file outside the container
    atf.run_command("rm -rf /tmp/test_147_1_private/*", fatal=True)
    atf.run_command("touch /tmp/test_147_1_private/file_on_host", fatal=True)

    atf.make_bash_script(
        "job.sh",
        f"""
    # Check to make sure job_container/tmpfs made a private mount
    if [[ -f /tmp/test_147_1_private/file_on_host ]]; then
        echo "job_container/tmpfs failed to create private mount"
    else
        echo "job_container/tmpfs created private mount"
    fi

    # Run step to trigger some SPANK functions we're testing
    srun hostname

    # Check if slurm_spank_user_init executed its functions and left behind a file
    if [[ -f /tmp/test_147_1_private/slurm_spank_user_init_log ]]; then
        echo "Found log for hook slurm_spank_user_init"
    else
        echo "Couldn't find log for hook slurm_spank_user_init"
    fi

    # Check if slurm_spank_task_post_fork executed its functions and left behind a file
    if [[ -f /tmp/test_147_1_private/slurm_spank_task_post_fork_log ]]; then
        echo "Found log for hook slurm_spank_task_post_fork"
    else
        echo "Couldn't find log for hook slurm_spank_task_post_fork"
    fi

    # Check if slurm_spank_task_exit executed its functions and left behind a file
    if [[ -f /tmp/test_147_1_private/slurm_spank_task_exit_log ]]; then
        echo "Found log for hook slurm_spank_task_exit"
    else
        echo "Couldn't find log for hook slurm_spank_task_exit"
    fi
    """,
    )

    # Wait for job to run
    job_id = atf.submit_job_sbatch("--output=output job.sh", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Job {job_id} did not complete successfully"

    # Make sure output file created
    atf.wait_for_file("output", timeout=5, fatal=True)

    # Verify the job found files created by each SPANK plugin hook
    with open("output") as output_file:
        content = output_file.read()

        assert (
            f"Found log for hook slurm_spank_user_init" in content
        ), f"Job couldn't find file for slurm_spank_user_init hook"
        assert (
            f"Found log for hook slurm_spank_task_post_fork" in content
        ), f"Job couldn't find file for slurm_spank_task_post_fork hook"
        assert (
            f"Found log for hook slurm_spank_task_exit" in content
        ), f"Job couldn't find file for slurm_spank_task_exit hook"

        assert (
            "job_container/tmpfs created private mount" in content
        ), "job_container/tmpfs failed to create a private mount because we found a pre-existing file on the host when running a job"
        assert not (
            os.path.isfile("/tmp/test_147_1_private/slurm_spank_user_init_log")
            or os.path.isfile("/tmp/test_147_1_private/slurm_spank_task_post_fork_log")
            or os.path.isfile("/tmp/test_147_1_private/slurm_spank_task_exit_log")
        ), "job_container/tmpfs failed to isolate private mount; files created in container appear on host"
