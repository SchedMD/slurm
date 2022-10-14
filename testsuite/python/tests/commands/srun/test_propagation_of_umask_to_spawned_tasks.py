############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('FrontendName', None)
    atf.require_slurm_running()


def test_propagation_of_umask_to_spawned_tasks():
    """Test propagation of umask to spawned tasks"""

    file_in = atf.module_tmp_path / 'file_in'
    file_script = atf.module_tmp_path / 'file_script'

    atf.make_bash_script(file_in, """umask""")
    atf.make_bash_script(file_script, f"""umask 023
srun -N1 -t1 {file_in}""")

    output = atf.run_command_output(f'{file_script}')
    assert re.search(r'0023|023', output), "umask not propagated"
