############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""The REST node record's drained_tres field lists what is drained."""

import pytest
import requests

import atf

pytestmark = pytest.mark.slow

NODE = "node1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmrestd",
        reason="Ticket 25180: drained_tres added to the node record in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: TRES drain added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_slurmrestd("slurmctld", "v0.0.46")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def resume_cpu():
    yield
    atf.update_tres_drain(NODE, "cpu", "Index=ALL", action="Resume", quiet=True)


def test_node_record_lists_drained_tres():
    """GET of the node returns one drained_tres record for a drained CPU."""
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="rest", fatal=True)
    assert atf.wait_for_node_state(NODE, "DEGRADED"), atf.get_node_detail(NODE)

    response = atf.request_slurmrestd(f"slurm/v0.0.46/node/{NODE}")
    assert response.status_code == 200, response.text[:500]
    body = response.json()
    assert not body.get("errors"), body
    nodes = body.get("nodes") or []
    assert len(nodes) == 1, body
    drained = nodes[0].get("drained_tres") or []
    assert len(drained) == 1, drained
    rec = drained[0]
    assert rec.get("tres_name") == "cpu", rec
    assert rec.get("index") == "0", rec
    assert rec.get("reason") == "rest", rec


def _post_tres(body):
    """POST /tres/ as SlurmUser. The RPC requires an administrator."""
    token = (
        atf.run_command_output(
            "scontrol token lifespan=600",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        .replace("SLURM_JWT=", "")
        .strip()
    )
    return requests.post(
        f"{atf.properties['slurmrestd_url']}/slurm/v0.0.46/tres/",
        headers={
            "X-SLURM-USER-NAME": atf.properties["slurm-user"],
            "X-SLURM-USER-TOKEN": token,
        },
        json=body,
    )


def test_post_drains_and_resumes_tres():
    """POST /tres/ drains a CPU and a later POST resumes it."""
    response = _post_tres(
        {
            "name": "cpu",
            "nodes": [NODE],
            "index": "0",
            "action": "drain",
            "reason": "rest-post",
        }
    )
    assert response.status_code == 200, response.text[:500]
    assert not response.json().get("errors"), response.text[:500]
    assert atf.wait_for_node_state(NODE, "DEGRADED"), atf.get_node_detail(NODE)
    assert "rest-post" in (atf.get_node_tres_drained(NODE) or ""), atf.get_node_detail(
        NODE
    )

    response = _post_tres(
        {"name": "cpu", "nodes": [NODE], "index": "0", "action": "resume"}
    )
    assert response.status_code == 200, response.text[:500]
    assert not response.json().get("errors"), response.text[:500]
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained is None:
            break


@pytest.mark.parametrize(
    "body",
    [
        {"name": "cpu", "nodes": [NODE], "action": "resume", "reason": "nope"},
        {
            "name": "cpu",
            "nodes": [NODE],
            "index": "foo",
            "action": "drain",
            "reason": "x",
        },
        {
            "name": "cpu",
            "nodes": [NODE],
            "select_all": True,
            "count": 1,
            "action": "drain",
            "reason": "both",
        },
        {"name": "cpu", "index": "0", "action": "drain", "reason": "nonode"},
        {
            "name": "cpu",
            "nodes": ["no-such-node"],
            "index": "0",
            "action": "drain",
            "reason": "missing",
        },
        {
            "name": "cpu",
            "nodes": [NODE, "no-such-node"],
            "index": "0",
            "action": "drain",
            "reason": "partial",
        },
    ],
)
def test_post_rejects_a_tres_update_the_controller_will_not_apply(body):
    """A POST the CLI would not build still has to be refused by slurmctld."""
    response = _post_tres(body)
    assert response.status_code >= 400, response.text[:500]
    assert response.json().get("errors"), response.text[:500]
    # A refused request must not apply to the nodes that did resolve.
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)


def test_post_set_drain_without_a_selector_changes_nothing():
    """A set_drain POST that names no TRES is refused, and nothing is resumed.

    slurmrestd hands the request to slurmctld without the checks scontrol
    makes, so the controller has to refuse it. Read as the empty set, it
    would resume every drained CPU on the node.
    """
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="kept", fatal=True)
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE)
        if drained == "cpu(IDX:0)(kept)":
            break

    response = _post_tres(
        {"name": "cpu", "nodes": [NODE], "action": "set_drain", "reason": "none"}
    )
    assert response.status_code >= 400, response.text[:500]
    assert response.json().get("errors"), response.text[:500]
    assert atf.get_node_tres_drained(NODE) == "cpu(IDX:0)(kept)", atf.get_node_detail(
        NODE
    )


def test_tres_path_rejects_get():
    """The TRES path accepts POST only."""
    response = requests.get(
        f"{atf.properties['slurmrestd_url']}/slurm/v0.0.46/tres/",
        headers=atf.properties["slurmrestd-headers"],
    )
    assert response.status_code >= 400, response.text[:500]


def test_node_record_omits_max_drained_tres_when_unset():
    """GET omits max_drained_tres when the node has no limit."""
    response = atf.request_slurmrestd(f"slurm/v0.0.46/node/{NODE}")
    assert response.status_code == 200, response.text[:500]
    node = response.json()["nodes"][0]
    assert not node.get("max_drained_tres"), node


@pytest.fixture
def max_drained_tres_cpu():
    atf.set_node_parameter(NODE, "MaxDrainedTres", "cpu:1")
    yield
    atf.set_node_parameter(NODE, "MaxDrainedTres", None)


def test_node_record_reports_max_drained_tres(max_drained_tres_cpu):
    """GET reports max_drained_tres when the node has a limit."""
    response = atf.request_slurmrestd(f"slurm/v0.0.46/node/{NODE}")
    assert response.status_code == 200, response.text[:500]
    node = response.json()["nodes"][0]
    assert node.get("max_drained_tres") == "cpu:1", node
