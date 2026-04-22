############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import logging

ERROR_TYPE = "error"
OUTPUT_TYPE = "output"
node_count = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


# creates either an error or an output file with file name formatting
def create_file_path(fnp, file_type=OUTPUT_TYPE):
    if file_type == ERROR_TYPE:
        return f"file_err.%{fnp}.error"
    return f"file_out.%{fnp}.output"


# creates either an error or an output file with file name formatting
def create_file(val, file_type=OUTPUT_TYPE):
    if file_type == ERROR_TYPE:
        return f"file_err.{val}.error"
    return f"file_out.{val}.output"


def remove_file(file_path):
    atf.run_command(f"rm {file_path}")


# returns the directory listing, waiting until all expected files appear.
# If expected is None, waits for at least one file.
def wait_and_assert_files(expected=None):
    for t in atf.timer(quiet=True):
        files = atf.run_command_output("ls").splitlines()
        if expected is None:
            if files:
                return files
        elif all(f in files for f in expected):
            return files

        logging.debug(
            f"Found files: {files}. Expected files: {expected}. Remaining time: {t}"
        )
    assert False, f"Expected files not found: expected={expected}, got={files}"


def test_output_error_formatting():
    """Verify srun stdout/err file name formatting (--output and --error options)."""

    # Test %t puts the task identifier in the file names
    task_count = 5
    file_out = create_file_path("t")
    atf.run_job(f"--output={file_out} -N1 -n{task_count} -O id")
    tmp_dir_list = wait_and_assert_files(
        [create_file(task) for task in range(task_count)]
    )
    for task in range(task_count):
        task_file = create_file(task)
        assert task_file in tmp_dir_list, f"%t: task file ({task_file}) was not created"
        remove_file(task_file)

    file_err = create_file_path("t", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -n{task_count} -O uid")
    tmp_dir_list = wait_and_assert_files(
        [create_file(task, ERROR_TYPE) for task in range(task_count)]
    )
    for task in range(task_count):
        task_file = create_file(task, ERROR_TYPE)
        assert task_file in tmp_dir_list, f"%t: task file ({task_file}) was not created"
        remove_file(task_file)

    # Test %j puts the job id in the file names
    file_out = create_file_path("j")
    job_id = atf.submit_job_srun(f"--output={file_out} -N1 -O id")
    file_out = wait_and_assert_files()[0]
    assert (
        re.search(str(job_id), file_out) is not None
    ), f"%j: Job id ({job_id}) was not in file name ({file_out})"
    remove_file(file_out)

    file_err = create_file_path("j", ERROR_TYPE)
    job_id = atf.submit_job_srun(f"--error={file_err} -N1 -O uid")
    file_err = wait_and_assert_files()[0]
    assert (
        re.search(str(job_id), file_err) is not None
    ), f"%j: Job id ({job_id}) was not in file name ({file_err})"
    remove_file(file_err)

    # Test %% creates a file with one % in the file name
    output_file = create_file("%")
    file_out = create_file_path("%")
    atf.run_job(f"--output={file_out} -N1 -O id")
    wait_and_assert_files([output_file])
    remove_file(output_file)

    error_file = create_file("%", ERROR_TYPE)
    file_err = create_file_path("%", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O uid")
    wait_and_assert_files([error_file])
    remove_file(error_file)

    # Test %J puts the job.step id in the file name
    file_out = create_file_path("J")
    error_out = atf.run_job_error(f"--output={file_out} -v -N1 -O id")
    match = re.search(r"(?<=StepId=)\d+\.\d+", error_out)
    step_id = match.group(0)
    file_out = wait_and_assert_files()[0]
    assert (
        re.search(step_id, file_out) is not None
    ), f"%J: Step id ({step_id}) was not in file name ({file_out})"
    remove_file(file_out)

    file_err = create_file_path("J", ERROR_TYPE)
    error_out = atf.run_job_error(f"--error={file_err} -v -N1 -O uid")
    match = re.search(r"(?<=StepId=)\d+\.\d+", error_out)
    step_id = match.group(0)
    file_err = wait_and_assert_files()[0]
    assert (
        re.search(step_id, file_err) is not None
    ), f"%J: Step id ({step_id}) was not in file name ({file_err})"
    remove_file(file_err)

    # Test %u puts the user name in the file name
    user_name = atf.properties["test-user"]
    file_out = create_file_path("u")
    atf.run_job(f"--output={file_out} -N1 -O id")
    file_out = wait_and_assert_files()[0]
    assert re.search(
        user_name, file_out
    ), f"%u: User name ({user_name}) was not in file name ({file_out})"
    remove_file(file_out)

    file_err = create_file_path("u", ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O uid")
    file_err = wait_and_assert_files()[0]
    assert re.search(
        user_name, file_err
    ), f"%u: User name ({user_name}) was not in file name ({file_err})"
    remove_file(file_err)

    # Test %n puts the node identifier relative to current job in the file name
    node_id = 0
    file_out = create_file_path("n")
    result_out = create_file(node_id)
    atf.run_job(f"--output={file_out} -N1 -O id")
    file_out = wait_and_assert_files()[0]
    assert (
        result_out in file_out
    ), f"%n: Node id ({node_id}) was not in file name ({file_out})"
    remove_file(file_out)

    file_err = create_file_path("n", ERROR_TYPE)
    result_err = create_file(node_id, ERROR_TYPE)
    atf.run_job(f"--error={file_err} -N1 -O id")
    file_err = wait_and_assert_files()[0]
    assert (
        result_err in file_err
    ), f"%n: Node id ({node_id}) was not in file name ({file_err})"
    remove_file(file_err)

    # Test %s puts the step identifier in the file name
    step_count = 4
    node_count = "1"
    file_out = create_file_path("s")
    file_in = "file_in.s.input"
    atf.make_bash_script(
        file_in,
        f"""for i in {{1..{step_count}}}
do
    srun -O --output={file_out} true
done""",
    )
    job_id = atf.submit_job_sbatch(f"-N{node_count} --output /dev/null {str(file_in)}")
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = wait_and_assert_files(
        [create_file(step) for step in range(step_count)]
    )
    for step in range(0, step_count):
        step_file = create_file(step)
        assert step_file in tmp_dir_list, f"%s: Step file ({step_file}) was not created"
        remove_file(step_file)

    file_err = create_file_path("s", ERROR_TYPE)
    atf.make_bash_script(
        file_in,
        f"""for i in {{1..{step_count}}}
do
    srun -O --error={file_err} true
done""",
    )
    job_id = atf.submit_job_sbatch(f"-N{node_count} --output /dev/null {str(file_in)}")
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = wait_and_assert_files(
        [create_file(step, ERROR_TYPE) for step in range(step_count)]
    )
    for step in range(0, step_count):
        step_file = create_file(step, ERROR_TYPE)
        assert step_file in tmp_dir_list, f"%s: Step file ({step_file}) was not created"
        remove_file(step_file)

    # Test %x puts the Job name in the file name
    job_command = "uid"
    file_out = create_file_path("x")
    file_err = create_file_path("x", ERROR_TYPE)
    job_id = atf.submit_job_srun(f"--output={file_out} {job_command}")
    job_name = atf.get_job_parameter(job_id, "JobName")
    assert (
        job_command == job_name
    ), f"%x: Job command ({job_command}) is not the same as the JobName ({job_name})"
    result_out = create_file(job_command)
    wait_and_assert_files([result_out])
    remove_file(result_out)

    job_id = atf.submit_job_srun(f"--error={file_err} {job_command}")
    job_name = atf.get_job_parameter(job_id, "JobName")
    assert (
        job_command == job_name
    ), f"%x: Job command ({job_command}) is not the same as the JobName ({job_name})"
    result_err = create_file(job_command, ERROR_TYPE)
    wait_and_assert_files([result_err])
    remove_file(result_err)

    # Test %N puts the short hostname in the file name
    file_out = create_file_path("N")
    file_err = create_file_path("N", ERROR_TYPE)

    job_id = atf.submit_job_srun(f"--output={file_out} -N1 -O id")
    node_name = atf.get_job_parameter(job_id, "NodeList")
    if atf.get_version("sbin/slurmd") >= (25, 11):
        # Ticket 23357: Use the node_name instead of hostname with %N
        expected_name = node_name
    else:
        expected_name = atf.get_node_parameter(node_name, "hostname")
        node_addr = atf.get_node_parameter(node_name, "address")
        if node_addr != expected_name and not atf.is_integer(node_addr[0]):
            expected_name = node_addr
    result_out = create_file(expected_name)
    wait_and_assert_files([result_out])
    remove_file(result_out)

    atf.submit_job_srun(f"--error={file_err} -N1 -O -w {node_name} true")
    result_err = create_file(expected_name, ERROR_TYPE)
    wait_and_assert_files([result_err])
    remove_file(result_err)

    # Test %A puts the Job array's master job allocation number in the file name
    array_size = 2
    file_out = create_file_path("A")
    file_err = create_file_path("A", ERROR_TYPE)
    file_in = "file_in.A.input"
    atf.make_bash_script(file_in, f"""srun -O --output={file_out} hostname""")
    job_id = atf.submit_job_sbatch(
        f"-N1 --output=/dev/null --array=1-{array_size} {file_in}"
    )
    atf.wait_for_job_state(job_id, "DONE")
    remove_file(file_in)
    result_out = create_file(str(job_id))
    wait_and_assert_files([result_out])
    remove_file(result_out)

    atf.make_bash_script(file_in, f"""srun -O --error={file_err} uid""")
    job_id = atf.submit_job_sbatch(
        f"-N1 --output=/dev/null --array=1-{array_size} {file_in}"
    )
    atf.wait_for_job_state(job_id, "DONE")
    remove_file(file_in)
    result_err = create_file(str(job_id), ERROR_TYPE)
    wait_and_assert_files([result_err])
    remove_file(result_err)

    # Test %a puts the Job array ID in the file name
    array_size = 2
    file_out = create_file_path("A.%a")
    file_err = create_file_path("A.%a", ERROR_TYPE)
    file_in = "file_in.A.a.input"
    atf.make_bash_script(file_in, f"""srun -O --output={file_out} hostname""")
    job_id = atf.submit_job_sbatch(
        f"-N1 --output=/dev/null --array=1-{array_size} {file_in}"
    )
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = wait_and_assert_files(
        [create_file(f"{job_id}.{array_id}") for array_id in range(1, array_size + 1)]
    )
    for array_id in range(1, array_size + 1):
        id_file = create_file(str(job_id) + "." + str(array_id))
        assert (
            id_file in tmp_dir_list
        ), f"%a: Job array file ({id_file}) was not created"
        remove_file(id_file)

    atf.make_bash_script(file_in, f"""srun -O --error={file_err} uid""")
    job_id = atf.submit_job_sbatch(
        f"-N1 --output=/dev/null --array=1-{array_size} {file_in}"
    )
    atf.wait_for_job_state(job_id, "DONE")
    tmp_dir_list = wait_and_assert_files(
        [
            create_file(f"{job_id}.{array_id}", ERROR_TYPE)
            for array_id in range(1, array_size + 1)
        ]
    )
    for array_id in range(1, array_size + 1):
        id_file = create_file(str(job_id) + "." + str(array_id), ERROR_TYPE)
        assert (
            id_file in tmp_dir_list
        ), f"%a: Job array file ({id_file}) was not created"
        remove_file(id_file)
