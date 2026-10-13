############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test multi-partition job submission with srun and salloc.

Issue 50273: with AccountingStorageEnforce=limits, srun or salloc asking
for several partitions deadlocked slurmctld.
"""

import os

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
part_a = f"{test_name}_a"
part_b = f"{test_name}_b"
acct = f"{test_name}_acct"
test_user = atf.properties["test-user"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    # Issue 50273 only happened with limits enforced.
    atf.require_config_parameter_includes("AccountingStorageEnforce", "limits")
    # With defer, -I requests fail.
    atf.require_config_parameter_excludes("SchedulerParameters", "defer")
    atf.require_slurm_running()

    atf.run_command(
        f"sacctmgr -i add account {acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Wait for slurmctld to get the association.
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"scontrol show assoc_mgr flags=assoc users={test_user} accounts={acct}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        if f"Account={acct} UserName={test_user}(" in output:
            break

    for part in [part_a, part_b]:
        atf.run_command(
            f"scontrol create PartitionName={part} Nodes=ALL",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    yield

    for part in [part_a, part_b]:
        atf.run_command(
            f"scontrol delete PartitionName={part}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.run_command(
        f"sacctmgr -i delete user {test_user} account={acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i delete account {acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(params=[(part_a, part_b), (part_b, part_a)], ids=["a_high", "b_high"])
def part_high(request):
    high, low = request.param
    for part, tier in [(low, 1), (high, 2)]:
        atf.run_command(
            f"scontrol update PartitionName={part} PriorityTier={tier}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    return high


@pytest.mark.parametrize(
    "first, second",
    [(part_a, part_b), (part_b, part_a)],
    ids=["a_first", "b_first"],
)
@pytest.mark.parametrize("command", ["srun", "salloc"])
def test_multi_partition(command, first, second, part_high):
    """Verify the job runs in the higher PriorityTier partition of the list."""

    result = atf.run_command(
        f"{command} -I -A {acct} -p {first},{second} printenv SLURM_JOB_PARTITION"
    )
    assert result["exit_code"] == 0, f"{command} failed with two partitions"
    assert result["stdout"].strip() == part_high, f"Job did not run in {part_high}"
    assert atf.run_command_exit("squeue") == 0, "slurmctld is not answering job queries"
