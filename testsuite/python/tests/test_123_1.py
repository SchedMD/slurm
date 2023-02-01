############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import re
import logging

node1 = ""
node2 = ""


@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope='module', autouse=False)
def node_list():
    return atf.run_job_nodes("-N2 true")


@pytest.fixture(scope='function')
def create_resv(request, node_list):
    global node1, node2
    node1, node2 = node_list
    flag = request.param

    atf.run_command(
        "scontrol create reservation reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={flag}", user=atf.properties['slurm-user'], fatal=True)

    if re.search(
        rf"(?:Nodes=)({node2})", atf.run_command_output("scontrol show res resv1")):
            node1, node2 = node_list.reverse()

    return [node1, node2, flag]


@pytest.fixture(scope='function')
def delete_resv():

    yield

    atf.run_command(
        f"scontrol update nodename={node1},{node2} state=idle reason='testing'",
        user=atf.properties['slurm-user'], fatal=True)

    atf.repeat_command_until(
        f"sinfo -h -n {node1} -o %t",
        lambda results: re.search(r'idle',results['stdout']))

    atf.run_command(
        "scontrol delete reservation resv1",
        user=atf.properties['slurm-user'],
        xfail=atf.run_command_exit("scontrol show reservationname resv1"),
        fatal=True)


@pytest.mark.parametrize("create_resv", ["REPLACE_DOWN", "REPLACE", ""], indirect=True)
def test_replace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with flags replace and replace_down
    are replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})", atf.run_command_output(
                f"scontrol show reservationname resv1"))
        is not None)

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)",
            results['stdout']),
        quiet=False)

    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties['slurm-user'], fatal=True)

    logging.info(f"Assert that resv1 now has {node2}")
    assert atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(
            rf"(?:Nodes=)({node2})", results['stdout']),
        quiet=False)

    logging.info(f"Assert that {node2} now has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)",
            results['stdout']),
        quiet=False)

    logging.info(f"Assert that {node1} has NOT the +RESERVED state anymore")
    assert atf.repeat_command_until(
            f"scontrol show node {node1}",
            lambda results: not re.search(rf"(?:State=).+(\+RESERVED)",
                results['stdout']),
        quiet=False)

# TODO: MAINT should work like STATIC_ALLOC in 23.02 (Bug 14308)
@pytest.mark.parametrize("create_resv", ["STATIC_ALLOC"], indirect=True)
def test_noreplace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with static allocations aren't
    replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})", atf.run_command_output(
                f"scontrol show reservationname resv1"))
        is not None)

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)",
            results['stdout']),
        quiet=False)


    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties['slurm-user'], fatal=True)

    logging.info(f"Assert that resv1 won't have {node2}")
    assert not atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(
            rf"(?:Nodes=)({node2})",
            results['stdout']),
        quiet=False)

    logging.info(f"Assert that {node2} doesn't get the +RESERVED state")
    assert not atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)",
            results['stdout']),
        quiet=False)


# TODO: MAINT and REPLACE_* should be incompatible options in 23.02 (Bug 14634)
