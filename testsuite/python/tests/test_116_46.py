############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_count = 1


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


def test_wait(tmp_path):
    """Verify srun --wait"""
    task_count = 10
    file_in = str(tmp_path / "file_in.input")
    atf.make_bash_script(
        file_in,
        """if [[ -z "$SLURM_PROCID" ]]
then exit
fi
if [[ $SLURM_PROCID == 1 ]]
then exit
fi
sleep 20
""",
    )
    run_error = atf.run_command_error(
        f"srun -n{task_count} -N{node_count} -O -W2 {file_in}"
    )
    task_count -= 1
    assert (
        re.search(r"First task exited", run_error) is not None
    ), "First task did not exit"

    # Sometimes the list of running and/or killed tasks may not be received
    # in a single message, but we may get it split.
    # E.g.:
    # srun: error: node0: tasks 0,2-6,8-9: Killed
    # srun: error: node0: task 7: Killed
    #
    # We should support receiving it in any combination of ranges

    expected_tasks = sorted(map(int, atf.node_range_to_list("[0,2-9]")))

    # Assert running tasks
    matches = re.findall(r"task[s]?\s+([0-9,\-]+): running", run_error)
    trange = "[" + ",".join(matches) + "]"
    tlist = sorted(map(int, atf.node_range_to_list(trange)))
    assert (
        tlist == expected_tasks
    ), f"Running tasks should be {expected_tasks}, but got {tlist}"

    # Assert killed tasks
    matches = re.findall(r"task[s]?\s+([0-9,\-]+): Killed", run_error)
    trange = "[" + ",".join(matches) + "]"
    tlist = sorted(map(int, atf.node_range_to_list(trange)))
    assert (
        tlist == expected_tasks
    ), f"Killed tasks should be {expected_tasks}, but got {tlist}"
