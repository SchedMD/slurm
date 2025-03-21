############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(3)
    atf.require_slurm_running()


def reserve_node():
    opts = "--nodes=1-1 --exclusive --wrap 'sleep infinity'"
    job_id = atf.submit_job_sbatch(opts, fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return atf.get_job_parameter(job_id, "NodeList")


def update_state(nodes, state):
    nodes = ",".join(nodes)
    atf.run_command(
        f"scontrol update nodename={nodes} state={state} reason=testing",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module", autouse=False)
def set_states():
    # Has alloc state flag
    node_down = reserve_node()
    node_drain = reserve_node()
    node_alloc = reserve_node()

    # Set down state flag
    update_state([node_down, node_drain], "down")

    # Set idle+drain state flags
    update_state([node_drain], "idle")
    update_state([node_drain], "drain")

    yield (node_down, node_drain, node_alloc)

    update_state([node_down, node_drain], "idle")


def filter_state(pattern, all_nodes):
    node_set = ",".join(all_nodes)
    cmd = f"sinfo --Node --noheader --nodes={node_set} --states='{pattern}'"
    return atf.run_command_output(cmd, fatal=True)


params = [("+", "~"), ("+", "!"), ("&", "~"), ("&", "!")]


@pytest.mark.parametrize("and_op, not_op", params)
def test_state(and_op, not_op, set_states):
    """Verify sinfo -t filters correctly"""

    node_down, node_drain, node_alloc = set_states

    def _assert_filter(pattern, nodes):
        output = filter_state(pattern, [node_down, node_drain, node_alloc])

        def _assert_state(node, desc):
            missing = re.search(node, output) is None
            if node in nodes:
                valid = not missing
                verb = "keep"
            else:
                valid = missing
                verb = "filter"

            assert valid, f"Expected {pattern} to {verb} {desc} node"

        _assert_state(node_down, "down")
        _assert_state(node_drain, "drain")
        _assert_state(node_alloc, "alloc")

    and_not = and_op + not_op

    _assert_filter("idle" + and_op + "drain", [node_drain])
    _assert_filter("down" + and_not + "drain", [node_down])
    _assert_filter("alloc" + and_not + "drain", [node_alloc])
    _assert_filter(not_op + "drain", [node_down, node_alloc])
