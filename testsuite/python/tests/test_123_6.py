############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import pytest
import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
partitions = [f"{test_name}_part1", f"{test_name}_part2"]
res_name = f"{test_name}_resv"
testuser = atf.properties["test-user"]
acct1 = f"{test_name}_acct"


@pytest.fixture(scope="module", autouse=True)
def setup():
    global local_cluster_name

    atf.require_slurm_running()
    local_cluster_name = atf.get_config_parameter("ClusterName")

    # Create test Partitions
    for part in partitions:
        atf.run_command(
            f"scontrol create partitionname={part} Nodes=ALL",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    atf.run_command(
        f"sacctmgr -i add account {acct1} cluster={local_cluster_name}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add user {testuser} cluster={local_cluster_name} account={acct1}",
        user=atf.properties["slurm-user"],
    )

    # Create the reservation for partition 1
    result = atf.run_command(
        f"scontrol create reservationname={res_name} allowedpartition={partitions[0]} start=now duration=1 nodecnt=1",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, "Couldn't create the reservation!"

    yield

    atf.run_command(
        f"scontrol delete reservation {res_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove user {testuser} wckey={acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {acct1}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    for part in partitions:
        atf.run_command(
            f"scontrol delete partitionname={part}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )


def test_reservation_partition():
    """Test that a reservation created for Partition {partitions[0]} can't be used by atf"""

    # Try to run a job as in the wrong partition
    result = atf.run_command(
        f"srun -N1 --reservation={res_name} --partition={partitions[1]} true",
        user=atf.properties["test-user"],
    )
    assert (
        result["exit_code"] != 0
    ), "The job should have been denied! {result[exit_code]}"
    assert (
        "Problem using reservation" in result["stderr"]
    ), "The job should have been denied!"

    # Try to run a job that can use either partition
    result = atf.run_command(
        f"srun -N1 --reservation={res_name} --partition={partitions[1]},{partitions[0]} true",
        user=atf.properties["test-user"],
    )
    assert (
        result["exit_code"] == 0
    ), "ExitCode wasn't 0. The job should not have been denied!"
    assert (
        "Problem using reservation" not in result["stderr"]
    ), "The job should not have been denied for user!"
