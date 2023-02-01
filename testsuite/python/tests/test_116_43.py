############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re
import pexpect

node_count = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


def test_input_ouput_taskid():
    """Verify srun stdin/out routing with specific task number."""

    task_id = 4
    task_num = 10

    # Test --input={task_id} runs Task task_id until the other tasks close
    child = pexpect.spawn(f"srun --input={task_id} -O -v --wait=2 -N1 -n{task_num} bash")
    assert child.expect('task 4: Killed') == 0, f"Task ({task_id}) was not killed when other tasks closed"
    child_out = str(child.before)
    match = re.search(r"jobid (\d+)", child_out)
    job_id = match.group(1)
    step_id = f"StepId={job_id}.0"
    assert re.search(step_id, child_out) is not None, f"Step ({step_id}) not found in verbose output"
    assert re.search(f"{step_id} task {task_id}: running", child_out), f"Task ({task_id}) failed to run"
    assert re.search(f"{step_id} tasks 0-{task_id - 1},{task_id + 1}-{task_num - 1}: exited", child_out), f"Other tasks failed to exit"

    # Test --output={task_id} sends only {task_id}'s output to stdout
    stdout = atf.run_command_output(f"srun --output={task_id} -O -N1 -n{task_num} env")
    match = re.search(r"SLURM_PROCID=(\d+)", stdout)
    proc_id = match.group(1)
    assert str(task_id) == proc_id, f"Wrong task id ({proc_id}) responded, expected task id ({task_id})"
