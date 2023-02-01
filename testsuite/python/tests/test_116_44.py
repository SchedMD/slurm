############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_task_prolog_and_epilog():
    """Test of srun task-prolog and task-epilog option."""

    task_prolog = atf.module_tmp_path / 'task_prolog'
    file_in = atf.module_tmp_path / 'file_in'
    task_epilog = atf.module_tmp_path / 'task_epilog'
    file_out_pre = atf.module_tmp_path / 'file_out_pre'
    file_out_post = atf.module_tmp_path / 'file_out_post'
    os.chmod(atf.module_tmp_path, 0o777)

    num_tasks = 4
    node_count = 1
    my_uid = os.getuid()

    atf.make_bash_script(task_prolog, f"""id >> {file_out_pre}
echo print HEADER
echo export TEST=prolog_qa
echo unset DISPLAY""")

    atf.make_bash_script(task_epilog, f"""id >> {file_out_post}
sleep 200""")

    atf.make_bash_script(file_in, r"""echo TEST==$TEST
echo DISPLAY==${DISPLAY}X""")

    output = atf.run_job_output(f"-N{node_count} -n{num_tasks} -O --task-prolog={task_prolog} --task-epilog={task_epilog} {file_in}")
    match_header = re.findall('HEADER', output)
    match_test = re.findall('TEST==prolog_qa', output)
    match_display = re.findall('DISPLAY==X', output)
    assert len(match_header) == num_tasks, "Prolog exported env var failure HEADER count != number of tasks"
    assert len(match_test) == num_tasks, "Prolog exported env var failure TEST count != number of tasks"
    assert len(match_display) == num_tasks, "Prolog exported env var failure DISPLAY count != number of tasks"

    atf.wait_for_file(file_out_pre)
    output = atf.run_command_output(f"cat {file_out_pre}")
    match_uid = re.findall(rf'uid={my_uid}', output)
    assert len(match_uid) == num_tasks, "Task prolog output is missing or uid mismatch"

    atf.wait_for_file(file_out_post)
    output = atf.run_command_output(f"cat {file_out_post}")
    match_uid = re.findall(rf'uid={my_uid}', output)
    assert len(match_uid) == num_tasks, "Task epilog output is missing or uid mismatch"
