############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test scontrol KV and KVPatch.

Cover jobs, nodes, partitions and reservations.
"""

import json
import shlex

import pytest

import atf

RESV_NAME = "resv_kv"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 51034: KV was added in 26.11"
    )
    atf.require_slurm_running()


@pytest.fixture
def job_id():
    """Keep the job held so it stays pending throughout the test."""

    return atf.submit_job_sbatch('--hold --wrap "sleep infinity"', fatal=True)


def kv_arg(tag, value):
    """Format a <tag>=<json> argument, quoted for the shell."""

    return f"{tag}={shlex.quote(value)}"


def update(target, tag, value, fatal=True, **run_command_kwargs):
    """Run scontrol update <target> <tag>=<value>."""

    return atf.run_command(
        f"scontrol update {target} {kv_arg(tag, value)}",
        fatal=fatal,
        **run_command_kwargs,
    )


def decode_kv(obj):
    """Return the KV document an object carries in --json output, or None."""

    return obj.get("kv") or None


def job_kv(job_id):
    return decode_kv(atf.get_jobs(job_id, use_json=True, fatal=True)[job_id])


def show_kv(entity, name, key):
    output = atf.run_command_output(f"scontrol --json show {entity} {name}", fatal=True)
    return decode_kv(json.loads(output)[key][0])


def test_job_kv_set_and_clear(job_id):
    """Verify KV= replaces the job's document and an empty KV= clears it."""

    target = f"jobid={job_id}"

    update(target, "KV", '{"a":1,"b":"x"}')
    assert job_kv(job_id) == {"a": 1, "b": "x"}, "KV= should set the document"

    update(target, "KV", '{"c":2}')
    assert job_kv(job_id) == {"c": 2}, "KV= should replace the whole document"

    update(target, "KV", "")
    assert job_kv(job_id) is None, "An empty KV= should clear the document"


def test_job_kvpatch(job_id):
    """Verify KVPatch= applies every operation in order."""

    target = f"jobid={job_id}"

    update(target, "KV", '{"a":1}')
    update(
        target,
        "KVPatch",
        '[{"op":"add","path":"/b","value":2},'
        '{"op":"replace","path":"/a","value":3}]',
    )
    assert job_kv(job_id) == {"a": 3, "b": 2}, "KVPatch= should apply every operation"


def test_job_kvpatch_no_change(job_id):
    """An unchanged nonempty patch returns SLURM_NO_CHANGE_IN_DATA."""

    target = f"jobid={job_id}"
    update(target, "KV", '{"a":1}')
    result = update(
        target,
        "KVPatch",
        '[{"op":"test","path":"/a","value":1}]',
        xfail=True,
    )
    assert result["exit_code"] != 0, "No-change status should reach scontrol"
    assert job_kv(job_id) == {"a": 1}, "A no-op patch must preserve the document"


def test_job_kvpatch_test_op(job_id):
    """Verify that a KVPatch that fails partway leaves the document unchanged.

    The same compare-and-swap operation succeeds when the test matches.
    """

    target = f"jobid={job_id}"

    update(target, "KV", '{"state":"old"}')

    result = update(
        target,
        "KVPatch",
        '[{"op":"add","path":"/x","value":1},'
        '{"op":"test","path":"/state","value":"other"}]',
        xfail=True,
    )
    assert result["exit_code"] != 0, "A failing test operation should be rejected"
    assert job_kv(job_id) == {
        "state": "old"
    }, "A rejected patch should not apply its earlier operations"

    update(
        target,
        "KVPatch",
        '[{"op":"test","path":"/state","value":"old"},'
        '{"op":"replace","path":"/state","value":"new"}]',
    )
    assert job_kv(job_id) == {
        "state": "new"
    }, "A matching test should let the patch apply"


def test_job_kv_must_be_object(job_id):
    """Verify that a KV document that is not a JSON object is rejected."""

    target = f"jobid={job_id}"

    update(target, "KV", '{"a":1}')

    result = update(target, "KV", "[1,2]", xfail=True)
    assert result["exit_code"] != 0, "A KV that is not an object should be rejected"
    assert job_kv(job_id) == {
        "a": 1
    }, "A rejected KV should leave the document unchanged"


@pytest.fixture
def admin():
    return atf.properties["slurm-user"]


@pytest.fixture
def node(admin):
    """A node whose KV is cleared after the test."""

    node = next(iter(atf.get_nodes(fatal=True)))
    yield node
    update(f"nodename={node}", "KV", "", user=admin)


@pytest.fixture
def partition(admin):
    """The default partition, with its KV cleared after the test."""

    partition = atf.default_partition()
    yield partition
    update(f"partitionname={partition}", "KV", "", user=admin)


@pytest.fixture
def reservation(admin):
    """Create a reservation with KV={"c":3}, then delete it after the test."""

    kv = kv_arg("KV", '{"c":3}')
    atf.run_command(
        f"scontrol create reservation reservationname={RESV_NAME} "
        f"starttime=now+1hour duration=10 nodecnt=1 users={admin} {kv}",
        user=admin,
        fatal=True,
    )
    yield RESV_NAME
    atf.run_command(f"scontrol delete reservation={RESV_NAME}", user=admin, fatal=True)


@pytest.mark.parametrize(
    "entity,key",
    [("node", "nodes"), ("partition", "partitions"), ("reservation", "reservations")],
)
def test_kv_operations(request, admin, entity, key):
    """Verify KV=, KVPatch= and clearing on other objects."""

    name = request.getfixturevalue(entity)
    target = f"{entity}name={name}"

    if entity == "reservation":
        assert show_kv(entity, name, key) == {
            "c": 3
        }, "KV= on create should set the document"

    update(target, "KV", '{"a":1}', user=admin)
    assert show_kv(entity, name, key) == {"a": 1}, "KV= should replace the document"

    result = update(
        target,
        "KVPatch",
        '[{"op":"test","path":"/a","value":1}]',
        user=admin,
        xfail=True,
    )
    assert result["exit_code"] != 0, "No-change status should reach scontrol"
    assert show_kv(entity, name, key) == {"a": 1}, "A no-op must preserve the document"

    update(target, "KVPatch", '[{"op":"add","path":"/b","value":2}]', user=admin)
    assert show_kv(entity, name, key) == {
        "a": 1,
        "b": 2,
    }, "KVPatch= should add the key"

    update(target, "KVPatch", '[{"op":"remove","path":"/a"}]', user=admin)
    assert show_kv(entity, name, key) == {"b": 2}, "KVPatch= should remove the key"

    update(target, "KV", "", user=admin)
    assert show_kv(entity, name, key) is None, "An empty KV= should clear the document"
