############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Test sacct reporting of job oversubscribe and exclusive intent.

Ticket 23303 adds OverSubscribe and Exclusive fields to sacct. These tests
submit completed jobs, wait for slurmdbd to receive the record, then verify
that sacct reports the same token families used by scontrol show job.
"""

import atf
import pytest


JOB_OVERSUBSCRIBE_TOKENS = {"NO", "YES", "OK"}
JOB_EXCLUSIVE_TOKENS = {"NO", "NODE", "USER", "MCS", "TOPO"}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "bin/sacct",
        reason="Ticket 23303: sacct OverSubscribe/Exclusive added in 26.05+",
    )
    atf.require_accounting()
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs(setup):
    yield
    atf.cancel_all_jobs()


def _sacct_oversubscribe_exclusive(job_id):
    """Return (oversubscribe, exclusive), retrying until slurmdbd has the job."""
    output = ""
    for _ in atf.timer():
        output = atf.run_command_output(
            f"sacct -j {job_id} -X --noheader -P -o OverSubscribe,Exclusive",
            fatal=True,
            quiet=True,
        ).strip()
        if output and "|" in output:
            break
    else:
        assert False, (
            f"sacct never returned OverSubscribe|Exclusive for job {job_id}: "
            f"{output!r}"
        )

    return tuple(s.strip() for s in output.split("|", 1))


def _submit_wait_completed(extra_args, script_name):
    script = atf.module_tmp_path / script_name
    atf.make_bash_script(script, "true")
    job_id = atf.submit_job_sbatch(f"{extra_args} {script}", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True, timeout=30)
    return job_id


def _assert_sacct_value(field_name, value, expected):
    if isinstance(expected, set):
        assert (
            value in expected
        ), f"sacct {field_name} must be one of {expected}, got {value!r}"
    else:
        assert (
            value == expected
        ), f"sacct {field_name} should be {expected}, got {value!r}"


@pytest.mark.parametrize(
    "extra_args,script_name,expected_oversubscribe,expected_exclusive",
    [
        pytest.param(
            "",
            "sacct_shared_default.sh",
            JOB_OVERSUBSCRIBE_TOKENS,
            JOB_EXCLUSIVE_TOKENS,
            id="default",
        ),
        pytest.param(
            "--exclusive",
            "sacct_shared_exclusive_node.sh",
            "NO",
            "NODE",
            id="exclusive_node",
        ),
        pytest.param(
            "--exclusive=user",
            "sacct_shared_exclusive_user.sh",
            None,
            # select/linear promotes --exclusive=user to whole-node behavior.
            {"USER", "NODE"},
            id="exclusive_user",
        ),
        pytest.param(
            "--exclusive=topo",
            "sacct_shared_exclusive_topo.sh",
            None,
            "TOPO",
            id="exclusive_topo",
        ),
    ],
)
def test_sacct_oversubscribe_exclusive(
    extra_args, script_name, expected_oversubscribe, expected_exclusive
):
    job_id = _submit_wait_completed(extra_args, script_name)
    oversubscribe, exclusive = _sacct_oversubscribe_exclusive(job_id)

    if expected_oversubscribe is not None:
        _assert_sacct_value("OverSubscribe", oversubscribe, expected_oversubscribe)
    _assert_sacct_value("Exclusive", exclusive, expected_exclusive)


@pytest.fixture(scope="function")
def part_for_oversubscribe(setup):
    atf.require_auto_config("Needs to create/delete partition")
    atf.require_nodes(1, [("CPUs", 1)])

    part_name = "test_101_3_oversubscribe"
    nodes = list(atf.get_nodes().keys())
    atf.run_command(
        f"scontrol create PartitionName={part_name} Nodes={nodes[0]} "
        "OverSubscribe=YES:4",
        fatal=True,
        user=atf.properties["slurm-user"],
    )

    yield part_name

    atf.run_command(
        f"scontrol delete PartitionName={part_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_sacct_oversubscribe_yes(part_for_oversubscribe):
    job_id = _submit_wait_completed(
        f"-p {part_for_oversubscribe} --oversubscribe",
        "sacct_oversubscribe_yes.sh",
    )
    oversubscribe, exclusive = _sacct_oversubscribe_exclusive(job_id)

    assert (
        oversubscribe == "YES"
    ), f"--oversubscribe: sacct OverSubscribe should be YES, got {oversubscribe!r}"
    assert (
        exclusive == "NO"
    ), f"--oversubscribe: sacct Exclusive should be NO, got {exclusive!r}"
