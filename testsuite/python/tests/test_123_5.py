############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import pytest
import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
qos_list = [f"{test_name}_qos{i}" for i in range(3)]
acct = f"{test_name}_acct"
resv_name = f"{test_name}_resv"
testuser = atf.properties["test-user"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 11),
        "bin/scontrol",
        reason="Creating reservations with qos= added in scontrol 25.11",
    )
    atf.require_accounting(True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "qos")

    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db():
    # Create test QOS'es and a User that can use them
    for qos in qos_list:
        atf.run_command(
            f"sacctmgr -i add qos {qos}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i add account {acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    qos_str = ",".join(qos_list)
    atf.run_command(
        f"sacctmgr -i add user {testuser} account={acct} qos={qos_str}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove user {testuser} account={acct}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    for qos in qos_list:
        atf.run_command(
            f"sacctmgr -i remove qos {qos}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


@pytest.fixture(scope="function")
def resv(request):
    qos_access = f"qos={qos_list[0]}"
    if hasattr(request, "param"):
        qos_access = request.param

    # Create the reservation for QOS test
    result = atf.run_command(
        f"scontrol create reservationname={resv_name} {qos_access} start=now duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] == 0
    ), f"Couldn't create the reservation with {qos_access}"

    yield resv_name

    atf.run_command(
        f"scontrol delete reservation {resv_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_invalid_args(resv):
    """Test that invalid configurations are rejected"""

    # Mix allow and deny syntax
    qos_access = f"qos={qos_list[0]},-{qos_list[1]}"
    result = atf.run_command(
        f"scontrol create reservationname=invalid_resv {qos_access} start=now duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Creating reservation with allowed and denied qos should fail"

    # Default reservation only allows qos 0
    # Clear all qos access control
    result = atf.run_command(
        f"scontrol update reservation ReservationName={resv} qos-={qos_list[0]}",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] != 0, "Updating reservation to clear all qos should fail"

    # Change from allow list to deny list
    result = atf.run_command(
        f"scontrol update reservation ReservationName={resv} qos=-{qos_list[0]}",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Updating reservation from allow list to deny list should fail"


tests = [
    {
        "qos_access": f"qos={qos_list[0]}",
        "expected": [0],
        "name": "=0",
    },
    {
        "qos_access": f"qos={qos_list[0]},{qos_list[1]}",
        "expected": [0, 1],
        "name": "=0,1",
    },
    {
        "qos_access": f"qos=-{qos_list[0]}",
        "expected": [1, 2],
        "name": "=-0",
    },
    {
        "qos_access": f"qos=-{qos_list[0]},-{qos_list[1]}",
        "expected": [2],
        "name": "=-0,-1",
    },
    {
        "qos_access": f"qos-={qos_list[0]},{qos_list[1]}",
        "expected": [2],
        "name": "-=0,1",
    },
]

args = [(x["qos_access"], x["expected"]) for x in tests]
ids = [x["name"] for x in tests]


def assert_resv_access(resv, qos, is_allowed):
    result = atf.run_command(
        f"srun -N1 --reservation={resv} --account={acct} --qos={qos} true",
    )
    if is_allowed:
        assert (
            result["exit_code"] == 0
        ), "ExitCode wasn't 0. The job should not have been denied!"
        assert (
            "Problem using reservation" not in result["stderr"]
        ), "The job should not have been denied for user!"
    else:
        assert (
            result["exit_code"] != 0
        ), "ExitCode was 0. The job should have been denied!"


def assert_resv_access_each(resv, expect):
    # Try to run a job in each QOS
    for i in range(len(qos_list)):
        qos = qos_list[i]
        assert_resv_access(resv, qos, i in expect)


@pytest.mark.parametrize("resv, expect", args, indirect=["resv"], ids=ids)
def test_reservation_qos(resv, expect):
    """Test that a reservation created with QOS= restricts access correctly"""

    assert_resv_access_each(resv, expect)


def test_reservation_qos_any(resv):
    """Test that a job may use reservations when any QOS is allowed"""

    # Default reservation only allows qos 0
    assert_resv_access_each(resv, [0])

    assert_resv_access(resv, f"{qos_list[0]},{qos_list[1]}", True)
    assert_resv_access(resv, f"{qos_list[0]},{qos_list[2]}", True)

    assert_resv_access(resv, f"{qos_list[1]},{qos_list[2]}", False)


def test_reservation_qos_update(resv):
    """Test that updating a reservation's QOS= restricts access correctly"""

    # Add qos 1
    atf.run_command(
        f"scontrol update ReservationName={resv} qos+={qos_list[1]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [0, 1])

    # Remove qos 0
    atf.run_command(
        f"scontrol update ReservationName={resv} qos-={qos_list[0]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [1])

    # Set to qos 2
    atf.run_command(
        f"scontrol update ReservationName={resv} qos={qos_list[2]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [2])
