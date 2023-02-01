############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_input_ouput_error(tmp_path):
    """Verify srun -input,--output, and --error options work"""

    CONTENT = "sleep aaa\nexit 0"
    sleep_err_message = "sleep: invalid time interval ‘aaa’"
    echo_output = "test --output"
    file_in = tmp_path / "file_in.input"
    file_out = tmp_path / "file_out.output"
    file_err = tmp_path / "file_err.error"
    file_in.write_text(CONTENT)

    # Test --input with a file that would fail and --error catching the error
    atf.run_command(f"srun --input={str(file_in)} --error={str(file_err)} -t1 bash")
    assert re.search(sleep_err_message,file_err.read_text()) is not None

    # Test --output's file gets the echoed message
    atf.run_command(f"srun --output={str(file_out)} -t1 echo {echo_output}")
    assert file_out.read_text().strip('\n') == echo_output

    # Test the none parameter
    stderr = atf.run_command_error(f"srun --input={str(file_in)} --error=none -t1 bash")
    assert not stderr
    stdout = atf.run_command_output(f"srun --output=none -t1 id")
    assert not stdout
