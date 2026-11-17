##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import cls1, cls2, cls3, run_sinfo_test

pytestmark = pytest.mark.slow


def test_federated_sinfo_output(federation_setup, cleanup):
    """Test federated sinfo output."""
    # Each cluster should appear in federated sinfo
    output = atf.run_command_output("sinfo --noheader", fatal=True)
    for cluster in [cls1, cls2, cls3]:
        assert re.search(
            rf"primary\*\s+{cluster}\s+up\s+infinite", output
        ), f"Cluster {cluster} not found in sinfo output:\n{output}"

    # Test local sinfo (nodes may be in mixed states from running jobs). The
    # nodes of a cluster of a multi-cluster setup have its number as prefix.
    run_sinfo_test("--local", r"primary\*\s+up\s+infinite\s+\d+\s+\S+\s+1-node")

    # Test cluster-specific sinfo
    for number, cluster in enumerate([cls1, cls2, cls3], start=1):
        run_sinfo_test(
            f"-M{cluster}",
            rf"primary\*\s+up\s+infinite\s+\d+\s+\S+\s+{number}-node",
        )
