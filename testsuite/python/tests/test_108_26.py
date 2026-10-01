############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that scontrol update rejects malformed input for every update tag.

This test is the place to vet the tags recognized by "scontrol update". Every
tag in UPDATE_TAGS is run with the same malformed command lines (a space after
'=', a trailing argument, a missing '=', ...). Each must fail, must not send
its update RPC to slurmctld, and must leave slurmctld running. Whether the RPC
was sent is read from the per-message-type counters in sdiag, so the check is
the same for every tag. A well-formed control command per tag proves those
counters move, so the malformed checks cannot pass by accident.

When adding a tag to "scontrol update", add it to UPDATE_TAGS so it is vetted
the same way.

Ticket 25491: a stray space after '=' (e.g. "SuspendExcNodes= node[1-2]") or a
trailing argument made scontrol send a SuspendExc* update with no value, which
crashed slurmctld. SlurmctldDebug was parsed the same way, and an empty value
silently set the debug level to quiet.
"""

import pathlib
import re
from dataclasses import dataclass

import pytest

import atf

TEST_NAME = "test_108_26"

SETTINGS = ["SuspendExcNodes", "SuspendExcParts", "SuspendExcStates"]


@dataclass(frozen=True)
class UpdateTag:
    """A tag recognized by "scontrol update".

    name: The tag as typed on the command line.
    rpc: The message type the update sends to slurmctld, as shown by sdiag.
    target: Key into the targets fixture for the tag's value.
    param: A parameter to update, or None for settings that take only a value.
    exempt: Malformed forms that are valid syntax for this tag.
    """

    name: str
    rpc: str
    target: str
    param: str = None
    exempt: tuple = ()


UPDATE_TAGS = [
    UpdateTag("NodeName", "REQUEST_UPDATE_NODE", "node", f"Comment={TEST_NAME}"),
    UpdateTag("PartitionName", "REQUEST_UPDATE_PARTITION", "partition", "State=UP"),
    # The job parser accepts "JobId <id>" and "JobName <name>" as well as '='.
    UpdateTag(
        "JobId", "REQUEST_UPDATE_JOB", "job", "TimeLimit=4", exempt=("no_equals",)
    ),
    UpdateTag(
        "JobName",
        "REQUEST_UPDATE_JOB",
        "job_name",
        "TimeLimit=4",
        exempt=("no_equals",),
    ),
    UpdateTag("StepId", "REQUEST_UPDATE_JOB_STEP", "step", "TimeLimit=4"),
    UpdateTag(
        "ReservationName", "REQUEST_UPDATE_RESERVATION", "reservation", "Duration=11"
    ),
    UpdateTag("HRESName", "REQUEST_UPDATE_HRES", "hres", "Count=1"),
    UpdateTag("SlurmctldDebug", "REQUEST_SET_DEBUG_LEVEL", "debug_level"),
    UpdateTag("SuspendExcNodes", "REQUEST_SET_SUSPEND_EXC_NODES", "node"),
    UpdateTag("SuspendExcParts", "REQUEST_SET_SUSPEND_EXC_PARTS", "partition"),
    UpdateTag("SuspendExcStates", "REQUEST_SET_SUSPEND_EXC_STATES", "node_state"),
]

# Malformed command lines tried for every tag. {tag} is the tag, {value} its
# target, and {param} the tag's parameter (empty for tags without one), split
# into {param_name} and {param_value}. Forms using {param_name} are only tried
# for tags with a parameter.
MALFORMED_FORMS = {
    "space_after_equals": "{tag}= {value} {param}",
    "space_after_plus_equals": "{tag}+= {value} {param}",
    "param_space_after_equals": "{tag}={value} {param_name}= {param_value}",
    "space_in_list": "{tag}={value}, {value} {param}",
    "trailing_word": "{tag}={value} {param} junk",
    "trailing_unknown_param": "{tag}={value} {param} x=y",
    "no_equals": "{tag} {value} {param}",
    "bare_tag": "{tag}",
}

MALFORMED_CASES = [
    pytest.param(tag, form, id=f"{tag.name}-{form}")
    for tag in UPDATE_TAGS
    for form, template in MALFORMED_FORMS.items()
    if form not in tag.exempt and (tag.param or "{param_name}" not in template)
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25491: scontrol sent malformed updates before 26.11",
    )
    atf.require_nodes(2)
    atf.require_slurm_running()


def _get_setting(name):
    """Return the live value of a setting, or "" if unset.

    Not atf.get_config_parameter(), which lowercases values, so node and
    partition names would not round-trip when the settings are restored.
    """
    value = atf.get_config().get(name)
    assert value is not None, f"{name} not found in scontrol show config"
    return "" if value == "(null)" else value


def _update(args, fatal=False):
    """Run scontrol update. Tests leave fatal off as many updates should fail."""
    return atf.run_command(
        f"scontrol update {args}", user=atf.properties["slurm-user"], fatal=fatal
    )


def _rpc_count(rpc):
    """Return how many times slurmctld has received an RPC type."""
    output = atf.run_command_output("sdiag", fatal=True, quiet=True)
    match = re.search(rf"^\s*{rpc}\s+\(\s*\d+\)\s+count:(\d+)", output, re.MULTILINE)
    return int(match.group(1)) if match else 0


def _command(tag, targets, form):
    param_name, _, param_value = (tag.param or "").partition("=")
    args = form.format(
        tag=tag.name,
        value=targets[tag.target],
        param=tag.param or "",
        param_name=param_name,
        param_value=param_value,
    )
    return " ".join(args.split())


@pytest.fixture(autouse=True)
def restore_settings():
    saved = {name: _get_setting(name) for name in SETTINGS}
    saved_debug = _get_setting("SlurmctldDebug")
    yield saved_debug
    for name, value in saved.items():
        _update(f"{name}={value}", fatal=True)
    atf.run_command(
        f"scontrol setdebug {saved_debug}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def nodes():
    return sorted(atf.get_nodes(fatal=True))[:2]


@pytest.fixture(scope="module")
def targets(nodes):
    """Objects for each tag to update, created and cleaned up by the test."""
    node = nodes[0]
    comment = atf.get_node_parameter(node, "comment", default="")
    # MAINT and OVERLAP so the reservation can be created whatever the node's
    # state or other reservations; it only needs to exist to be updated.
    atf.run_command(
        f"scontrol create reservation ReservationName={TEST_NAME} "
        f"Users={atf.properties['test-user']} StartTime=now+1hour Duration=10 "
        f"Nodes={node} Flags=MAINT,OVERLAP",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield {
        "node": node,
        "partition": atf.default_partition(),
        "reservation": TEST_NAME,
        "hres": TEST_NAME,
        "debug_level": "3",
        "node_state": "DOWN",
    }

    atf.run_command(
        f"scontrol delete ReservationName={TEST_NAME}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _update(f'NodeName={node} Comment="{comment}"', fatal=True)


@pytest.fixture
def held_job():
    """A held job for the job and step tags. conftest cancels it after the test."""
    return atf.submit_job_sbatch(
        f'--hold -J {TEST_NAME} -t 5 --wrap "sleep infinity"', fatal=True
    )


def _tag_targets(tag, targets, request):
    """Return targets, adding a fresh held job if the tag updates a job."""
    if tag.target not in ("job", "job_name", "step"):
        return targets
    job_id = request.getfixturevalue("held_job")
    return {**targets, "job": job_id, "job_name": TEST_NAME, "step": f"{job_id}.0"}


@pytest.fixture(scope="module")
def no_value_client():
    """Compile the helper in the module's directory; absolute for later tests."""
    source_file = pathlib.Path(__file__).with_suffix(".c")
    atf.compile_against_libslurm(str(source_file), TEST_NAME, fatal=True)
    return pathlib.Path(TEST_NAME).resolve()


