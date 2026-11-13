############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the hres field of the slurmrestd v0.0.46 node endpoints.

POST /slurm/v0.0.46/node/{node_name} with hres replaces the HRES layers of the
node like scontrol update nodename HRES=: an empty array removes the node from
every layer.
GET /slurm/v0.0.46/node/{node_name} lists the layers in hres, without the
parents of a MODE_3 leaf, and hres is an empty array when the node is in no
layer.
"""

import pathlib

import pytest
import requests

import atf

node = "node1"

resources_yaml = """
- resource: power
  mode: MODE_3
  layers:
    - layer_name: "root"
      count: 100000
    - layer_name: "leaf1"
      parent_name: "root"
      nodes:
        - "node1"
      count: 1000
    - layer_name: "leaf2"
      parent_name: "root"
      nodes:
        - "node2"
      count: 1000
"""

pytestmark = pytest.mark.skipif(
    not pathlib.Path(f"{atf.properties['slurm-sbin-dir']}/slurmrestd").exists(),
    reason="Issue #51190: slurmrestd is not built",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmrestd",
        reason="Issue #51190, #51415: the hres field of the v0.0.46 node endpoints was added in 26.11.",
    )
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    atf.require_nodes(2)
    atf.require_config_file("resources.yaml", resources_yaml)
    atf.require_slurmrestd("slurmctld", "v0.0.46")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def headers(setup):
    """Authenticate as SlurmUser, which may update nodes"""
    slurm_user = atf.properties["slurm-user"]
    token = (
        atf.run_command_output(
            f"scontrol token username={slurm_user} lifespan=600",
            user=slurm_user,
            fatal=True,
        )
        .strip()
        .split("=", 1)[1]
    )
    return {"X-SLURM-USER-NAME": slurm_user, "X-SLURM-USER-TOKEN": token}


@pytest.fixture(autouse=True)
def reset_node(headers):
    """Start every test with the node in the layers of resources.yaml"""
    yield
    resp = post_hres(headers, [{"hres_name": "power", "layer_name": "leaf1"}])
    if resp.status_code != 200:
        pytest.fail(f"Resetting the HRES of {node} failed: {resp.text}")


def node_url():
    return f"{atf.properties['slurmrestd_url'].rstrip('/')}/slurm/v0.0.46/node/{node}"


def post_hres(headers, value):
    return requests.post(node_url(), headers=headers, json={"hres": value})


def get_hres(headers):
    resp = requests.get(node_url(), headers=headers)
    assert resp.status_code == 200, f"GET {node_url()} should succeed: {resp.text}"
    return resp.json()["nodes"][0]["hres"]


def get_shown_hres_layers(headers):
    """Return the (resource, layer) pairs in hres of GET node."""
    return {(lay["hres_name"], lay["layer_name"]) for lay in get_hres(headers)}


def test_get(headers):
    """GET node lists the leaf of a node, without its parent"""

    assert get_shown_hres_layers(headers) == {
        ("power", "leaf1")
    }, f"GET node should list leaf1 for {node}"


def test_post_replace(headers):
    """POST node with hres replaces the layers"""

    resp = post_hres(headers, [{"hres_name": "power", "layer_name": "leaf2"}])
    assert resp.status_code == 200, f"POST hres should succeed: {resp.text}"
    assert atf.get_node_hres_layers(node) == {
        ("power", "root"),
        ("power", "leaf2"),
    }, f"{node} should be moved to leaf2"
    assert get_shown_hres_layers(headers) == {
        ("power", "leaf2")
    }, f"GET node should list leaf2 for {node}"


def test_post_no_layers(headers):
    """POST node with an empty array removes the node from every layer"""

    resp = post_hres(headers, [])
    assert resp.status_code == 200, f"POST hres [] should succeed: {resp.text}"
    assert atf.get_node_hres_layers(node) == set(), f"{node} should be in no layer"
    assert get_hres(headers) == [], f"GET node should have an empty hres for {node}"


@pytest.mark.parametrize(
    "layers",
    [[{"hres_name": "power", "layer_name": "leaf2"}], []],
    ids=["leaf2", "no_layers"],
)
def test_post_get_value(headers, layers):
    """POST node with the hres of GET node keeps the layers of the node"""

    resp = post_hres(headers, layers)
    assert resp.status_code == 200, f"POST hres should succeed: {resp.text}"
    expected = atf.get_node_hres_layers(node)

    resp = post_hres(headers, get_hres(headers))
    assert resp.status_code == 200, f"POST hres of GET should succeed: {resp.text}"
    assert (
        atf.get_node_hres_layers(node) == expected
    ), f"{node} should keep its layers when the hres of GET node is posted"


@pytest.mark.parametrize(
    "value",
    [
        [{"hres_name": "power", "layer_name": "root"}],
        '[{"hres_name":"power","layer_name":"leaf2"}]',
    ],
    ids=["parent_layer", "json_string"],
)
def test_post_invalid(headers, value):
    """POST node with an invalid hres is rejected and changes nothing"""

    resp = post_hres(headers, value)
    assert resp.status_code != 200, f"POST hres {value!r} should be rejected"
    assert atf.get_node_hres_layers(node) == {
        ("power", "root"),
        ("power", "leaf1"),
    }, f"{node} should keep the layers of resources.yaml"
