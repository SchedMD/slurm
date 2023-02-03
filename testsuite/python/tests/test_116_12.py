############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import os
from pathlib import Path

ERROR_TYPE = "error"
OUTPUT_TYPE = "output"
node_count = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


class FPC:
    def __init__(self,tmp_path):
        self.tmp_path = tmp_path

    # creates either an error or an output file with file name formatting
    # as a full path from the tmp_path
    def create_file_path(self, fnp, file_type = OUTPUT_TYPE):
        if file_type == ERROR_TYPE:
            return self.tmp_path / f"file_err.%{fnp}.error"
        return self.tmp_path / f"file_out.%{fnp}.output"

    # creates either an error or an output file with file name formatting
    def create_file(self, val, file_type = OUTPUT_TYPE):
        if file_type == ERROR_TYPE:
            return f"file_err.{val}.error"
        return f"file_out.{val}.output"

    # only works for file names, not full paths
    def remove_file(self, file_path):
        os.remove(str(self.tmp_path) + "/" + file_path)

    # returns the first file in the tmp_path
    # usful when you only have 1 file in tmp_path
    def get_tmp_file(self):
        return os.listdir(self.tmp_path)[0]


def test_output_error_formatting(tmp_path):
    """Verify srun stdout/err file name formatting (--output and --error options)."""

    fpc = FPC(tmp_path)

    # Test %t puts the task identifier in the file names
    task_count = 5
    file_out = fpc.create_file_path("t")
    atf.run_job(f"--output={file_out} -N1 -n{task_count} -O id")
    tmp_dir_list = os.listdir(tmp_path)
    for task in range(task_count):
        task_file = fpc.create_file(task)
        assert task_file in tmp_dir_list, f"%t: task file ({task_file}) was not created"
        fpc.remove_file(task_file)

    file_err = fpc.create_file_path("t", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -n{task_count} -O uid")
    tmp_dir_list = os.listdir(tmp_path)
    for task in range(task_count):
        task_file = fpc.create_file(task, ERROR_TYPE)
        assert task_file in tmp_dir_list, f"%t: task file ({task_file}) was not created"
        fpc.remove_file(task_file)

    # Test %j puts the job id in the file names
    file_out = fpc.create_file_path("j")
    job_id = atf.run_job_id(f"--output={file_out} -N1 -O id")
    file_out = fpc.get_tmp_file()
    assert re.search(str(job_id), file_out) is not None, f"%j: Job id ({job_id}) was not in file name ({file_out})"
    fpc.remove_file(file_out)

    file_err = fpc.create_file_path("j", ERROR_TYPE)
    job_id = atf.run_job_id(f"--error={file_err} -N1 -O uid")
    file_err = fpc.get_tmp_file()
    assert re.search(str(job_id), file_err) is not None, f"%j: Job id ({job_id}) was not in file name ({file_err})"
    fpc.remove_file(file_err)

    # Test %% creates a file with one % in the file name
    output_file = fpc.create_file("%")
    file_out = fpc.create_file_path("%")
    atf.run_job(f"--output={file_out} -N1 -O id")
    tmp_dir_list = os.listdir(tmp_path)
    assert output_file in tmp_dir_list, f"%%: Output file ({output_file}) not created"
    fpc.remove_file(output_file)

    error_file = fpc.create_file("%", ERROR_TYPE)
    file_err = fpc.create_file_path("%", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O uid")
    tmp_dir_list = os.listdir(tmp_path)
    assert error_file in tmp_dir_list, f"%%: Output file ({error_file}) not created"
    fpc.remove_file(error_file)

    # Test %J puts the job.step id in the file name
    file_out = fpc.create_file_path("J")
    error_out = atf.run_job_error(f"--output={file_out} -v -N1 -O id")
    tmp_dir_list = os.listdir(tmp_path)
    match = re.search(r"(?<=StepId=)\d+\.\d+", error_out)
    step_id = match.group(0)
    file_out = fpc.get_tmp_file()
    assert re.search(step_id, file_out) is not None, f"%J: Step id ({step_id}) was not in file name ({file_out})"
    fpc.remove_file(file_out)

    file_err = fpc.create_file_path("J", ERROR_TYPE)
    error_out = atf.run_job_error(f"--error={file_err} -v -N1 -O uid")
    tmp_dir_list = os.listdir(tmp_path)
    match = re.search(r"(?<=StepId=)\d+\.\d+", error_out)
    step_id = match.group(0)
    file_err = fpc.get_tmp_file()
    assert re.search(step_id, file_err) is not None, f"%J: Step id ({step_id}) was not in file name ({file_err})"
    fpc.remove_file(file_err)

    # Test %u puts the user name in the file name
    user_name = atf.get_user_name()
    file_out = fpc.create_file_path("u")
    atf.run_job(f"--output={file_out} -N1 -O id")
    file_out = fpc.get_tmp_file()
    assert re.search(user_name, file_out), f"%u: User name ({user_name}) was not in file name ({file_out})"
    fpc.remove_file(file_out)

    file_err = fpc.create_file_path("u", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O uid")
    file_err = fpc.get_tmp_file()
    assert re.search(user_name, file_err), f"%u: User name ({user_name}) was not in file name ({file_err})"
    fpc.remove_file(file_err)

    # Test %n puts the node identifier relative to current job in the file name
    node_id = 0
    file_out = fpc.create_file_path("n")
    result_out = fpc.create_file(node_id)
    atf.run_job(f"--output={file_out} -N1 -O id")
    file_out = fpc.get_tmp_file()
    assert result_out in file_out, f"%n: Node id ({node_id}) was not in file name ({file_out})"
    fpc.remove_file(file_out)

    file_err = fpc.create_file_path("n", ERROR_TYPE)
    result_err = fpc.create_file(node_id, ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O id")
    file_err = fpc.get_tmp_file()
    assert result_err in file_err, f"%n: Node id ({node_id}) was not in file name ({file_err})"
    fpc.remove_file(file_err)

    # Test %s puts the step identifier in the file name
    step_count = 4
    node_count = "1"
    file_out = fpc.create_file_path("s")
    file_in = tmp_path / "file_in.s.input"
    atf.make_bash_script(file_in, f"""for i in {{1..{step_count}}}
do
    srun -O --output={file_out} true
done""")
    os.chmod(file_in, 0o0777)
    job_id = atf.submit_job(f"-N{node_count} --output /dev/null {str(file_in)}")
    atf.wait_for_job_state(job_id, 'DONE')
    tmp_dir_list = os.listdir(tmp_path)
    for step in range(0,step_count):
        step_file = fpc.create_file(step)
        assert step_file in tmp_dir_list, f"%s: Step file ({step_file}) was not created"
        fpc.remove_file(step_file)

    file_err = fpc.create_file_path("s", ERROR_TYPE)
    atf.make_bash_script(file_in, f"""for i in {{1..{step_count}}}
do
    srun -O --error={file_err} true
done""")
    os.chmod(file_in, 0o0777)
    job_id = atf.submit_job(f"-N{node_count} --output /dev/null {str(file_in)}")
    atf.wait_for_job_state(job_id, 'DONE')
    tmp_dir_list = os.listdir(tmp_path)
    for step in range(0,step_count):
        step_file = fpc.create_file(step, ERROR_TYPE)
        assert step_file in tmp_dir_list, f"%s: Step file ({step_file}) was not created"
        fpc.remove_file(step_file)
    os.remove(file_in)

    # Test %x puts the Job name in the file name
    job_command = "uid"
    file_out = fpc.create_file_path("x")
    file_err = fpc.create_file_path("x", ERROR_TYPE)
    job_id = atf.run_job_id(f"--output={file_out} {job_command}")
    job_name = atf.get_job_parameter(job_id, "JobName")
    assert job_command == job_name, f"%x: Job command ({job_command}) is not the same as the JobName ({job_name})"
    result_out = fpc.create_file(job_command)
    assert result_out in os.listdir(tmp_path), f"%x: Output file ({result_out}) was not created"
    fpc.remove_file(result_out)

    job_id = atf.run_job_id(f"--error={file_err} {job_command}")
    job_name = atf.get_job_parameter(job_id, "JobName")
    assert job_command == job_name, f"%x: Job command ({job_command}) is not the same as the JobName ({job_name})"
    result_err = fpc.create_file(job_command, ERROR_TYPE)
    assert result_err in os.listdir(tmp_path), f"%x: Error file ({result_err}) was not created"
    fpc.remove_file(result_err)

    # Test %N puts the short hostname in the file name
    file_out = fpc.create_file_path("N")
    file_err = fpc.create_file_path("N", ERROR_TYPE)
    atf.run_job(f"--output={file_out} printenv SLURMD_NODENAME")
    result_out = fpc.get_tmp_file()
    node_name = (tmp_path / result_out).read_text().rstrip()
    node_host_name = atf.get_node_parameter(node_name, "NodeHostName")
    node_addr = atf.get_node_parameter(node_name, "NodeAddr")
    if node_addr != node_host_name and not atf.is_integer(node_addr[0]):
        node_host_name = node_addr
    assert re.search(node_host_name, result_out) is not None, f"%N: Output file ({result_out}) does not contain NodeHostName ({node_host_name})"
    fpc.remove_file(result_out)

    job_id = atf.run_job_id(f"--error={file_err} true")
    node_name = atf.get_job_parameter(job_id, "NodeList")
    node_host_name = atf.get_node_parameter(node_name, "NodeHostName")
    node_addr = atf.get_node_parameter(node_name, "NodeAddr")
    if node_addr != node_host_name and not atf.is_integer(node_addr[0]):
        node_host_name = node_addr
    result_err = fpc.get_tmp_file()
    assert re.search(node_host_name, result_err) is not None, f"%N: Error file ({result_err}) does not contain NodeHostName ({node_host_name})"
    fpc.remove_file(result_err)

    # Test %A puts the Job array's master job allocation number in the file name
    array_size = 2
    file_out = fpc.create_file_path("A")
    file_err = fpc.create_file_path("A", ERROR_TYPE)
    file_in = tmp_path / "file_in.A.input"
    atf.make_bash_script(file_in, f"""srun -O --output={file_out} hostname""")
    os.chmod(file_in, 0o0777)
    job_id = atf.submit_job(f"-N1 --output=/dev/null --array=1-{array_size} {file_in}")
    atf.wait_for_job_state(job_id, "DONE")
    os.remove(file_in)
    result_out = fpc.get_tmp_file()
    assert str(job_id) in result_out, f"%A: Job array's master job allocation number ({job_id}) was not in file name ({result_out})"
    fpc.remove_file(result_out)

    atf.make_bash_script(file_in, f"""srun -O --error={file_err} uid""")
    os.chmod(file_in, 0o0777)
    job_id = atf.submit_job(f"-N1 --output=/dev/null --array=1-{array_size} {file_in}")
    atf.wait_for_job_state(job_id, "DONE")
    os.remove(file_in)
    result_err = fpc.get_tmp_file()
    assert str(job_id) in result_err, f"%A: Job array's master job allocation number ({job_id}) was not in file name ({result_err})"
    fpc.remove_file(result_err)

    # Test %a puts the Job array ID in the file name
    array_size = 2
    file_out = fpc.create_file_path(f"A.%a")
    file_err = fpc.create_file_path(f"A.%a", ERROR_TYPE)
    file_in = tmp_path / "file_in.A.a.input"
    atf.make_bash_script(file_in, f"""srun -O --output={file_out} hostname""")
    os.chmod(file_in, 0o0777)
    job_id = atf.submit_job(f"-N1 --output=/dev/null --array=1-{array_size} {file_in}")
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = os.listdir(tmp_path)
    for array_id in range(1,array_size+1):
        id_file = fpc.create_file(str(job_id) + "." + str(array_id))
        assert id_file in tmp_dir_list, f"%a: Job array file ({id_file}) was not created"
        fpc.remove_file(id_file)

    atf.make_bash_script(file_in, f"""srun -O --error={file_err} uid""")
    job_id = atf.submit_job(f"-N1 --output=/dev/null --array=1-{array_size} {file_in}")
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = os.listdir(tmp_path)
    for array_id in range(1,array_size+1):
        id_file = fpc.create_file(str(job_id) + "." + str(array_id), ERROR_TYPE)
        assert id_file in tmp_dir_list, f"%a: Job array file ({id_file}) was not created"
        fpc.remove_file(id_file)
    os.remove(file_in)
