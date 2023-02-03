############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_constraint_invalid_feature_1():
    """Verify --constraint option fails for invalid feature"""

    requested_features = 'invalid'
    exit_code = atf.run_command_exit(f"srun --constraint={requested_features} true", xfail=True)
    assert exit_code != 0, "srun ran when --constraint was invalid"


def test_constraint_invalid_feature_2():
    """Verify --constraint option fails when one of two features invalid"""

    requested_features = 'valid,invalid'
    available_features = 'valid'
    nodes = atf.get_nodes()
    node_name = list(nodes.keys())[0]
    atf.set_node_parameter(node_name, 'Features', available_features)
    exit_code = atf.run_command_exit(f"srun --constraint={requested_features} true", xfail=True)
    assert exit_code != 0, "srun ran when only 1 --constraint was valid"


def test_constraint_valid_feature_1():
    """Verify --constraint option runs when requesting valid sole feature"""

    requested_features = 'spicy'
    available_features = 'spicy'
    nodes = atf.get_nodes()
    node_name = list(nodes.keys())[0]
    atf.set_node_parameter(node_name, 'Features', available_features)
    exit_code = atf.run_command_exit(f"srun --constraint={requested_features} true")
    assert exit_code == 0, "srun did not run when --constraint was valid"


def test_constraint_valid_feature_2():
    """Verify --constraint option runs when requesting both of 2 valid features"""

    requested_features = 'tart,spicy'
    available_features = 'tart,spicy'
    nodes = atf.get_nodes()
    node_name = list(nodes.keys())[0]
    atf.set_node_parameter(node_name, 'Features', available_features)
    exit_code = atf.run_command_exit(f"srun --constraint={requested_features} true")
    assert exit_code == 0, "srun did not run when --constraint was valid with multiple constraints"


def test_constraint_valid_feature_3():
    """Verify --constraint option runs when 1 out of 2 valid features"""

    requested_features = 'tart'
    available_features = 'tart,spicy'
    nodes = atf.get_nodes()
    node_name = list(nodes.keys())[0]
    atf.set_node_parameter(node_name, 'Features', available_features)
    exit_code = atf.run_command_exit(f"srun --constraint={requested_features} true")
    assert exit_code == 0, "srun did not run when --constraint was valid with one constraint on a node with multiple constraints"
