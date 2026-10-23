############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Run with python3: require_nodes must retain every requested GRES token."""

import ast
import re
from pathlib import Path


def test_require_nodes_preserves_all_gres():
    # Load the real helper without ATF's module-level Slurm configuration.
    source = Path(__file__).with_name("atf.py")
    function = next(
        node
        for node in ast.parse(source.read_text()).body
        if isinstance(node, ast.FunctionDef) and node.name == "require_nodes"
    )
    for existing, required in [
        ("gpu:1", "gpu:4,mps:8"),
        ("gpu:4", "gpu:4,mps:8"),
        ("mps:8", "gpu:4,mps:8"),
        ("gpu:chunked:1", "gpu:chunked:4,gpu:other:2"),
        (None, "gpu:4,mps:8"),
    ]:
        template = {"Port": 7001}
        if existing is not None:
            template["Gres"] = existing
        created = []
        namespace = {
            "re": re,
            "get_nodes": lambda **kwargs: {"node1": template},
            "properties": {"auto-config": True},
            "create_node": created.append,
        }
        exec(
            compile(ast.Module(body=[function], type_ignores=[]), source, "exec"),
            namespace,
        )
        namespace["require_nodes"](1, [("Gres", required)])
        assert len(created) == 1, created
        assert created[0]["Gres"] == required, (existing, required, created)


if __name__ == "__main__":
    test_require_nodes_preserves_all_gres()
    print("require_nodes preserves all GRES requirements")
