##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import re

import pytest

import atf

from .conftest import cls1, cls2, cls3, fed_name

pytestmark = pytest.mark.slow


def test_federation_database_setup(federation_setup):
    """Test federation database setup and cluster features."""
    output = atf.run_command_output(
        f"sacctmgr show federation {fed_name} format=federation%20",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        re.search(rf"\s+{fed_name}\s*$", output, re.MULTILINE) is not None
    ), f"Federation {fed_name} not found"

    output = atf.run_command_output(
        f'sacctmgr show cluster {cls1},{cls2},{cls3} format="cluster%20,federation%20,features%20"',
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        re.search(rf"\s+{cls1}\s+{fed_name}\s+fa\s*$", output, re.MULTILINE) is not None
    )
    assert (
        re.search(rf"\s+{cls2}\s+{fed_name}\s+fb\s*$", output, re.MULTILINE) is not None
    )
    assert (
        re.search(rf"\s+{cls3}\s+{fed_name}\s+fc\s*$", output, re.MULTILINE) is not None
    )
