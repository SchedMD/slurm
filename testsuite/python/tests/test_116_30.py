############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_exit_code_reporting():
    """Test of srun exit code reporting"""

    exit_code = 66
    task_count = '2'
    exit_string = 'srun_exit_code_'
    exit_script = atf.module_tmp_path / 'exit_script'
    atf.make_bash_script(exit_script, f"""sleep 2
    exit {exit_code}""")
    test_script = atf.module_tmp_path / 'test_script'
    atf.make_bash_script(test_script, rf"""srun -n{task_count} -O {exit_script}
    echo srun_exit_code_$?""")

    # Spawn program and check for exit code messages from srun
    result_exit_code = atf.run_command_exit(f"srun -n{task_count} -O {exit_script}", xfail=True)
    assert exit_code == result_exit_code, f"Srun failed to report exit code: {exit_code}, reported: {result_exit_code}"

    # Spawn program to check the exit code of srun itself
    result_exit_string = atf.run_command_output(f"{test_script}").strip()
    assert f'{exit_string}{exit_code}' == result_exit_string, f"Sruns exit code is bad {exit_string}{exit_code} != {result_exit_string}"
