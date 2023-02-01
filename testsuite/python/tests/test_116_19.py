############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import resource
import pathlib


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('FrontendName', None)
    atf.require_slurm_running()


def test_user_limits():
    """Verify that user user limits are propagated to the job."""

    file_in = atf.module_tmp_path / 'file_in'
    file_out = atf.module_tmp_path / 'file_out'
    file_err = atf.module_tmp_path / 'file_err'
    script_file = pathlib.Path(atf.properties['testsuite_python_lib']) / 'print_user_limits.py'
    atf.run_command(f"srun python3 {script_file}")

    limit_core = 943
    limit_fsize = 674515
    limit_nofile = 1016
    limit_nproc = 34500
    limit_stack = 5021
    cur_core = resource.getrlimit(resource.RLIMIT_CORE)[1]
    cur_fsize = resource.getrlimit(resource.RLIMIT_FSIZE)[1]
    cur_nofile = resource.getrlimit(resource.RLIMIT_NOFILE)[1]
    if resource.getrlimit(resource.RLIMIT_NPROC) != resource.error:
        cur_nproc = resource.getrlimit(resource.RLIMIT_NPROC)[1]
    else:
        cur_nproc = -1
    if resource.getrlimit(resource.RLIMIT_STACK) != resource.error:
        cur_stack = resource.getrlimit(resource.RLIMIT_STACK)[1]
    else:
        cur_stack = -1

    if cur_core != -1:
        limit_core = (cur_core / 1024) - 2
        if limit_core < 1:
            limit_core = cur_core / 1024

    if cur_fsize != -1:
        limit_fsize = (cur_fsize / 1024) - 2
        if limit_fsize < 1:
            limit_fsize = cur_fsize / 1024

    if cur_nofile != -1:
        limit_nofile = cur_nofile - 2
        if limit_nofile < 1:
            limit_nofile = cur_nofile

    if cur_nproc != -1:
        limit_nproc = cur_nproc - 200
        if limit_nproc < 1:
            limit_nproc = cur_nproc

    if cur_stack != -1:
        limit_stack = (cur_stack / 1024) - 2
        if limit_stack < 1:
            limit_stack = cur_stack / 1024

    atf.make_bash_script(file_in, f"""  ulimit -c {limit_core}
    ulimit -f {limit_fsize}
    ulimit -n {limit_nofile}
    ulimit -u {limit_nproc}
    ulimit -s {limit_stack}
    srun python3 {script_file}""")

    job_id = atf.submit_job(f"--output={file_out} --error={file_err} {file_in}")
    atf.wait_for_job_state(job_id, 'DONE')
    f = open(file_out, 'r')
    line = f.readline()
    assert limit_core * 1024 == int(line), f"RLIMIT_CORE failed {limit_core * 1024} != {line}"
    line = f.readline()
    assert limit_fsize * 1024 == int(line), f"RLIMIT_FSIZE failed {limit_fsize * 1024} != {line}"
    line = f.readline()
    assert limit_nofile == int(line), f"RLIMIT_NOFILE failed {limit_nofile} != {line}"
    line = f.readline()
    if line != "USER_NPROC unsupported":
        assert limit_nproc == int(line), f"RLIMIT_NPROC failed {limit_nproc} != {line}"
    line = f.readline()
    if line != "USER_STACK unsupported":
        assert limit_stack * 1024 == int(line), f"RLIMIT_STACK failed {limit_stack * 1024} != {line}"
