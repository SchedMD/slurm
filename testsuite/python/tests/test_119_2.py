############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import shutil


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    if shutil.which('qsub') is None:
        pytest.skip("This test requires the contribs to be built and for qsub to be in your path", allow_module_level=True)


def test_output(tmp_path):
    """Verify that qsub -o and -e results in creation of output and error files"""
    file_in = str(tmp_path / 'job.in')
    atf.make_bash_script(file_in, """echo out
echo err >&2
""")
    file_out = str(tmp_path / 'job.out')
    file_err = str(tmp_path / 'job.err')

    output = atf.run_command_output(f"qsub -l walltime=1:00 -o {file_out} -e {file_err} {file_in}", fatal=True)
    job_id = int(match.group(1)) if (match := re.search(r'(\d+)', output)) else None
    assert job_id is not None, "A job id was not returned from qsub"
    assert atf.wait_for_job_state(job_id, 'DONE'), f"Job ({job_id}) did not run"
    assert atf.wait_for_file(file_out), f"Output file ({file_out}) was not created"
    with open(file_out) as f:
        assert re.search(r'out', f.read()), "Stdout was not written to output file"
    assert atf.wait_for_file(file_err), f"Error file ({file_err}) was not created"
    with open(file_err) as f:
        assert re.search(r'err', f.read()) is not None, "Stderr was not written to error file"
