############################################################################
# Copyright (C) SchedMD LLC.
############################################################################

import os
import atf
import pytest
import re
import time

test_name = os.path.splitext(os.path.basename(__file__))[0]
part_name = f"{test_name}_partition"
feature_list = ["f1", "f2", "f3"]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(6)
    atf.require_slurm_running()


def _extract_nodenames(s: str, max_count: int | None = None) -> list[str]:
    names: list[str] = []
    for v in re.compile(r"^\s*NodeName=([^\s]+)", re.M).findall(s):
        v = v.strip()
        names.append(v)
    return names


def _create_partition(part_name: str = "res_part", count: int = 4) -> list[str]:
    out = atf.run_command_output("scontrol show nodes")
    picked = _extract_nodenames(out, max_count=count)

    nodelist = ",".join(picked)
    atf.run_command(
        f"scontrol create partitionname={part_name} Nodes={nodelist}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return picked


def _delete_partition(part_name):
    atf.run_command(
        f"scontrol delete partitionname={part_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _extract_features(s: str) -> str | None:
    m = re.search(r"(?:ActiveFeatures)=([^\s]+)", s)
    if not m:
        return None
    v = m.group(1)
    if v.lower() in ("(null)", "none"):
        return None
    return v


def _get_features(node):
    out = atf.run_command_output(f"scontrol show node {node}")
    return _extract_features(out) or []


def _clear_res_features(nodes):
    for i, node in enumerate(nodes):
        atf.run_command(
            f"scontrol update nodename={node} "
            f"availablefeatures= "
            f"activefeatures= ",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


def _extract_nodes_raw(s: str) -> str | None:
    m = re.search(r"(?:Nodes)=([^\s]+)", s)
    if not m:
        return None
    v = m.group(1)
    if v.lower() in ("(null)", "none"):
        return None
    return v


def _expand_hostlist(hostlist: str) -> set[str]:
    if not hostlist:
        return set()
    out = atf.run_command_output(f"scontrol show hostnames {hostlist}")
    return {ln.strip() for ln in out.splitlines() if ln.strip()}


def _nodes_from_res(resv_name: str) -> set[str]:
    show = atf.run_command_output(f"scontrol show res {resv_name}")
    return _expand_hostlist(_extract_nodes_raw(show))


def _down_one_node_and_wait_for_replacement(
    resv_name: str,
    current_nodes: set[str],
    target: str,
    timeout_s: int = 60,
    poll_interval_s: float = 2.0,
):
    """
    DOWN one node from the reservation and wait for a change in the set.
    Returns (new_nodes_set, replacements_set).
    """
    atf.run_command(
        f"scontrol update nodename={target} state=DOWN reason=HOLD",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    deadline = time.time() + timeout_s
    while time.time() < deadline:
        time.sleep(poll_interval_s)
        updated = _nodes_from_res(resv_name)
        if updated != current_nodes:
            return updated, (updated - current_nodes)

    print(
        f"{resv_name}: node set did not change after DOWNing {target} within {timeout_s}s; "
        f"still: {sorted(_nodes_from_res(resv_name))} - this may be OK"
    )
    return current_nodes, set()


def _bring_node_up(node: str):
    try:
        atf.run_command(
            f"scontrol update nodename={node} state=RESUME",
            user=atf.properties["slurm-user"],
        )
    except Exception:
        pass


def _check_nodes_for_features(name, nodes):
    if len(nodes) != len(feature_list):
        pytest.fail(
            f"{name} Did not allocate correct number of nodes. "
            f"Expected={len(feature_list)} Nodes={sorted(nodes)}"
        )

    # Pre-init counts so all features are present even if 0
    counts = {f: 0 for f in feature_list}

    for n in nodes:
        features = _get_features(n)
        matched = [f for f in feature_list if f in features]
        if len(matched) != 1:
            pytest.fail(
                f"{name} Node {n} matched {matched} " f"features (expected exactly 1)."
            )
        counts[matched[0]] += 1

    missing = [f for f, c in counts.items() if c == 0]
    extra = [f for f, c in counts.items() if c > 1]
    if missing or extra:
        pytest.fail(
            f"{name} Feature assignment wrong. "
            f"Missing={missing} Extra={extra} Counts={counts}"
        )


def test_reservation_by_feature_and_replace():
    """
    This only tests the simple case where the reservation expects
    1 node for each feature, and the one node that DOWNs is
    expected to be replaced with a node with the same feature.

    1) Assign features to nodes from partition
    2) Create a reservation by features and node count
    3) Check features/nodes allocation is as expected
    4) DOWN one node, wait for replacement
    5) Check features/nodes allocation is as expected
    """
    downed_node = None

    try:
        # Create partition and get nodes
        part_nodes = _create_partition(part_name, 4)

        # Assign features to pairs of nodes
        f_len = len(feature_list)
        for i, node in enumerate(part_nodes):
            atf.run_command(
                f"scontrol update nodename={node} "
                f"availablefeatures={feature_list[i % f_len]} "
                f"activefeatures={feature_list[i % f_len]} ",
                user=atf.properties["slurm-user"],
                fatal=True,
            )

        # Create reservation
        res_name = f"{test_name}_res"
        atf.run_command(
            f"scontrol create reservation reservationname={res_name} "
            f"user={atf.properties['test-user']} start=now duration=1 "
            f"partition={part_name} "
            f"features='[{'&'.join(s + '*1' for s in feature_list)}]' "
            f"NodeCnt={f_len} flags=REPLACE_DOWN",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        # give it some time to populate
        time.sleep(5)

        res_nodes = _nodes_from_res(res_name)
        print(res_nodes)

        # log the node reservation state
        atf.run_command(
            "sinfo -l",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        # Something went wrong, there are no assigned nodes
        assert res_nodes, f"{res_name}: no nodes assigned"

        ## Verify allocated nodes for each feature
        _check_nodes_for_features(res_name, res_nodes)

        # Get node to down
        downed_node = next(iter(res_nodes))

        # DOWN a node and wait for replacement
        new_nodes, replacements = _down_one_node_and_wait_for_replacement(
            res_name, res_nodes, downed_node, timeout_s=5, poll_interval_s=2.0
        )

        if not replacements:
            pytest.fail(
                f"{res_name}: no replacement detected after DOWNing {downed_node}. "
                f"Old={sorted(res_nodes)} New={sorted(new_nodes)}"
            )

        ## Verify allocated nodes for each feature
        res_nodes = _nodes_from_res(res_name)
        print(res_nodes)

        _check_nodes_for_features(res_name, res_nodes)

    finally:
        try:
            atf.run_command(
                f"scontrol delete reservationname={res_name}",
                user=atf.properties["slurm-user"],
            )
        except Exception:
            pass
        if downed_node:
            _bring_node_up(downed_node)
        _clear_res_features(part_nodes)
        _delete_partition(part_name)
