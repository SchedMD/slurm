############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import re
import pytest

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()

def test_slurmdDebug():
    # Test error level
    error = atf.run_command_error('srun --slurmd-debug=error true')
    assert re.search(r'debug.*?verbose', error) is not None
    
    # Test info level
    error = atf.run_command_error('srun --slurmd-debug=info true')
    assert re.search(r'debug.*?debug', error) is not None
    assert re.search(r'debug:', error) is not None

    # Test verbose level
    error = atf.run_command_error('srun --slurmd-debug=verbose true')
    assert re.search(r'debug.*?debug2', error) is not None
    assert re.search(r'debug2:', error) is not None
