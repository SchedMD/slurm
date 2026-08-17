############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify sinfo row grouping.

sinfo collapses nodes that share every displayed field into a single row.
Which fields participate depends on the format, on --exact, and on -N/-R, so
these tests drive each of those paths through the documented command line
rather than through any one output format.
"""

import os
import re

import pytest

import atf

test_name = os.path.splitext(os.path.basename(__file__))[0]
extra_part_name = f"{test_name}_partition"

# A weight no node is expected to be configured with, used to make nodes
# differ in an exact-match-only field
new_weight = 1234

# A list of the nodes used in the test
node_list = []
node_range = ""


# Setup for all tests
@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_list, node_range

    atf.require_nodes(8)
    atf.require_slurm_running()

    # Get a list of 8 idle nodes to be used in the tests. run_job_nodes
    # silently returns [] if srun succeeds but its stderr doesn't match the
    # expected pattern, so check it explicitly rather than letting every
    # test in the module fail later with a confusing empty node_range.
    node_list = atf.run_job_nodes("-N8 true", fatal=True)
    assert len(node_list) == 8, f"Expected 8 allocated nodes, got {node_list}"
    node_range = atf.node_list_to_range(node_list)


@pytest.fixture(scope="function", autouse=True)
def reset_node_states():
    """Wait for all nodes to be idle before each test; resume them after."""
    atf.wait_for_node_state(node_list, "IDLE", operator="==", fatal=True)
    yield
    # Not fatal: scontrol rejects RESUME for a node that is already IDLE,
    # so this returns non-zero for every test that left the
    # nodes alone, and for the untouched nodes of the tests that did not.
    # The nodes that do need resuming are still resumed. The next test's
    # wait_for_node_state() above is what actually enforces a clean state.
    atf.run_command(
        f"scontrol update nodename='{node_range}' state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _sinfo_rows(command, expected_fields):
    """Run an sinfo command and return its rows as lists of fields.

    A row with an unexpected number of fields fails an assertion instead of
    raising ValueError from tuple unpacking, so malformed output (an empty
    %v on an unregistered node, for example) gives a readable failure.
    """
    output = atf.run_command_output(command, fatal=True)
    rows = []
    for line in output.strip().splitlines():
        fields = line.split()
        if not fields:
            continue
        assert (
            len(fields) == expected_fields
        ), f"Expected {expected_fields} fields in sinfo row, got {line!r}"
        rows.append(fields)
    return rows


def _group_rows(node_range_expr):
    """Run non-(-N) sinfo grouped by NodeList/State.

    Returns one (state, set of nodes) tuple per output row. A list rather than
    a dict keyed on state, so that rows sharing a state stay visible to the
    row count assertions instead of overwriting each other.
    """
    return [
        (state, set(atf.node_range_to_list(hostlist)))
        for hostlist, state in _sinfo_rows(
            f"sinfo -h -n '{node_range_expr}' -o '%N %t'", 2
        )
    ]


def _partitions_by_node():
    """Return {node: [partitions]} for the test nodes.

    Queried through scontrol rather than sinfo, so tests that count one row
    per partition are not validating sinfo against itself.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    return {node: list(nodes_dict[node].get("partitions", [])) for node in node_list}


