############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf
import re
import logging

node1 = ""
node2 = ""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=False)
def node_list():
    return atf.run_job_nodes("-N2 true")


@pytest.fixture(scope="function")
def create_resv(request, node_list):
    global node1, node2
    node1, node2 = node_list
    flag = request.param

    atf.run_command(
        "scontrol create reservation reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={flag}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    if re.search(
        rf"(?:Nodes=)({node2})", atf.run_command_output("scontrol show res resv1")
    ):
        node_list.reverse()
        node1, node2 = node_list

    return [node1, node2, flag]


@pytest.fixture(scope="function")
def delete_resv():
    yield

    atf.run_command(
        f"scontrol update nodename={node1},{node2} state=idle reason='testing'",
        user=atf.properties["slurm-user"],
    )

    atf.run_command(
        "scontrol delete reservation resv1", user=atf.properties["slurm-user"]
    )


@pytest.mark.parametrize("create_resv", ["REPLACE_DOWN", "REPLACE", ""], indirect=True)
def test_replace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with flags replace and replace_down
    are replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})",
            atf.run_command_output(f"scontrol show reservationname resv1"),
        )
        is not None
    )

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert that resv1 now has {node2}")
    assert atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(rf"(?:Nodes=)({node2})", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Assert that {node2} now has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    logging.info(f"Assert that {node1} has NOT the +RESERVED state anymore")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: not re.search(rf"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )


@pytest.mark.parametrize("create_resv", ["STATIC_ALLOC", "MAINT"], indirect=True)
def test_noreplace_flags(create_resv, delete_resv):
    """Verify that nodes in a reservation with static allocations aren't
    replaced when they go down
    """

    node1, node2, flag = create_resv

    logging.info(f"Assert that resv1 initially has {node1}")
    assert (
        re.search(
            rf"(?:Nodes=)({node1})",
            atf.run_command_output(f"scontrol show reservationname resv1"),
        )
        is not None
    )

    logging.info(f"Assert that {node1} has the +RESERVED state")
    assert atf.repeat_command_until(
        f"scontrol show node {node1}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)", results["stdout"]),
        quiet=False,
    )

    if flag == "MAINT":
        logging.info(f"Assert that {node1} has the +MAINT state")
        assert atf.repeat_command_until(
            f"scontrol show node {node1}",
            lambda results: re.search(rf"(?:State=).+(\+MAINT)", results["stdout"]),
            quiet=False,
        )

    logging.info(f"Set {node1} down")
    atf.run_command(
        f"scontrol update nodeName={node1} state=down reason='testing'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    logging.info(f"Assert that resv1 won't have {node2}")
    assert not atf.repeat_command_until(
        "scontrol show reservationname resv1",
        lambda results: re.search(rf"(?:Nodes=)({node2})", results["stdout"]),
        timeout=15,
        quiet=False,
    )

    logging.info(f"Assert that {node2} doesn't get the +RESERVED state")
    assert not atf.repeat_command_until(
        f"scontrol show node {node2}",
        lambda results: re.search(rf"(?:State=).+(\+RESERVED)", results["stdout"]),
        timeout=15,
        quiet=False,
    )

    if flag == "MAINT":
        logging.info(f"Assert that {node1} still has the +MAINT state")
        assert atf.repeat_command_until(
            f"scontrol show node {node1}",
            lambda results: re.search(rf"(?:State=).+(\+MAINT)", results["stdout"]),
            quiet=False,
        )


@pytest.mark.parametrize(
    "flag, delete_resv",
    [
        ("MAINT,REPLACE", True),
        ("STATIC_ALLOC,REPLACE", True),
        ("MAINT,REPLACE_DOWN", True),
        ("STATIC_ALLOC,REPLACE_DOWN", True),
        ("REPLACE,MAINT", True),
        ("REPLACE_DOWN,MAINT", True),
        ("REPLACE,STATIC_ALLOC", True),
        ("REPLACE_DOWN,STATIC_ALLOC", True),
        ("HOURLY,WEEKDAY", True),
        ("WEEKDAY,WEEKEND", True),
        ("WEEKEND,WEEKLY", True),
        ("WEEKLY,HOURLY", True),
        ("TIME_FLOAT,HOURLY", True),
        ("TIME_FLOAT,WEEKDAY", True),
        ("TIME_FLOAT,WEEKEND", True),
        ("TIME_FLOAT,WEEKLY", True),
        ("HOURLY,TIME_FLOAT", True),
        ("WEEKDAY,TIME_FLOAT", True),
        ("WEEKEND,TIME_FLOAT", True),
        ("WEEKLY,TIME_FLOAT", True),
    ],
)
def test_incomp_flags(flag, delete_resv):
    """Verify that reservations are not allowed to be created with incompatible
    flags"""

    logging.info(f"Attempt to create a reservation with incompatible flags: {flag}")
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = "Error creating the reservation"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), f"Could not find {expected_output} in {result['stderr']}"

    logging.info(f"Assert exit code is not 0")
    assert result["exit_code"] != 0, "The command was supposed to fail, but didn't!"


@pytest.mark.parametrize(
    "create_flag, update_flag",
    [
        ("MAINT", "REPLACE"),
        ("STATIC_ALLOC", "REPLACE"),
        ("MAINT", "REPLACE_DOWN"),
        ("STATIC_ALLOC", "REPLACE_DOWN"),
        ("REPLACE", "MAINT"),
        ("REPLACE_DOWN", "MAINT"),
        ("REPLACE", "STATIC_ALLOC"),
        ("REPLACE_DOWN", "STATIC_ALLOC"),
        ("HOURLY", "WEEKDAY"),
        ("WEEKDAY", "WEEKEND"),
        ("WEEKEND", "WEEKLY"),
        ("WEEKLY", "HOURLY"),
        ("TIME_FLOAT", "HOURLY"),
        ("TIME_FLOAT", "WEEKDAY"),
        ("TIME_FLOAT", "WEEKEND"),
        ("TIME_FLOAT", "WEEKLY"),
        ("HOURLY", "TIME_FLOAT"),
        ("WEEKDAY", "TIME_FLOAT"),
        ("WEEKEND", "TIME_FLOAT"),
        ("WEEKLY", "TIME_FLOAT"),
    ],
)
def test_update_incomp_flags(create_flag, update_flag, delete_resv):
    """Verify that reservations created with a a given flag cannot be
    updated with incompatible flags"""
    result = atf.run_command(
        "scontrol create reservationname=resv1 "
        f"user={atf.properties['test-user']} start=now duration=1 nodecnt=1 "
        f"flags={create_flag}",
        user=atf.properties["slurm-user"],
    )

    result = atf.run_command(
        f"scontrol update reservation=resv1 flags={update_flag}",
        user=atf.properties["slurm-user"],
        xfail=True,
        fatal=True,
    )

    expected_output = "slurm_update error"
    logging.info(f"Assert output message contains {expected_output}")
    assert (
        expected_output in result["stderr"]
    ), "Could not find 'error updating the res' in output"

    logging.info(f"Assert exit code is not 0")
    assert result["exit_code"] != 0, "The command was supposed to fail, but didn't!"
