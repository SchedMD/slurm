############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import pytest
import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
partitions = [f"{test_name}_part{i}" for i in range(3)]
acct = f"{test_name}_acct"
resv_name = f"{test_name}_resv"
testuser = atf.properties["test-user"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 11),
        "bin/scontrol",
        reason="Creating reservations with AllowedPartition= added in scontrol 25.11",
    )
    atf.require_accounting(True)

    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db():

    # Create test Partitions
    for part in partitions:
        atf.run_command(
            f"scontrol create PartitionName={part} Nodes=ALL",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield

    for part in partitions:
        atf.run_command(
            f"scontrol delete PartitionName={part}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


@pytest.fixture(scope="function")
def resv(request):
    part_access = f"AllowedPartition={partitions[0]}"
    if hasattr(request, "param"):
        part_access = request.param

    result = atf.run_command(
        f"scontrol create ReservationName={resv_name} {part_access} Start=now Duration=1 NodeCnt=1",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, "Couldn't create the reservation!"

    yield resv_name

    atf.run_command(
        f"scontrol delete reservation {resv_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_invalid_args(resv):
    """Test that invalid configurations are rejected"""

    # Mix allow and deny syntax
    part_access = f"AllowedPartitions={partitions[0]},-{partitions[1]}"
    result = atf.run_command(
        f"scontrol create ReservationName=invalid_resv {part_access} Start=now Duration=1 Nodecnt=1",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Creating reservation with allowed and denied partitions should fail"

    # Default reservation only allows partition 0
    # Clear all partition access control
    result = atf.run_command(
        f"scontrol update reservation ReservationName={resv} AllowedPartitions-={partitions[0]}",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Updating reservation to clear all partitions should fail"

    # Change from allow list to deny list
    result = atf.run_command(
        f"scontrol update reservation ReservationName={resv} AllowedPartitions=-{partitions[0]}",
        user=atf.properties["slurm-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "Updating reservation from allow list to deny list should fail"


tests = [
    {
        "part_access": f"AllowedPartitions={partitions[0]}",
        "expected": [0],
        "name": "=0",
    },
    {
        "part_access": f"AllowedPartitions={partitions[0]},{partitions[1]}",
        "expected": [0, 1],
        "name": "=0,1",
    },
    {
        "part_access": f"AllowedPartitions=-{partitions[0]}",
        "expected": [1, 2],
        "name": "=-0",
    },
    {
        "part_access": f"AllowedPartitions=-{partitions[0]},-{partitions[1]}",
        "expected": [2],
        "name": "=-0,-1",
    },
    {
        "part_access": f"AllowedPartitions-={partitions[0]},{partitions[1]}",
        "expected": [2],
        "name": "-=0,1",
    },
]

args = [(x["part_access"], x["expected"]) for x in tests]
ids = [x["name"] for x in tests]


def assert_resv_access(resv, part, is_allowed):
    result = atf.run_command(
        f"srun -N1 --reservation={resv} --partition={part} true",
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
    # Try to run a job in each partition
    for i in range(len(partitions)):
        part = partitions[i]
        assert_resv_access(resv, part, i in expect)


@pytest.mark.parametrize("resv, expect", args, indirect=["resv"], ids=ids)
def test_reservation_partition(resv, expect):
    """Test that a reservation created with AllowedPartitions= restricts access correctly"""

    assert_resv_access_each(resv, expect)


def test_reservation_partition_any(resv):
    """Test that a job may use reservations when any partition is allowed"""

    # Default reservation only allows part 0
    assert_resv_access_each(resv, [0])

    assert_resv_access(resv, f"{partitions[0]},{partitions[1]}", True)
    assert_resv_access(resv, f"{partitions[0]},{partitions[2]}", True)

    assert_resv_access(resv, f"{partitions[1]},{partitions[2]}", False)


def test_reservation_partition_update(resv):
    """Test that updating a reservation's AllowedPartitions= restricts access correctly"""

    # Add part 1
    atf.run_command(
        f"scontrol update ReservationName={resv} AllowedPartitions+={partitions[1]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [0, 1])

    # Remove part 0
    atf.run_command(
        f"scontrol update ReservationName={resv} AllowedPartitions-={partitions[0]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [1])

    # Set to part 2
    atf.run_command(
        f"scontrol update ReservationName={resv} AllowedPartitions={partitions[2]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert_resv_access_each(resv, [2])
