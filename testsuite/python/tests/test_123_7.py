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


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(4)
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


@pytest.mark.parametrize(
    "resv_a, resv_b, can_replacement_overlap",
    [
        # Normal vs REPLACE_DOWN - replacement must NOT overlap
        (
            ("resv_a1", ""),
            ("resv_b1", "REPLACE_DOWN"),
            False,
        ),
        # Normal vs REPLACE - replacement must NOT overlap
        (
            ("resv_a2", ""),
            ("resv_b2", "REPLACE"),
            False,
        ),
        # MAINT vs Normal - replacement must NOT overlap
        pytest.param(
            ("resv_a3", "MAINT"),
            ("resv_b3", ""),
            False,
            marks=pytest.mark.xfail(
                atf.get_version() < (25, 11),
                reason="Ticket 23547: Do not select replacement nodes from MAINT reservations",
            ),
        ),
        # MAINT vs REPLACE_DOWN - replacement must NOT overlap
        pytest.param(
            ("resv_a4", "MAINT"),
            ("resv_b4", "REPLACE_DOWN"),
            False,
            marks=pytest.mark.xfail(
                atf.get_version() < (25, 11),
                reason="Ticket 23547: Do not select replacement nodes from MAINT reservations",
            ),
        ),
        # MAINT vs REPLACE - replacement must NOT overlap
        pytest.param(
            ("resv_a5", "MAINT"),
            ("resv_b5", "REPLACE"),
            False,
            marks=pytest.mark.xfail(
                atf.get_version() < (25, 11),
                reason="Ticket 23547: Do not select replacement nodes from MAINT reservations",
            ),
        ),
        # OVERLAP vs Normal - replacement overlap is allowed
        (
            ("resv_a6", "OVERLAP"),
            ("resv_b6", ""),
            True,
        ),
        # OVERLAP vs REPLACE_DOWN - replacement overlap is allowed
        (
            ("resv_a7", "OVERLAP"),
            ("resv_b7", "REPLACE_DOWN"),
            True,
        ),
        # OVERLAP vs REPLACE - replacement overlap is allowed
        (
            ("resv_a8", "OVERLAP"),
            ("resv_b8", "REPLACE"),
            True,
        ),
    ],
)
def test_reservation_replacement_overlap_behavior(
    resv_a, resv_b, can_replacement_overlap
):
    """
    1) Create A and B with names/flags
    2) For B: DOWN one node, wait for replacement.
    3) Enforce replacement-vs-A overlap policy via can_replacement_overlap.
    """
    name_a, flags_a = resv_a
    name_b, flags_b = resv_b
    downed_node = None

    name_a = f"{test_name}_{name_a}"
    name_b = f"{test_name}_{name_b}"

    try:
        # Create partition and get nodes
        part_nodes = _create_partition(part_name, 4)
        resv_a_nodes = ",".join(part_nodes[-2:])

        # Create first reservation
        atf.run_command(
            f"scontrol create reservation reservationname={name_a} "
            f"user={atf.properties['test-user']} start=now duration=1 "
            f"Nodes={resv_a_nodes} partition={part_name} "
            f"flags={flags_a}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        # give it some time to populate
        time.sleep(5)

        nodes_a = _nodes_from_res(name_a)

        # log the node reservation state
        atf.run_command(
            "sinfo -l",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        # Create second reservation
        atf.run_command(
            f"scontrol create reservation reservationname={name_b} "
            f"user={atf.properties['test-user']} start=now duration=1 "
            f"nodecnt=2 partition={part_name} "
            f"flags={flags_b}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        # Give it some time to populate
        time.sleep(5)

        nodes_b = _nodes_from_res(name_b)

        # log the node reservation state
        atf.run_command(
            "sinfo -l",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        # Something went wrong, there are no assigned nodes
        assert nodes_b, f"{name_b}: no nodes to down"
        downed_node = next(iter(nodes_b))

        # DOWN a node in B and wait for replacement
        new_nodes_b, replacements = _down_one_node_and_wait_for_replacement(
            name_b, nodes_b, downed_node, timeout_s=5, poll_interval_s=2.0
        )

        # If we are allowed to overlap, but no replacement happened
        if not replacements and can_replacement_overlap:
            pytest.fail(
                f"{name_b}: no replacement detected after DOWNing {downed_node}. "
                f"Old={sorted(nodes_b)} New={sorted(new_nodes_b)}"
            )

        # Fail on overlap when not allowed
        replacement_overlap = bool(replacements & nodes_a)
        if not can_replacement_overlap and replacement_overlap:
            pytest.fail(
                f"Replacement for {name_b} must not overlap {name_a} but did: "
                f"replacements={sorted(replacements)}, A={sorted(nodes_a)}"
            )

    finally:
        for resv in (name_b, name_a):
            try:
                atf.run_command(
                    f"scontrol delete reservationname={resv}",
                    user=atf.properties["slurm-user"],
                )
            except Exception:
                pass
        if downed_node:
            _bring_node_up(downed_node)
        _delete_partition(part_name)
