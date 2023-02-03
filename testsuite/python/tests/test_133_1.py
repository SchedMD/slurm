############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pathlib
import os
import pytest
import re


@pytest.fixture(scope='function')
def compiled_program(tmp_path):
    """Compile test program that uses plugin"""
    test_program = str(tmp_path / "test.exe")
    source_file = re.sub(r'\.py$', '.c', __file__)
    atf.compile_against_libslurm(source_file, test_program, full=True)
    return test_program


@pytest.mark.parametrize("test_case", list(range(1,6)))
def test_route_topology(compiled_program, test_case):
    """Test the route/topology plugin"""
    cwd = str(pathlib.Path(__file__).resolve().parent)
    test_dir = os.path.splitext(__file__)[0] + f'_testcases/testcase{test_case}'
    error = atf.run_command_error(f"{compiled_program} --configdir={test_dir} --testcases={test_dir}/testcases", fatal=True)
    assert re.search(r'Failed cases 0', error) is not None
