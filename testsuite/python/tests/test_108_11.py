############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def restore_features():
    saved = {n: atf.get_node_parameter(n, "features", default=[]) for n in atf.nodes}
    yield
    for node, feats in saved.items():
        feat_str = ",".join(feats) if feats else ""
        atf.run_command(
            f"scontrol update nodename={node} availablefeatures={feat_str}",
            user=atf.properties["slurm-user"],
        )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5),
    reason="Ticket 25121: scontrol update nodename=ALL availablefeatures=... "
    "returned 'Invalid argument' before 26.05.",
)
def test_update_avail_features_all():
    """Verify NodeName=ALL applies AvailableFeatures to every node."""
    feature = "feat"

    result = atf.run_command(
        f"scontrol update nodename=ALL availablefeatures={feature}",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, (
        f"scontrol update nodename=ALL availablefeatures={feature} failed: "
        f"{result['stderr']}"
    )

    for node in atf.nodes:
        assert atf.get_node_parameter(node, "features") == [
            feature
        ], f"Node {node} did not receive AvailableFeatures={feature}"
