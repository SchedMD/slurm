############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test dynamic node range creation and rollback of a failed create.

A `scontrol create NodeName=...` naming a range must create every node in it.
One that fails must create nothing and must leave every pre-existing node
exactly as it was, whether it fails on a duplicate name, past MaxNodeCount, or
on an invalid state.
"""

import re

import pytest

import atf

max_nodes = 6

# A failed create must not stamp its own configuration onto a node it collided
# with, so the node under test and the create that fails against it differ in
# every field assert_node_intact() checks.
intact_feature = "f1"
intact_cpus = 4
intact_memory = 1000
intact_spec = f"CPUs={intact_cpus} RealMemory={intact_memory}"

colliding_feature = "f2"
colliding_spec = "CPUs=8 RealMemory=2000"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to set required dynamic node parameters")
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("PartitionName", {"primary": {"Nodes": "ALL"}})

    # Needed for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Needed for cons_tres
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.require_slurm_running()


# Helper fixtures
@pytest.fixture
def create_node():
    created_nodes = []

    def scontrol_create_node(
        node_name, state, feature, extra="", xfail=False, fatal=False
    ):
        command = f"scontrol create NodeName={node_name} State={state}"
        command += f" Feature={feature}"
        if extra:
            command += f" {extra}"

        results = atf.run_command(
            command,
            user=atf.properties["slurm-user"],
            xfail=xfail,
            fatal=fatal,
        )

        if results["exit_code"] == 0:
            # A range expression must be expanded so the teardown can look
            # each node up by name
            if "[" in node_name:
                created_nodes.extend(atf.node_range_to_list(node_name))
            else:
                created_nodes.append(node_name)

        return results

    yield scontrol_create_node

    delete_nodes_or_fail(created_nodes)


@pytest.fixture
def register_node():
    registered_nodes = []

    def slurmd_register_node(node_name, feature, fatal=False):
        results = atf.run_command(
            f"{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name} -Z -b"
            f" --conf 'feature={feature}'",
            user="root",
            fatal=fatal,
        )

        if results["exit_code"] == 0:
            registered_nodes.append(node_name)

        return results

    yield slurmd_register_node

    # The slurmd must go before create_node's teardown deletes the node. One
    # that never came up leaves nothing to kill, and failing on that here would
    # bury why the test itself failed.
    for node_name in registered_nodes:
        node_pid = atf.run_command_output(
            f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name}'"
        ).strip()
        if node_pid:
            atf.run_command(f"kill {node_pid}", fatal=True, user="root")


# Nodes created as a side effect of a failed command are not tracked by
# create_node, which only records nodes whose creation succeeded
@pytest.fixture
def delete_nodes():
    node_names = []

    yield node_names

    delete_nodes_or_fail(node_names)


# Each test gets its own prefix so its nodes cannot collide with those of
# another test. The prefix is derived from the test name, so a leaked node
# names the test that made it. Only the tail is kept, to stay well inside the
# host name length limit, and it must not end in a digit so that prefix[1-3]
# expands unambiguously.
@pytest.fixture
def unique_node_prefix(request):
    return re.sub(r"[^a-zA-Z]", "", request.node.name)[-32:] + "node"


# Helper functions
def node_exists(node_name):
    return node_name in atf.get_nodes(live=True, quiet=True)


def node_count():
    return len(atf.get_nodes(live=True, quiet=True))


# Delete every node that still exists, then report all the failures at once. A
# fatal delete inside the loop would skip every node after it, leaking them
# into the next test's MaxNodeCount budget.
def delete_nodes_or_fail(node_names):
    errors = []

    for node_name in node_names:
        if not node_exists(node_name):
            continue

        results = atf.run_command(
            f"scontrol delete NodeName={node_name}",
            user=atf.properties["slurm-user"],
        )
        if results["exit_code"] != 0:
            errors.append(f"Failed to delete node {node_name}: {results['stderr']}")

    if errors:
        pytest.fail("; ".join(errors))


def assert_node_intact(node_name, state="FUTURE"):
    assert node_exists(node_name), f"Node {node_name} should still exist"

    node_states = atf.get_node_parameter(node_name, "state")
    assert (
        state in node_states
    ), f"Node {node_name} should still be {state}, got {node_states}"
    assert (
        "DYNAMIC_NORM" in node_states
    ), f"Node {node_name} should still be dynamic, got {node_states}"

    features = atf.get_node_parameter(node_name, "features")
    assert features == [
        intact_feature
    ], f"Node {node_name} should still have feature {intact_feature}, got {features}"

    cpus = atf.get_node_parameter(node_name, "cpus")
    assert (
        cpus == intact_cpus
    ), f"Node {node_name} should still have {intact_cpus} CPUs, got {cpus}"

    real_memory = atf.get_node_parameter(node_name, "real_memory")
    assert (
        real_memory == intact_memory
    ), f"Node {node_name} should still have {intact_memory} RealMemory, got {real_memory}"

    # An orphaned node keeps its record but loses its partition assignment,
    # which leaves it visible but unschedulable. 'primary' has Nodes=ALL, so
    # every intact node belongs to it.
    partitions = atf.get_node_parameter(node_name, "partitions")
    assert (
        partitions
    ), f"Node {node_name} should still belong to a partition, got {partitions}"
    assert (
        "primary" in partitions
    ), f"Node {node_name} should still be in 'primary', got {partitions}"


# Tests
@pytest.mark.parametrize("state", ["FUTURE", "CLOUD"])
def test_range_create_makes_every_node(create_node, unique_node_prefix, state):
    """Verify a create naming a node range makes every node in the range"""

    node_names = [f"{unique_node_prefix}{i}" for i in (1, 2, 3)]
    before_count = node_count()

    create_node(
        f"{unique_node_prefix}[1-3]",
        state,
        intact_feature,
        extra=intact_spec,
        fatal=True,
    )

    assert node_count() == before_count + len(
        node_names
    ), f"A create of {len(node_names)} nodes should add exactly that many nodes"

    for node_name in node_names:
        assert_node_intact(node_name, state=state)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25501: scontrol create deleted pre-existing nodes on failure before 26.05.5",
)
@pytest.mark.parametrize(
    "state, pre_existing_ids, create_range, not_created_ids",
    [
        ("FUTURE", [1], "1", []),
        ("FUTURE", [2], "[1-2]", [1]),
        ("FUTURE", [3], "[1-3]", [1, 2]),
        ("FUTURE", [1], "[1-2]", [2]),
        ("FUTURE", [1, 3], "[1-3]", [2]),
        ("FUTURE", [2], "[1-3]", [1, 3]),
        ("CLOUD", [2], "[1-3]", [1, 3]),
    ],
    ids=[
        "single_node",
        "range_failing_on_last",
        "range_failing_on_last_after_two",
        "range_failing_on_first",
        "range_with_untouched_pre_existing",
        "range_failing_in_middle",
        "cloud_range_failing_in_middle",
    ],
)
def test_node_stability_when_create_collides_with_existing_node(
    create_node,
    delete_nodes,
    unique_node_prefix,
    state,
    pre_existing_ids,
    create_range,
    not_created_ids,
):
    """
    Verify a create failing on a duplicate node name leaves pre-existing nodes
    intact and creates zero additional nodes.
    """

    pre_existing = [f"{unique_node_prefix}{i}" for i in pre_existing_ids]
    not_created = [f"{unique_node_prefix}{i}" for i in not_created_ids]

    for node_name in pre_existing:
        create_node(node_name, state, intact_feature, extra=intact_spec, fatal=True)

    before_count = node_count()

    # Nodes elsewhere in the nodelist must not be created either
    delete_nodes.extend(not_created)

    err = create_node(
        f"{unique_node_prefix}{create_range}",
        state,
        colliding_feature,
        extra=colliding_spec,
        xfail=True,
        fatal=True,
    )["stderr"]
    assert (
        "already exists" in err
    ), f"scontrol should report the node already exists, got: {err}"

    # Nodes created before the failing one must be rolled back
    for node_name in not_created:
        assert not node_exists(
            node_name
        ), f"Node {node_name} should not have been created"

    assert (
        node_count() == before_count
    ), "A failed create should not change the number of nodes"

    for node_name in pre_existing:
        assert_node_intact(node_name, state=state)

    # The rolled back names and their table slots must be reusable
    for node_name in not_created:
        create_node(node_name, state, intact_feature, extra=intact_spec, fatal=True)
        assert_node_intact(node_name, state=state)


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25501: scontrol create deleted pre-existing nodes on failure before 26.05.5",
)
def test_node_stability_when_create_collides_with_static_node(create_node):
    """Verify a create colliding with a slurm.conf node leaves it intact"""

    static_node = list(atf.get_nodes(live=False))[0]
    before_states = atf.get_node_parameter(static_node, "state")
    before_config = {
        parameter: atf.get_node_parameter(static_node, parameter)
        for parameter in ("cpus", "real_memory", "features")
    }

    err = create_node(
        static_node,
        "FUTURE",
        colliding_feature,
        extra=colliding_spec,
        xfail=True,
        fatal=True,
    )["stderr"]
    assert (
        "already exists" in err
    ), f"scontrol should report the node already exists, got: {err}"

    assert node_exists(static_node), f"Static node {static_node} should still exist"

    after_config = {
        parameter: atf.get_node_parameter(static_node, parameter)
        for parameter in ("cpus", "real_memory", "features")
    }
    assert (
        after_config == before_config
    ), f"Static node {static_node} config changed from {before_config} to {after_config}"

    after_states = atf.get_node_parameter(static_node, "state")
    assert (
        after_states == before_states
    ), f"Static node {static_node} state changed from {before_states} to {after_states}"

    # A static node deleted by mistake cannot be restored with scontrol create
    assert (
        "DYNAMIC_NORM" not in after_states
    ), f"Static node {static_node} should not have become dynamic, got {after_states}"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25501: scontrol create deleted pre-existing nodes on failure before 26.05.5",
)
def test_node_stability_when_create_collides_with_busy_node(
    create_node, register_node, delete_nodes, unique_node_prefix
):
    """Verify a create colliding with an allocated node leaves it and its job intact"""

    # The busy node is the second entry so the first is created and must be
    # rolled back before the collision, exercising the unwind against a node
    # that cannot itself be deleted.
    unmade_node = f"{unique_node_prefix}1"
    busy_node = f"{unique_node_prefix}2"

    create_node(busy_node, "FUTURE", intact_feature, extra=intact_spec, fatal=True)
    register_node(busy_node, intact_feature, fatal=True)
    assert atf.wait_for_node_state(
        busy_node, "IDLE", fatal=True
    ), f"Node {busy_node} should be IDLE once its slurmd has registered"

    job_id = atf.submit_job_sbatch(
        f"-p primary -w {busy_node} --exclusive --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_node_state(
        busy_node, "ALLOCATED", fatal=True
    ), f"Node {busy_node} should be ALLOCATED while running job {job_id}"

    before_config = {
        parameter: atf.get_node_parameter(busy_node, parameter)
        for parameter in ("cpus", "real_memory", "features")
    }
    before_count = node_count()
    delete_nodes.append(unmade_node)

    err = create_node(
        f"{unique_node_prefix}[1-2]",
        "FUTURE",
        colliding_feature,
        extra=colliding_spec,
        xfail=True,
        fatal=True,
    )["stderr"]
    assert (
        "already exists" in err
    ), f"scontrol should report the node already exists, got: {err}"

    assert node_exists(busy_node), f"Allocated node {busy_node} should still exist"

    node_states = atf.get_node_parameter(busy_node, "state")
    assert (
        "ALLOCATED" in node_states
    ), f"Node {busy_node} should still be ALLOCATED, got {node_states}"

    after_config = {
        parameter: atf.get_node_parameter(busy_node, parameter)
        for parameter in ("cpus", "real_memory", "features")
    }
    assert (
        after_config == before_config
    ), f"Node {busy_node} config changed from {before_config} to {after_config}"

    assert not node_exists(
        unmade_node
    ), f"Node {unmade_node} should not have been created"
    assert (
        node_count() == before_count
    ), "A failed create should not change the number of nodes"

    # The allocation the collision could have destroyed must still be live and
    # releasable
    atf.cancel_jobs([job_id], fatal=True)
    assert atf.wait_for_node_state(
        busy_node, "IDLE", fatal=True
    ), f"Node {busy_node} should return to IDLE once job {job_id} is gone"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25501: scontrol create left a node behind past MaxNodeCount before 26.05.5",
)
def test_node_stability_when_create_exceeds_max_node_count(
    create_node, delete_nodes, unique_node_prefix
):
    """Verify a create exceeding MaxNodeCount leaves pre-existing nodes intact"""

    # Fill the table to one free slot, so the two node range below overruns
    # MaxNodeCount by exactly one
    if (base_nodes := node_count()) > max_nodes - 2:
        pytest.fail(
            f"Test needs 2 free node slots, have {max_nodes - base_nodes}"
            f" (MaxNodeCount={max_nodes})"
        )

    pre_existing = []
    for i in range(base_nodes, max_nodes - 1):
        node_name = f"{unique_node_prefix}{i}"
        create_node(node_name, "FUTURE", intact_feature, extra=intact_spec, fatal=True)
        pre_existing.append(node_name)

    before_count = node_count()

    overrun_first = f"{unique_node_prefix}{max_nodes}"
    overrun_second = f"{unique_node_prefix}{max_nodes + 1}"
    delete_nodes.append(overrun_first)
    delete_nodes.append(overrun_second)

    err = create_node(
        f"{unique_node_prefix}[{max_nodes}-{max_nodes + 1}]",
        "FUTURE",
        colliding_feature,
        extra=colliding_spec,
        xfail=True,
        fatal=True,
    )["stderr"]
    assert (
        "node table is full" in err
    ), f"scontrol should report the node table is full, got: {err}"

    # The batch is rejected as a whole. overrun_first fits the last free slot,
    # so it is the node a partial create would leave behind.
    assert not node_exists(
        overrun_first
    ), f"Node {overrun_first} should not have been created past MaxNodeCount"
    assert not node_exists(
        overrun_second
    ), f"Node {overrun_second} should not have been created past MaxNodeCount"

    assert (
        node_count() == before_count
    ), "A create past MaxNodeCount should not change the number of nodes"

    for node_name in pre_existing:
        assert_node_intact(node_name)


# The invalid state is rejected before any node table state is touched. This
# pins that early return, which the rollback path must never have to undo.
def test_node_stability_when_create_uses_invalid_state(
    create_node, delete_nodes, unique_node_prefix
):
    """Verify a create rejected for its state leaves pre-existing nodes intact"""

    create_node(
        f"{unique_node_prefix}1",
        "FUTURE",
        intact_feature,
        extra=intact_spec,
        fatal=True,
    )

    rejected_nodes = [f"{unique_node_prefix}2", f"{unique_node_prefix}3"]
    delete_nodes.extend(rejected_nodes)

    err = create_node(
        f"{unique_node_prefix}[2-3]",
        "DOWN",
        colliding_feature,
        extra=colliding_spec,
        xfail=True,
        fatal=True,
    )["stderr"]
    assert (
        "Invalid node state specified" in err
    ), f"scontrol should report an invalid node state, got: {err}"

    for node_name in rejected_nodes:
        assert not node_exists(
            node_name
        ), f"Node {node_name} should not have been created with an invalid state"

    assert_node_intact(f"{unique_node_prefix}1")