@pytest.mark.parametrize("tag", UPDATE_TAGS, ids=lambda tag: tag.name)
def test_wellformed_update_sent(tag, targets, request):
    """Verify a well-formed update sends its RPC, so the counter can be trusted."""
    args = _command(tag, _tag_targets(tag, targets, request), "{tag}={value} {param}")
    before = _rpc_count(tag.rpc)

    _update(args)

    assert atf.is_slurmctld_running(), f"slurmctld died on scontrol update {args}"
    assert (
        _rpc_count(tag.rpc) > before
    ), f"scontrol update {args} did not send {tag.rpc}"


@pytest.mark.parametrize("tag, form", MALFORMED_CASES)
def test_malformed_update_rejected(tag, form, targets, request):
    """Verify a malformed update fails without reaching slurmctld."""
    args = _command(tag, _tag_targets(tag, targets, request), MALFORMED_FORMS[form])
    before = _rpc_count(tag.rpc)

    result = _update(args)

    assert atf.is_slurmctld_running(), f"slurmctld died on scontrol update {args}"
    assert result["exit_code"] != 0, f"scontrol update {args} should fail"
    assert (
        _rpc_count(tag.rpc) == before
    ), f"scontrol update {args} sent {tag.rpc} instead of rejecting the input"


@pytest.mark.parametrize("setting", ["nodes", "parts", "states"])
def test_no_value_rpc_rejected(setting, no_value_client):
    """Verify slurmctld rejects a SuspendExc* update with no value."""
    result = atf.run_command(
        f"{no_value_client} {setting}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    output = result["stdout"]

    assert atf.is_slurmctld_running(), f"slurmctld died on {setting} with no value"
    assert re.search(r"rc=-1\b", output), f"Update with no value succeeded: {output}"
    assert "Invalid argument" in output, f"Unexpected error for {setting}: {output}"
    assert (
        "Internal Helper Error" not in result["stderr"]
    ), f"The helper script had an internal error for {setting}: {result['stderr']}"


def _check_list_update(name, args, expected, expand):
    result = _update(args)
    assert result["exit_code"] == 0, f"scontrol update {args} failed"
    assert (
        expand(_get_setting(name)) == expected
    ), f"scontrol update {args} left {name}={_get_setting(name)}"


def _state_part_expand(value):
    return {item.casefold() for item in value.split(",") if item}


def _node_expand(value):
    return set(atf.node_range_to_list(value)) if value else set()


# SuspendExcParts stores partition names without checking they exist, so made-up
# names avoid needing two partitions.
@pytest.mark.parametrize(
    "name, first, second, expand",
    [
        ("SuspendExcParts", f"{TEST_NAME}_a", f"{TEST_NAME}_b", _state_part_expand),
        ("SuspendExcStates", "down", "drain", _state_part_expand),
        ("SuspendExcNodes", None, None, _node_expand),
    ],
)
def test_suspend_exc_updates(name, first, second, expand, nodes):
    """Verify set, add, remove and clear of SuspendExc*."""

    # The node names can not be set in the parameterize setup since the fixture
    # is needed.
    if name == "SuspendExcNodes":
        first = nodes[0]
        second = nodes[1]

    for args, expected in (
        (f"{name}={first}", {first}),
        (f"{name}+={second}", {first, second}),
        (f"{name}-={first}", {second}),
        (f"{name}=", set()),
    ):
        _check_list_update(name, args, expected, expand)


def test_slurmctld_debug_empty_rejected(restore_settings):
    """Verify an empty SlurmctldDebug value is rejected, not set to quiet."""
    orig_debug = restore_settings
    result = _update("SlurmctldDebug=")

    assert result["exit_code"] != 0, "scontrol update SlurmctldDebug= should fail"
    assert (
        "invalid debug level" in result["stderr"]
    ), f"Unexpected error for SlurmctldDebug=: {result['stderr']}"
    assert (
        _get_setting("SlurmctldDebug") == orig_debug
    ), "scontrol update SlurmctldDebug= changed the debug level"


def test_slurmctld_debug_set(restore_settings):
    """Verify a valid SlurmctldDebug update still works."""
    orig_debug = restore_settings
    new_debug, new_debug_str = (
        (5, "debug") if (orig_debug != "debug") else (6, "debug2")
    )
    result = _update(f"SlurmctldDebug={new_debug}")

    assert (
        result["exit_code"] == 0
    ), f"scontrol update SlurmctldDebug={new_debug} failed"
    assert (
        _get_setting("SlurmctldDebug") == new_debug_str
    ), f"scontrol update SlurmctldDebug={new_debug} did not set the level to {new_debug_str}"
