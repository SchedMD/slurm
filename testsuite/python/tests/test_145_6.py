############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test setting node Extra with scontrol and slurmd for extra_constraints.

With SchedulerParameters=extra_constraints a node's Extra is parsed as JSON
and jobs filter nodes on it with --extra. scontrol rejects malformed Extra and
leaves the node unchanged. slurmd --extra stores any string, but Extra that
slurmctld cannot parse never matches a job.

How long slurmd --extra persists across restarts is covered by test_150_5.

Ticket 18133.
"""

import time

import pytest

import atf

INVALID_EXTRA = "Invalid extra constraints specification"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("SchedulerParameters", "extra_constraints")
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    # slurmd -b below simulates a reboot so slurmctld applies its --extra.
    # Keep the node from being set DOWN as unexpectedly rebooted.
    atf.require_config_parameter("ReturnToService", 2)
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def extra_node():
    """The node to set Extra on.

    The other nodes have no Extra, so a job with --extra can only run here.
    """

    return sorted(atf.get_nodes(fatal=True))[-1]


@pytest.fixture(autouse=True)
def reset_extra(extra_node):
    yield
    atf.run_command(
        f"scontrol update nodename={extra_node} extra=",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def assert_job_runs_on(node, request):
    job_id = atf.submit_job_sbatch(
        f"--extra='{request}' -o /dev/null --wrap true", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Verify job with --extra='{request}' runs"
    assert (
        atf.get_job_parameter(job_id, "NodeList", fatal=True) == node
    ), f"Verify job with --extra='{request}' runs on {node}"


def assert_job_pends(request):
    job_id = atf.submit_job_sbatch(
        f"--extra='{request}' -o /dev/null --wrap true", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="Constraints"
    ), f"Verify job with --extra='{request}' pends with Reason=Constraints"


# Cases of (node Extra, job --extra request matching it). How requests match is
# covered by test_145_7.
VALID_EXTRA = [
    ('{"key":"value"}', "key=value"),
    ('{"number":123}', "number=123"),
    ('{"bool":true}', "bool=true"),
    ('{"spaces":" value with spaces "}', "spaces= value with spaces "),
    ('{"unicode":"\u00e9"}', "unicode=\u00e9"),
    ('{"special":"@#$%^*"}', "special=@#$%^*"),
    ('{"a":1,"b":2,"c":3}', "a=1,b=2,c=3"),
    ('{"long":"' + "x" * 1000 + '"}', "long=" + "x" * 1000),
]


@pytest.mark.parametrize(
    "extra,request_expr",
    VALID_EXTRA,
    ids=[
        "string",
        "number",
        "bool",
        "spaces",
        "unicode",
        "special",
        "multiple",
        "long",
    ],
)
def test_scontrol_valid_extra(extra_node, extra, request_expr):
    """Verify scontrol sets valid JSON Extra and jobs filter on it"""

    atf.run_command(
        f"scontrol update nodename={extra_node} extra='{extra}'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        atf.get_node_parameter(extra_node, "extra") == extra
    ), "Verify scontrol set the node Extra"

    assert_job_runs_on(extra_node, request_expr)


# Malformed for every serializer that can provide JSON, as some accept unquoted
# strings
@pytest.mark.parametrize("extra", ['{"unclosed":"value}', "{", "}"])
def test_scontrol_invalid_extra(extra_node, extra):
    """Verify scontrol rejects malformed Extra"""

    atf.run_command(
        f"scontrol update nodename={extra_node} extra='{{\"a\":1}}'",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    result = atf.run_command(
        f"scontrol update nodename={extra_node} extra='{extra}'",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert result["exit_code"] != 0, "Verify scontrol rejects the Extra"
    assert INVALID_EXTRA in result["stderr"], "Verify scontrol reports why"
    assert (
        atf.get_node_parameter(extra_node, "extra") == '{"a":1}'
    ), "Verify the node Extra is unchanged"


@pytest.mark.parametrize(
    "extra,matches",
    [('{"key":"value"}', True), ('{"key":"value"', False)],
    ids=["valid", "invalid"],
)
def test_slurmd_extra(extra_node, extra, matches):
    """Verify slurmd --extra sets Extra and only valid JSON is used by jobs"""

    # slurmctld only applies --extra when slurmd_start_time changes and the
    # boot time (set by -b) is after the node last responded. Both have a one
    # second resolution.
    atf.stop_slurmd(extra_node)
    stopped = int(time.time())
    for t in atf.timer(fatal=True):
        if int(time.time()) > stopped:
            break
    atf.start_slurmd(extra_node, f"-b --extra '{extra}'", wait="idle")

    assert (
        atf.get_node_parameter(extra_node, "extra") == extra
    ), "Verify slurmd set the node Extra even if it is not valid JSON"

    if matches:
        assert_job_runs_on(extra_node, "key=value")
    else:
        assert_job_pends("key=value")
