############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import pytest
import atf

line_received = "Line received:"


def setup_module():
    atf.require_nodes(1)
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def interactive_step_id(request):
    """
    Submits a job with a single step waiting for 'trigger' input, and returns
    the StepId based on the request.param, so it can be JobId.0 or SLUID.0
    """

    file_out = "file.out"
    ready_str = "ready to read input"
    step_script = "interactive_step.sh"
    atf.make_bash_script(
        step_script,
        f"""
echo {ready_str}
read line
echo "{line_received} $line"
""",
    )

    job_script = "job_with_interactive_step.sh"
    atf.make_bash_script(
        job_script,
        # We need to keep the stdin of the step open or an EOF will close it
        # and sattach won't be able to use it.
        f"""srun {step_script} < <(sleep infinity)""",
    )

    job_id = atf.submit_job_sbatch(f"-t1 -o {file_out} {job_script}", fatal=True)

    # Wait until the interactive step is ready to read the line
    atf.wait_for_file(file_out)
    for t in atf.timer():
        output = atf.run_command_output(f"cat {file_out}")
        if ready_str in output:
            break
    else:
        pytest.fail(f"File {file_out} wasn't ready")

    # Return the StepID using either the JobId or SLUID based on the param
    return f"{atf.get_job_parameter(job_id, request.param)}.0"


@pytest.mark.parametrize("interactive_step_id", ["JobId", "SLUID"], indirect=True)
def test_sattach(interactive_step_id):
    """Verify sattach receives output and can send input to the step, using JobId or SLUID."""

    result = atf.run_command(f"sattach {interactive_step_id}", input="trigger")
    assert (
        f"{line_received} trigger" in result["stdout"]
    ), f"sattach should attach IO streams and get '{line_received} trigger', got: {result['stdout']}"
    assert result["exit_code"] == 0, "sattach should end normally"