def test_group_identical_nodes():
    """Test that sinfo collapses nodes sharing displayed fields into one row."""
    down_nodes = node_list[:4]
    idle_nodes = node_list[4:]

    atf.run_command(
        f"scontrol update nodename={','.join(down_nodes)} state=down reason=test",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    rows = _group_rows(node_range)

    # Only two (NodeList, State) rows should be produced: one for the down
    # nodes and one for the still-idle nodes.
    assert len(rows) == 2, f"Expected 2 grouped rows, got {rows}"

    down_rows = [nodes for state, nodes in rows if "down" in state]
    idle_rows = [nodes for state, nodes in rows if "idle" in state]
    assert len(down_rows) == 1, f"Expected exactly one down row, got {rows}"
    assert len(idle_rows) == 1, f"Expected exactly one idle row, got {rows}"

    assert down_rows[0] == set(
        down_nodes
    ), "Down row should contain exactly the down nodes"
    assert idle_rows[0] == set(
        idle_nodes
    ), "Idle row should contain exactly the idle nodes"


def test_group_split_on_difference():
    """Test that sinfo keeps nodes differing in a displayed field on separate rows."""
    down_nodes = node_list[:2]
    drain_nodes = node_list[2:4]
    idle_nodes = node_list[4:]

    atf.run_command(
        f"scontrol update nodename={','.join(down_nodes)} state=down reason=test",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update nodename={','.join(drain_nodes)} state=drain reason=test",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    rows = _group_rows(node_range)

    # down, drain, and idle nodes must never be merged into the same row.
    assert len(rows) == 3, f"Expected 3 grouped rows, got {rows}"

    down_rows = [nodes for state, nodes in rows if "down" in state]
    drain_rows = [nodes for state, nodes in rows if "drain" in state]
    idle_rows = [
        nodes for state, nodes in rows if "down" not in state and "drain" not in state
    ]
    assert len(down_rows) == 1, f"Expected exactly one down row, got {rows}"
    assert len(drain_rows) == 1, f"Expected exactly one drain row, got {rows}"
    assert len(idle_rows) == 1, f"Expected exactly one idle row, got {rows}"

    assert down_rows[0] == set(
        down_nodes
    ), "Down row should contain exactly the down nodes"
    assert drain_rows[0] == set(
        drain_nodes
    ), "Drain row should contain exactly the drained nodes"
    assert idle_rows[0] == set(
        idle_nodes
    ), "Idle row should contain exactly the idle nodes"


def test_undisplayed_state_does_not_split():
    """Test that a field left out of the format does not split rows.

    doc/man/man1/sinfo.1 documents that only fields included in the output
    format are compared. State is the field users most expect to split rows,
    so down some nodes and leave %t out: every node must still share a row.
    """
    atf.run_command(
        f"scontrol update nodename={','.join(node_list[:4])} state=down reason=test",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N'", 1)

    assert (
        len(rows) == 1
    ), f"Expected an undisplayed state not to split rows, got {rows}"
    assert set(atf.node_range_to_list(rows[0][0])) == set(
        node_list
    ), "The single row should contain every node"


def test_node_flag_one_row_per_node_partition_pair():
    """Test that sinfo -N still emits one row per node-partition pair even
    when every node shares identical displayed fields (all idle, no state
    differences)."""
    partitions = _partitions_by_node()
    # -N emits one row per (partition, node) pair, so the expected row count
    # is the total partition membership of the test nodes, not len(node_list).
    expected = sum(len(parts) for parts in partitions.values())

    # No -a, so sinfo sees the same partitions _partitions_by_node() does:
    # scontrol omits hidden and access-restricted ones for this user too.
    rows = _sinfo_rows(f"sinfo -N -h -o '%N' -n '{node_range}'", 1)
    nodes = [row[0] for row in rows]

    assert (
        len(nodes) == expected
    ), f"Expected one row per (partition, node) pair ({expected}), got {nodes}"
    assert set(nodes) == set(node_list), "sinfo -N should list every node"


def test_node_flag_default_format():
    """Test that sinfo -N with its own default format gives one row per node
    and partition, each reporting a single node.

    doc/man/man1/sinfo.1 documents the -N default format as
    "%#N %.6D %#P %6t", so every row carries NODELIST, NODES, PARTITION and
    STATE, and -N's one-line-per-node-partition rule makes NODES always 1.
    """
    partitions = _partitions_by_node()
    expected = sum(len(parts) for parts in partitions.values())

    rows = _sinfo_rows(f"sinfo -Nh -n '{node_range}'", 4)

    assert (
        len(rows) == expected
    ), f"Expected one row per (partition, node) pair ({expected}), got {rows}"

    for row in rows:
        assert row[1] == "1", f"Each -N row should report a single node, got {rows}"

    assert {row[0] for row in rows} == set(node_list), "sinfo -N should list every node"


@pytest.mark.skipif(
    atf.get_version("bin/sinfo") < (26, 11, 0),
    reason="Ticket 25633: sinfo --exact grouped by node version incorrectly before 26.11.0",
)
def test_group_by_version():
    """Test that sinfo --exact groups nodes sharing the same %v (Version) into
    one row.

    Regression test: nodes reporting the exact same version string used to
    never be grouped under --exact; each node got its own row even though
    every displayed field, including %v, was identical.

    Known limitation: every test node reports the same slurmd version, so
    this can only confirm that same-version nodes still merge under
    --exact; it cannot exercise the case where differing versions must
    split rows, since the test cluster has no way to give a node a
    different reported version.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    versions = {nodes_dict[node]["version"] for node in node_list}

    rows = _sinfo_rows(f"sinfo --exact -h -n '{node_range}' -o '%N %v'", 2)

    assert len(rows) == len(versions), (
        f"Expected one row per distinct reported version ({len(versions)}), "
        f"got {len(rows)} rows: {rows}"
    )

    assert {version for _, version in rows} == versions, (
        f"Expected the rows to report every node version ({versions}), "
        f"got {[version for _, version in rows]}"
    )

    grouped_nodes = [
        node for hostlist, _ in rows for node in atf.node_range_to_list(hostlist)
    ]
    assert sorted(grouped_nodes) == sorted(
        node_list
    ), "Every node should appear in exactly one version row"


def test_group_by_version_non_exact():
    """Test that %v reports one node's version for a grouped row when
    --exact is not used.

    doc/man/man1/sinfo.1 documents that, for a merged row spanning nodes that
    were never split by version (i.e. without --exact), %v prints the version
    of one of the nodes in the list rather than splitting rows on version
    like test_group_by_version does under --exact.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    versions = {nodes_dict[node]["version"] for node in node_list}

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N %v'", 2)

    # Without --exact, version differences don't split rows, so all 8 nodes
    # should still collapse into a single grouped row.
    assert len(rows) == 1, (
        "Expected all nodes to group into a single row without --exact, "
        f"got {len(rows)} rows: {rows}"
    )

    hostlist, version = rows[0]
    assert set(atf.node_range_to_list(hostlist)) == set(
        node_list
    ), "Grouped row should contain every node"

    assert version in versions, (
        f"Expected the merged row's version ({version!r}) to be one reported "
        f"by a grouped node ({versions})"
    )


def _node_ports():
    """Return {node: port} for the test nodes.

    Queried through scontrol for the same reason as _partitions_by_node().
    Unlike the version, weight and memory fields, the ports already differ
    from node to node on any cluster, so no fixture is needed to make the
    nodes disagree.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    return {node: nodes_dict[node]["port"] for node in node_list}


def test_port_grouping_non_exact():
    """Test that Port neither splits rows nor gains a "+" without --exact.

    doc/man/man1/sinfo.1 lists port among the fields compared only when
    --exact is set, and singles it out (with slurmd version) as having no
    min/max indicator: the value from one of the grouped nodes is shown
    instead. So nodes differing only in port must collapse into a single row
    whose port is one node's actual port, printed bare.
    """
    ports = set(_node_ports().values())

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -O NodeList,Port", 2)

    assert (
        len(rows) == 1
    ), f"Expected port to be ignored without --exact, got {len(rows)} rows: {rows}"

    hostlist, port = rows[0]
    assert set(atf.node_range_to_list(hostlist)) == set(
        node_list
    ), "The single non-exact row should contain every node"

    # Port has no "min+" or "min-max" rendering, so anything but bare digits
    # means the grouped row is aggregating a field that should not be.
    assert (
        port.isdigit()
    ), f"Expected the grouped row's port to be printed bare, got {port!r}"

    assert (
        int(port) in ports
    ), f"Expected the grouped row's port ({port}) to be one of {sorted(ports)}"


def test_port_grouping_exact():
    """Test that Port splits rows under --exact.

    The other half of what doc/man/man1/sinfo.1 documents for port: once
    --exact is set, nodes are only grouped when their ports match. Every
    node has its own port, so this is expected to give one row per node.

    Spelled -e rather than --exact, so the short form is exercised
    somewhere in the module.
    """
    node_ports = _node_ports()
    expected = len(set(node_ports.values()))

    rows = _sinfo_rows(f"sinfo -e -h -n '{node_range}' -O NodeList,Port", 2)

    assert (
        len(rows) == expected
    ), f"Expected --exact to give one row per distinct port ({expected}), got {rows}"

    grouped_nodes = []
    for hostlist, port in rows:
        row_nodes = atf.node_range_to_list(hostlist)
        grouped_nodes.extend(row_nodes)
        assert {node_ports[node] for node in row_nodes} == {int(port)}, (
            f"Every node on an --exact row should have the row's port ({port}), "
            f"got {[(node, node_ports[node]) for node in row_nodes]}"
        )

    assert sorted(grouped_nodes) == sorted(
        node_list
    ), "Every node should appear in exactly one port row"


def test_exact_ignores_undisplayed_fields():
    """Test that --exact compares only the fields that are displayed.

    Port is one of the fields --exact adds to the comparison, and ports
    already differ from node to node. Leaving port out of the format must
    keep --exact from splitting on it.
    """
    assert len(set(_node_ports().values())) > 1, "Test needs nodes whose ports differ"

    rows = _sinfo_rows(f"sinfo -e -h -n '{node_range}' -o '%N'", 1)

    assert (
        len(rows) == 1
    ), f"Expected --exact to ignore the undisplayed port, got {rows}"
    assert set(atf.node_range_to_list(rows[0][0])) == set(
        node_list
    ), "The single row should contain every node"


def _node_field_values(field):
    """Return {node: value} for a numeric node field, read through scontrol.

    Some fields come back as a data_parser number, {"set", "infinite",
    "number"}, rather than as a bare int.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    values = {}
    for node in node_list:
        value = nodes_dict[node].get(field)
        if isinstance(value, dict):
            value = value.get("number") if value.get("set") else None
        values[node] = value
    return values


def test_free_mem_renders_as_range():
    """Test the documented "minimum-maximum" form of free memory.

    doc/man/man1/sinfo.1 documents that without --exact free memory and CPU
    load are not compared, and are shown as one value or a "minimum-maximum"
    range rather than with the "+" the other numeric fields use. Only that
    half is checked: free memory is a live measurement that resamples per
    node, so an --exact row count would be racy.

    CPU load is not checked. slurmd reports the host's load average, so nodes
    sharing a host, as on most test clusters, nearly always report the same
    value and never produce a range.
    """
    # A "+" can only appear when the grouped nodes disagree: when they agree,
    # every form prints the bare value.
    if len(set(_node_field_values("free_mem").values())) < 2:
        pytest.skip(
            "Every node reports the same free_mem, so the range form of %e "
            "cannot be exercised"
        )

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N %e'", 2)

    assert len(rows) == 1, f"Expected %e to be ignored without --exact, got {rows}"
    value = rows[0][1]
    assert re.fullmatch(
        r"(\d+|N/A)(-(\d+|N/A))?", value
    ), f"Expected %e as one value or a minimum-maximum range, got {value!r}"


@pytest.fixture(scope="function")
def mixed_weights():
    """Give node_list[:4] a distinct weight, restoring the original weights.

    Yields {node: weight} as it stands during the test.
    """
    nodes_dict = atf.get_nodes(quiet=True)
    original = {node: nodes_dict[node]["weight"] for node in node_list}
    changed_nodes = node_list[:4]

    atf.run_command(
        f"scontrol update nodename={','.join(changed_nodes)} weight={new_weight}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield {
        node: (new_weight if node in changed_nodes else weight)
        for node, weight in original.items()
    }

    # One command per distinct original weight, since the nodes are not
    # required to have started out with the same one.
    for weight in {original[node] for node in changed_nodes}:
        restore = [node for node in changed_nodes if original[node] == weight]
        atf.run_command(
            f"scontrol update nodename={','.join(restore)} weight={weight}",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )


def test_weight_grouping_requires_exact(mixed_weights):
    """Test that %w (Weight) only splits rows under --exact.

    doc/man/man1/sinfo.1 lists weight among the fields compared only when
    --exact is set, and documents that those fields are otherwise shown as
    the minimum value followed by "+". So without --exact, nodes differing
    only in weight must still collapse into a single row reporting "min+",
    and with --exact they must split into one row per distinct weight.
    """
    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N %w'", 2)
    assert len(rows) == 1, f"Expected weight to be ignored without --exact, got {rows}"
    hostlist, weight = rows[0]
    assert set(atf.node_range_to_list(hostlist)) == set(
        node_list
    ), "The single non-exact row should contain every node"

    # %w renders a merged row as "min+" (or bare "min" if every node shares
    # the same weight), so confirm the collapsed row actually reflects both
    # the original and new_weight rather than silently picking one node's
    # value.
    min_weight = min(mixed_weights.values())
    max_weight = max(mixed_weights.values())
    expected_weight = str(min_weight) if min_weight == max_weight else f"{min_weight}+"
    assert weight == expected_weight, (
        "Non-exact grouped row should report the min/max weight across all "
        f"nodes ({expected_weight}), got {weight}"
    )

    rows = _sinfo_rows(f"sinfo --exact -h -n '{node_range}' -o '%N %w'", 2)
    expected = len(set(mixed_weights.values()))
    assert (
        len(rows) == expected
    ), f"Expected --exact to give one row per distinct weight ({expected}), got {rows}"

    grouped_nodes = [
        node for hostlist, _ in rows for node in atf.node_range_to_list(hostlist)
    ]
    assert sorted(grouped_nodes) == sorted(
        node_list
    ), "Every node should appear in exactly one weight row"


# (slurm.conf parameter, sinfo JSON node field, sinfo format specifier) for
# fields that only split rows under --exact.
#
# CPUs is deliberately absent: setting it alone can contradict a node's
# Sockets/CoresPerSocket/ThreadsPerCore, and slurmctld reconciles that by
# overwriting CPUs rather than by honoring it, so the test would assert
# against a value the node never had.
#
# The reverse is harmless and is why Sockets/CoresPerSocket/ThreadsPerCore are
# safe to raise here: doing so leaves the configured CPUs matching neither
# Sockets, Sockets*CoresPerSocket, nor the product, so slurmctld logs an error
# and resets CPUs to the product. It keeps running, and since these tests never
# put %c/%C in the format, the CPU count is not compared and cannot affect
# which rows group.
sockets_field = ("Sockets", "sockets", "%X")
exact_config_fields = [
    ("RealMemory", "real_memory", "%m"),
    ("TmpDisk", "temporary_disk", "%d"),
    sockets_field,
    ("CoresPerSocket", "cores", "%Y"),
    ("ThreadsPerCore", "threads", "%Z"),
]


def _min_max_string(values):
    """Render values the way sinfo renders a grouped numeric field.

    As sinfo.1 documents under --exact: the value alone when every grouped
    node agrees, otherwise the minimum followed by a "+".
    """
    low, high = min(values), max(values)
    return str(low) if low == high else f"{low}+"


@pytest.fixture(scope="function")
def mixed_config(request):
    """Give node_list[0] a distinct value for a node config parameter.

    Unlike Weight, these cannot be changed on a live node with "scontrol
    update", so this edits slurm.conf and restarts the daemons via
    atf.set_node_parameter() (requires auto-config mode; the test using this
    fixture is skipped where that isn't available).

    Yields (format specifier, {node: value}) as it stands during the test.
    """
    config_name, node_field, format_spec = request.param

    # Both fields default to a value (RealMemory=1, TmpDisk=0) far below
    # what any real node reports, so halving the configured value -- as an
    # earlier version of this fixture did -- is a no-op on a stock ATF
    # cluster and the test below passes without ever exercising the
    # min+/--exact split it's meant to cover. Raise it instead: this is
    # safe even though it advertises more than the node actually has,
    # because ATF clusters run with SlurmdParameters=config_overrides,
    # which skips slurmd's since-detected-vs-configured validation.
    atf.require_config_parameter_includes("SlurmdParameters", "config_overrides")

    nodes_dict = atf.get_nodes(quiet=True)
    original = {node: nodes_dict[node][node_field] for node in node_list}
    changed_node = node_list[0]
    new_value = original[changed_node] + 1

    # Register the restore before making the edit, not after the yield. The
    # edit rewrites slurm.conf and restarts the daemons, and the checks below
    # can fail; a post-yield restore would then never run, leaving the
    # modified node line in place for every later test in the module.
    def _restore():
        atf.set_node_parameter(changed_node, config_name, str(original[changed_node]))
        atf.wait_for_node_state(node_list, "IDLE", operator="==", fatal=True)

    request.addfinalizer(_restore)

    atf.set_node_parameter(changed_node, config_name, str(new_value))
    atf.wait_for_node_state(node_list, "IDLE", operator="==", fatal=True)

    # Read the values back rather than assuming the edit took effect. slurmctld
    # can legitimately discard a node parameter (Sockets= is ignored when the
    # node line also carries SocketsPerBoard=, for instance), and it only logs
    # when it does, so a computed dict would claim a split that never happened.
    nodes_dict = atf.get_nodes(quiet=True)
    values = {node: nodes_dict[node][node_field] for node in node_list}

    assert values[changed_node] == new_value, (
        f"{config_name}={new_value} on {changed_node} was not honored; "
        f"{node_field} reads {values[changed_node]}"
    )
    assert min(values.values()) != max(values.values()), (
        f"{config_name} fixture failed to make {changed_node} differ from "
        f"the rest of {node_list}: {values}"
    )

    yield format_spec, values


@pytest.mark.parametrize(
    "mixed_config",
    exact_config_fields,
    indirect=True,
    ids=[field[0] for field in exact_config_fields],
)
def test_exact_config_plus_suffix(mixed_config):
    """Test that a config field shows the documented "min+" suffix without
    --exact, and splits into one row per distinct value with --exact.

    doc/man/man1/sinfo.1 documents that without --exact these fields are
    aggregated as the minimum followed by "+" when the grouped nodes differ,
    and that --exact compares them instead, so each row reports one value.
    """
    format_spec, values = mixed_config

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N {format_spec}'", 2)
    assert (
        len(rows) == 1
    ), f"Expected {format_spec} to be ignored without --exact, got {rows}"
    hostlist, value = rows[0]
    assert set(atf.node_range_to_list(hostlist)) == set(
        node_list
    ), "The single non-exact row should contain every node"

    min_value = min(values.values())
    max_value = max(values.values())
    expected_value = str(min_value) if min_value == max_value else f"{min_value}+"
    assert value == expected_value, (
        f"Non-exact grouped row should report the min/max {format_spec} across "
        f"all nodes ({expected_value}), got {value}"
    )

    rows = _sinfo_rows(f"sinfo --exact -h -n '{node_range}' -o '%N {format_spec}'", 2)
    expected = len(set(values.values()))
    assert len(rows) == expected, (
        f"Expected --exact to give one row per distinct {format_spec} value "
        f"({expected}), got {rows}"
    )

    grouped_nodes = [
        node for hostlist, _ in rows for node in atf.node_range_to_list(hostlist)
    ]
    assert sorted(grouped_nodes) == sorted(
        node_list
    ), f"Every node should appear in exactly one {format_spec} row"

    # --exact must stop aggregating as well as split: each row reports the
    # one value its nodes share, bare, with no "+" left over from grouping.
    for hostlist, value in rows:
        row_values = {values[node] for node in atf.node_range_to_list(hostlist)}
        assert (
            len(row_values) == 1
        ), f"An --exact row should hold nodes sharing one {format_spec}, got {row_values}"
        assert value == str(
            row_values.pop()
        ), f"An --exact row should report its {format_spec} bare, got {value!r}"


@pytest.mark.parametrize(
    "mixed_config", [sockets_field], indirect=True, ids=[sockets_field[0]]
)
def test_exact_sct_composite(mixed_config):
    """Test that %z applies the "min+" rule to each of its three parts.

    doc/man/man1/sinfo.1 lists the combined socket:core:thread field among
    the fields compared only under --exact. Unlike %X/%Y/%Z it is rendered
    from three independent min/max strings joined by ":", so a group whose
    nodes differ only in sockets reports "S+:C:T" rather than suffixing the
    field as a whole.

    Reuses the Sockets fixture rather than adding a parameter of its own,
    since each mixed_config parameter costs a slurm.conf rewrite and a
    daemon restart on the way in and another on the way out.
    """
    _, sockets = mixed_config

    nodes_dict = atf.get_nodes(quiet=True)
    cores = {node: nodes_dict[node]["cores"] for node in node_list}
    threads = {node: nodes_dict[node]["threads"] for node in node_list}

    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N %z'", 2)
    assert len(rows) == 1, f"Expected %z to be ignored without --exact, got {rows}"

    hostlist, sct = rows[0]
    assert set(atf.node_range_to_list(hostlist)) == set(
        node_list
    ), "The single non-exact row should contain every node"

    # Only the sockets part should carry a "+", since the fixture leaves
    # cores and threads alone. Asserting the whole field catches a
    # regression that suffixes the composite instead of its parts.
    expected_sct = ":".join(
        _min_max_string(part.values()) for part in (sockets, cores, threads)
    )
    assert (
        sct == expected_sct
    ), f"Non-exact grouped row should report %z as {expected_sct}, got {sct}"

    rows = _sinfo_rows(f"sinfo --exact -h -n '{node_range}' -o '%N %z'", 2)
    expected = len({(sockets[node], cores[node], threads[node]) for node in node_list})
    assert len(rows) == expected, (
        f"Expected --exact to give one row per distinct S:C:T ({expected}), "
        f"got {rows}"
    )

    grouped_nodes = [
        node for hostlist, _ in rows for node in atf.node_range_to_list(hostlist)
    ]
    assert sorted(grouped_nodes) == sorted(
        node_list
    ), "Every node should appear in exactly one %z row"


@pytest.fixture(scope="function")
def extra_partition():
    """Create a partition holding node_list[0], removing it after the test."""
    atf.run_command(
        f"scontrol create partitionname={extra_part_name} nodes={node_list[0]}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield extra_part_name
    atf.run_command(
        f"scontrol delete partitionname={extra_part_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


@pytest.fixture(scope="function")
def node0_partitions(extra_partition):
    """Add node_list[0] to a second partition, then remove the partition.

    Yields every partition node_list[0] belongs to while the test runs, so
    tests can expect one row per partition without assuming the cluster only
    had a single one to begin with.
    """
    partitions = _partitions_by_node()[node_list[0]]
    assert extra_part_name in partitions, (
        f"{node_list[0]} should belong to {extra_part_name}, "
        f"it only belongs to {partitions}"
    )
    assert len(partitions) > 1, (
        f"{node_list[0]} should belong to more than one partition, " f"got {partitions}"
    )

    yield partitions


def test_group_merges_across_partitions(node0_partitions):
    """Test that a node reached through two partitions collapses into one row
    when no partition field is displayed.

    sinfo groups rows by their displayed fields. Since no partition field is
    requested here, a node's partition membership does not distinguish rows,
    so a node in two partitions must still produce a single row rather than
    one per partition.
    """
    rows = _sinfo_rows(f"sinfo -h -n '{node_list[0]}' -o '%N %t'", 2)

    assert len(rows) == 1, (
        "Expected one row for a node in multiple partitions when no partition "
        f"field is displayed, got {rows}"
    )
    hostlist, _ = rows[0]
    nodes = atf.node_range_to_list(hostlist)
    assert nodes == [
        node_list[0]
    ], f"Row should hold exactly one entry for {node_list[0]}, got {nodes}"


def test_node_count_matches_nodelist(node0_partitions):
    """Test that a merged row's node count matches its node list.

    A node reached through several partitions merges into one row when no
    partition field is displayed. It must be counted once, not once for each
    partition it belongs to.
    """
    rows = _sinfo_rows(f"sinfo -h -n '{node_range}' -o '%N %D'", 2)

    for hostlist, count in rows:
        assert int(count) == len(
            atf.node_range_to_list(hostlist)
        ), f"Row node count {count} should match its node list {hostlist}"

    grouped_nodes = [
        node for hostlist, _ in rows for node in atf.node_range_to_list(hostlist)
    ]
    assert sorted(grouped_nodes) == sorted(
        node_list
    ), "Every node should appear in exactly one row"


@pytest.mark.skipif(
    atf.get_version("bin/sinfo") < (26, 11, 0),
    reason="Ticket 25633: sinfo -R emitted a placeholder row per partition "
    "before 26.11.0, so a node in multiple partitions produced more than "
    "one -R row",
)
def test_list_reasons_ignores_partition(node0_partitions):
    """Test that sinfo -R groups by reason/node data only, ignoring partition.

    -R's default output format has no partition field, so a node reachable
    through two different partitions must still produce a single -R row
    instead of one row per partition, even when a partition field is added
    to the format and used to filter the query.
    """
    atf.run_command(
        f"scontrol update nodename={node_list[0]} state=drain reason=test",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # %P is added here so the format does include a partition field, to
    # confirm ignoring partition holds even then. -p keeps unrelated
    # partitions from adding empty rows, and the reason is a single word so
    # the rows have three fields.
    rows = _sinfo_rows(
        f"sinfo -Rh -n '{node_list[0]}' -p {','.join(node0_partitions)} -o '%P %E %N'",
        3,
    )

    assert len(rows) == 1, (
        "Expected a single -R row for a node in "
        f"{len(node0_partitions)} partitions with the same reason, got {rows}"
    )
    assert (
        rows[0][2] == node_list[0]
    ), f"The -R row should be for {node_list[0]}, got {rows}"


def _drain_with_reason(nodes, reason, state="drain"):
    """Put nodes into a down/drain state with a shared reason.

    One scontrol command for the whole set on purpose: update_node() stamps
    every node in a single call with the same reason_time, so nodes given a
    reason together also share the timestamp that -R's default format
    compares. Splitting the set across commands could land them on different
    seconds and split rows for a reason the caller did not intend.
    """
    atf.run_command(
        f"scontrol update nodename={','.join(nodes)} state={state} reason={reason}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_list_reasons_groups_by_reason():
    """Test that -R merges nodes sharing a reason and splits differing ones.

    doc/man/man1/sinfo.1 documents that -R displays the reason field and the
    list of nodes with that reason. So a set of nodes drained for one reason
    must collapse to a single row carrying every one of them, and a second
    set drained for a different reason must appear as its own row.
    """
    reason_a_nodes = node_list[:4]
    reason_b_nodes = node_list[4:6]

    # Single-word reasons keep each row at two whitespace-separated fields.
    _drain_with_reason(reason_a_nodes, "reasonA")
    _drain_with_reason(reason_b_nodes, "reasonB")

    rows = _sinfo_rows(f"sinfo -Rh -n '{node_range}' -o '%E %N'", 2)

    assert (
        len(rows) == 2
    ), f"Expected one -R row per distinct reason, got {len(rows)} rows: {rows}"

    nodes_by_reason = {
        reason: set(atf.node_range_to_list(hostlist)) for reason, hostlist in rows
    }
    assert nodes_by_reason == {
        "reasonA": set(reason_a_nodes),
        "reasonB": set(reason_b_nodes),
    }, f"Each -R row should hold exactly the nodes sharing its reason, got {rows}"


def test_list_reasons_splits_by_state():
    """Test that a state field splits -R rows that share a reason.

    -R's own default format has no state field, so nodes sharing a reason
    merge regardless of whether they are down or drained. Adding a state
    field to the format -- which is what -Rl does, via its default format's
    statecompact column -- must split them.

    Spelled with an explicit -o rather than -Rl because -Rl's default format
    also compares the timestamp, and the two states have to be set by two
    scontrol commands, which can land on different seconds. The rows would
    then split whether or not state was ever compared.
    """
    drained_node, down_node = node_list[0], node_list[1]
    reason = "sharedreason"

    _drain_with_reason([drained_node], reason)
    _drain_with_reason([down_node], reason, state="down")

    rows = _sinfo_rows(f"sinfo -Rh -n '{node_range}' -o '%E %t %N'", 3)

    assert len(rows) == 2, (
        "Expected nodes sharing a reason to split on state, got "
        f"{len(rows)} rows: {rows}"
    )

    assert {row[0] for row in rows} == {
        reason
    }, f"Both -R rows should carry the shared reason {reason!r}, got {rows}"

    nodes_by_state = {state: hostlist for _, state, hostlist in rows}
    assert nodes_by_state == {
        "drain": drained_node,
        "down": down_node,
    }, f"Expected one drain row and one down row, got {rows}"


def test_node_flag_separates_by_partition(node0_partitions):
    """Test that sinfo -N gives one row per (partition, node) pair.

    doc/man/man1/sinfo.1 documents that -N prints one line per node and
    partition: if a node belongs to more than one partition, one line for
    each node-partition pair is shown.
    """
    # No partition field in the format, so it's -N's own node/partition
    # pairing, not a displayed partition column, that separates these rows.
    rows = _sinfo_rows(f"sinfo -Nh -n '{node_list[0]}' -o '%N'", 1)

    assert len(rows) == len(node0_partitions), (
        f"Expected {len(node0_partitions)} rows (one per partition) for "
        f"{node_list[0]}, got {rows}"
    )
    assert {row[0] for row in rows} == {
        node_list[0]
    }, f"Every row should be for {node_list[0]}, got {rows}"

    # And each of those rows belongs to a different partition.
    rows = _sinfo_rows(f"sinfo -Nh -n '{node_list[0]}' -o '%N %P'", 2)
    partitions = {partition.rstrip("*") for _, partition in rows}
    assert partitions == set(
        node0_partitions
    ), f"Expected one row per partition in {node0_partitions}, got {rows}"


def test_node_flag_narrows_with_partition(node0_partitions):
    """Test that sinfo -N combined with --partition narrows back to a single
    row per node.

    doc/man/man1/sinfo.1 documents that -N normally emits one line per
    node-partition pair, but that if --partition is also given, only one
    line per node in that partition is shown. Restricting the query to a
    single partition here should leave exactly one row for the node.
    """
    single_partition = node0_partitions[0]

    rows = _sinfo_rows(
        f"sinfo -Nh -n '{node_list[0]}' -p {single_partition} -o '%N'", 1
    )

    assert len(rows) == 1, (
        f"Expected a single row for {node_list[0]} when -N is narrowed to "
        f"partition {single_partition}, got {rows}"
    )
    assert (
        rows[0][0] == node_list[0]
    ), f"The single row should be for {node_list[0]}, got {rows}"


def test_default_grouping_separates_by_partition(node0_partitions):
    """Test that default (non -N) sinfo emits a separate row per partition
    for a node that belongs to more than one, instead of merging them."""
    # -p limits the output to the partitions this node is in, so partitions
    # left empty by the -n filter do not add placeholder rows.
    rows = _sinfo_rows(
        f"sinfo -h -n '{node_list[0]}' -p {','.join(node0_partitions)} -o '%N %P'", 2
    )

    assert len(rows) == len(node0_partitions), (
        f"Expected {len(node0_partitions)} rows (one per partition) for "
        f"{node_list[0]}, got {rows}"
    )

    partitions = {partition.rstrip("*") for _, partition in rows}
    assert partitions == set(
        node0_partitions
    ), f"Expected one row per partition in {node0_partitions}, got {rows}"
    for hostlist, _ in rows:
        assert atf.node_range_to_list(hostlist) == [
            node_list[0]
        ], f"Each row should hold only {node_list[0]}, got {rows}"


def test_empty_partition_still_shown(extra_partition):
    """Test that a partition with zero matching nodes still gets its own row.

    Even when a --state filter excludes every node in a partition, sinfo
    still lists that partition with a node count of 0 rather than omitting
    it, as long as -N is not used and the partition column is displayed.

    Ticket 25633 rewrote how these zero-node partition rows are emitted, so
    this pins the behavior even though sinfo.1 does not describe it.
    """
    # The partition's only node is idle, so --state=down leaves it with none.
    rows = _sinfo_rows(f"sinfo -h -p {extra_part_name} -o '%P %D' --state=down", 2)

    assert len(rows) == 1, f"Expected a single empty-partition row, got {rows}"
    partition, node_count = rows[0]
    assert (
        partition.rstrip("*") == extra_part_name
    ), f"Expected the {extra_part_name} row, got {rows}"
    assert node_count == "0", f"Expected 0 nodes in {extra_part_name}, got {rows}"
