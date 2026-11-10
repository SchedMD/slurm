############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test helpers constraint operators and the feature sets they select."""

import os

import pytest

import atf

# Lists containing groups of features
HELPER_FEATURES = ["help_a", "help_b", "help_c"]
CONFIG_FEATURES = ["conf_a", "conf_b", "conf_c"]

# Pairs of mutually exclusive features
MUTUALLY_EXCLUSIVE_GROUPS = tuple(zip(HELPER_FEATURES, CONFIG_FEATURES))

# Constraint, extra sbatch options, and whether the controller accepts it.
# Brackets and counts are only rejected when a changeable feature is named;
# nested parentheses are rejected for any feature.
SYNTAX_CASES = (
    ("help_a*1", "", False),
    ("conf_a*1", "", True),
    ("[help_a|help_b]", "", False),
    ("[conf_a|conf_b]", "", True),
    ("[conf_a|conf_b]&help_c", "", False),
    ("[help_a*1&conf_b*1]", "-N2", False),
    ("[conf_a*1&conf_b*1]", "-N2", True),
    ("((help_a|help_b)&conf_c)", "", False),
    ("((conf_a|conf_b)&conf_c)", "", False),
    ("(help_a|help_b)&conf_c", "", True),
)

# Global constraint tuples used to determine number of nodes needed
constraint_result_tuples = (
    ("conf_c|(conf_b)&(conf_c)&help_a", ["conf_c"]),
    ("conf_c|(help_c)|uniq_a&conf_a", ["conf_c"]),
    ("help_b&help_c&(uniq_a|help_c)", ["help_c", "help_b"]),
    ("(help_c&uniq_a)|(help_c&help_b)", ["help_c", "help_b"]),
    ("(conf_b)&(conf_c)&conf_c|(help_a)", ["conf_c", "conf_b"]),
    ("(conf_b)&conf_c|(conf_c)|(help_a)", ["conf_c", "conf_b"]),
    ("(conf_b)&(conf_c&help_a)&conf_c", ["help_a", "conf_c", "conf_b"]),
    ("(conf_b&help_a)&(conf_c&conf_c)", ["help_a", "conf_c", "conf_b"]),
)

# Changeable features for checking which nodes are changed. The static feature
# lets scontrol set ActiveFeatures with no changeable feature active.
CHANGE_FEATURES = ["chg_a", "chg_b", "chg_c"]
CHANGE_STATIC = "chg_static"

# Constraint, changeable features active on each node before the job, and the
# features each node's helper is asked to set. A node is changed only when it
# lacks a requested feature, and is then sent every requested feature.
SELECTION_CASES = (
    ("chg_a&chg_b", (["chg_a", "chg_b"],), ([],)),
    ("chg_a&chg_b", (["chg_a", "chg_b", "chg_c"],), ([],)),
    ("chg_a&chg_b", (["chg_a"],), (["chg_a", "chg_b"],)),
    ("chg_a&chg_b", (["chg_b"],), (["chg_a", "chg_b"],)),
    ("chg_a&chg_b&chg_c", (["chg_b"],), (["chg_a", "chg_b", "chg_c"],)),
    ("chg_a&chg_b", (["chg_a", "chg_b"], ["chg_b"]), ([], ["chg_a", "chg_b"])),
    ("chg_a&chg_b", (["chg_a", "chg_b"], ["chg_a", "chg_b"]), ([], [])),
    ("chg_a&chg_b", (["chg_b"], ["chg_a", "chg_b"]), (["chg_a", "chg_b"], [])),
    ("chg_a&chg_b", ([], ["chg_a", "chg_b"]), (["chg_a", "chg_b"], [])),
    (
        "chg_a&chg_b",
        (["chg_a"], ["chg_b"]),
        (["chg_a", "chg_b"], ["chg_a", "chg_b"]),
    ),
)

# List to track feature nodes
feature_nodes = []

# Node whose only feature is uniq_a
uniq_node = None

# Two node sets with different changeable features, for matching OR
set_a_nodes = []
set_b_nodes = []

# Nodes with CHANGE_FEATURES, for checking which nodes are changed
change_nodes = []

# Every feature each node's helper was asked to set
helper_logs = {}

# Changeable features each node's helper reports as active on its next query
feature_files = {}

# Active features of each node right after the setup
initial_active_features = {}


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    """Set up the test environment including nodes with features and helper scripts"""
    global feature_nodes, uniq_node, set_a_nodes, set_b_nodes, change_nodes
    global helper_logs, feature_files, initial_active_features

    # --------------------------- CREATE SLURM.CONF ---------------------------
    atf.require_config_parameter_includes(
        "NodeFeaturesPlugins", "node_features/helpers"
    )
    # Rebootless features never run it, but 26.05.0-26.05.5 need one set to
    # change them (Ticket 25619)
    atf.require_config_parameter("RebootProgram", "/bin/true")

    # Calculate needed nodes based on valid constraints
    needed_nodes = len(constraint_result_tuples)

    # Create multiple nodes with all features
    feature_nodes = atf.require_nodes(
        needed_nodes, [("Features", ",".join(CONFIG_FEATURES))]
    )[:needed_nodes]

    # Have one node with a unique feature
    uniq_node = atf.require_nodes(1, [("Features", "uniq_a")])[0]

    set_a_nodes = atf.require_nodes(2, [("Features", "set_a")])[:2]
    set_b_nodes = atf.require_nodes(2, [("Features", "set_b")])[:2]
    change_nodes = atf.require_nodes(2, [("Features", CHANGE_STATIC)])[:2]
    node_helper_features = {node: HELPER_FEATURES for node in feature_nodes}
    node_helper_features.update({node: ["h1"] for node in set_a_nodes})
    node_helper_features.update({node: ["h4"] for node in set_b_nodes})
    node_helper_features.update({node: CHANGE_FEATURES for node in change_nodes})

    # -------------------------- CREATE HELPERS.CONF --------------------------
    helper_node_entries = {}

    # Create helper entries and scripts for all nodes
    for node, helper_features in node_helper_features.items():
        helper_script = os.path.abspath(f"{node}_helper.sh")
        feature_file = feature_files[node] = os.path.abspath(f"{node}_feature")
        helper_logs[node] = os.path.abspath(f"{node}_helper.log")
        helper_node_entries[node] = {
            "Feature": ",".join(helper_features),
            "Helper": helper_script,
            "Flags": "rebootless",
        }

        # Make the helper script for each node
        atf.make_bash_script(
            helper_script,
            f"""
            if [[ -n "$1" ]]; then
                echo "$1" >> {helper_logs[node]}
                echo "$1" >> {feature_file}
            else
                if [[ -e {feature_file} ]]; then
                    cat {feature_file}
                    echo "" > {feature_file}
                fi
            fi
            """,
        )

    # Add all nodes' entries to helpers.conf
    atf.require_config_parameter("NodeName", helper_node_entries, source="helpers")

    mutually_exclusive_entries = {}

    # Make each "help_" and "conf_" feature mutually exclusive if they end with
    # the same letter
    # Ex: ("help_a", "conf_a"), ("help_b", "conf_b"), etc.
    for help_feat, conf_feat in MUTUALLY_EXCLUSIVE_GROUPS:
        mutually_exclusive_entries[help_feat + "," + conf_feat] = {}

    atf.require_config_parameter(
        "MutuallyExclusive", mutually_exclusive_entries, source="helpers"
    )

    atf.require_slurm_running()

    for node in helper_logs:
        initial_active_features[node] = atf.get_node_parameter(node, "active_features")


@pytest.fixture(autouse=True)
def reset_helpers():
    """Restore the features of the nodes after each test.

    A helper reports the features it set as active, so a node registering
    later would restore them in slurmctld. Clear that before resetting
    ActiveFeatures with scontrol, which only changes slurmctld.
    """
    yield
    atf.run_command(
        f"rm -f {' '.join(helper_logs.values())} {' '.join(feature_files.values())}",
        fatal=True,
    )
    for node, features in initial_active_features.items():
        atf.run_command(
            f"scontrol update NodeName={node} ActiveFeatures={','.join(features)}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )


@pytest.mark.parametrize("constraint,options,accepted", SYNTAX_CASES)
def test_constraint_syntax(constraint, options, accepted):
    """Test which constraint operators are accepted with changeable features"""
    result = atf.run_command(
        f"sbatch -H {options} -C '{constraint}' --wrap true", xfail=not accepted
    )
    if accepted:
        assert (
            result["exit_code"] == 0
        ), f"Constraint '{constraint}' should be accepted: {result['stderr']}"
    else:
        assert (
            "Invalid feature specification" in result["stderr"]
        ), f"Constraint '{constraint}' should be rejected as an invalid feature"


@pytest.mark.parametrize("command", ["salloc", "sbatch", "srun"])
def test_constraint_validity(command):
    """Test that valid job constraints run and activate the expected features

    This test verifies that:
    1. Valid constraints allow jobs to run
    2. Nodes get the correct active features when running jobs
    """
    valid_jobs = []

    # First submit all valid jobs
    for (constraint, expected_result), assigned_node in zip(
        constraint_result_tuples, feature_nodes
    ):
        job_id = atf.submit_job(
            command,
            f"-w {assigned_node} -C '{constraint}'",
            "hostname",
            background=True,
        )
        assert (
            job_id
        ), f"The {command} job didn't submit when it should have with constraint: {constraint}"
        valid_jobs.append((job_id, constraint, expected_result, assigned_node))

    # Wait for all valid jobs to complete
    for job_id, constraint, _, expected_node in valid_jobs:
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Verify results for all completed jobs
    for job_id, constraint, expected_result, expected_node in valid_jobs:
        # Verify job completed successfully
        assert (
            atf.get_job_parameter(job_id, "JobState") == "COMPLETED"
        ), f"Job {job_id} did not complete successfully with constraint: {constraint}"

        # Verify job ran on assigned node
        node = atf.get_job_parameter(job_id, "NodeList")
        assert (
            node == expected_node
        ), f"Job {job_id} ran on node {node} instead of assigned node {expected_node}"

        # Verify active features on the assigned node
        exp_feats = set(expected_result + ["conf_a", "conf_b", "conf_c"])
        features_after = set(atf.get_node_parameter(node, "active_features"))
        assert (
            features_after == exp_feats
        ), f"Node {node} has Active Features {features_after} instead of {exp_feats} after running job with constraint: {constraint}"

        # ActiveFeatures is set by slurmctld, so check the helper was called
        exp_helper = set(expected_result) & set(HELPER_FEATURES)
        helper_set = set(
            atf.run_command_output(f"cat {helper_logs[node]} 2>/dev/null").split()
        )
        assert (
            helper_set == exp_helper
        ), f"Helper on node {node} was asked to set {helper_set} instead of {exp_helper} for constraint: {constraint}"


@pytest.mark.parametrize("constraint,before,expected", SELECTION_CASES)
def test_only_nodes_missing_features_change(constraint, before, expected):
    """Test that only nodes lacking a requested feature are changed"""
    nodes = change_nodes[: len(before)]
    for node, active in zip(nodes, before):
        atf.run_command(
            f"scontrol update NodeName={node} ActiveFeatures={','.join([CHANGE_STATIC] + active)}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    job_id = atf.submit_job_sbatch(
        f"-N{len(nodes)} -w {','.join(nodes)} -C '{constraint}' --wrap true",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    requested = set(constraint.split("&"))
    for node, exp_helper in zip(nodes, expected):
        # ActiveFeatures is set by slurmctld, so the helper log is what shows
        # which nodes were changed. Compare lists to catch repeated calls or
        # features passed together as one argument.
        helper_calls = atf.run_command_output(
            f"cat {helper_logs[node]} 2>/dev/null"
        ).split()
        assert sorted(helper_calls) == sorted(
            exp_helper
        ), f"Helper on node {node} was asked to set {helper_calls} instead of {exp_helper} for constraint: {constraint}"

        active = set(atf.get_node_parameter(node, "active_features"))
        assert (
            requested <= active
        ), f"Node {node} has Active Features {active} without {requested}"


def test_and_before_or_node_eligibility():
    """Test that a node with only the first OR operand runs the job"""
    job_id = atf.submit_job_sbatch(
        f"-w {uniq_node} -C 'uniq_a|help_a&conf_b' --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)


def assert_allocated(job_id, nodes, feature):
    assert sorted(
        atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList"))
    ) == sorted(nodes), f"Job {job_id} should run on {nodes}"
    for node in nodes:
        assert atf.run_command_output(
            f"cat {helper_logs[node]}", fatal=True
        ).split() == [
            feature
        ], f"Helper on node {node} should have been asked to set only {feature}"


def test_matching_or_one_node_set():
    """Test that '|' with changeable features allocates nodes from one set"""
    job_id = atf.submit_job_sbatch("-N2 -C 'h1|h4' --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    assert_allocated(job_id, set_a_nodes, "h1")


@pytest.fixture
def one_set_a_node_drained():
    node = set_a_nodes[1]
    atf.run_command(
        f"scontrol update NodeName={node} State=DRAIN Reason=test_137_4",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.run_command(
        f"scontrol update NodeName={node} State=RESUME",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def test_matching_or_never_mixes_sets(one_set_a_node_drained):
    """Test that '|' uses the other node set rather than mixing the two"""
    job_id = atf.submit_job_sbatch("-N2 -C 'h1|h4' --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    assert_allocated(job_id, set_b_nodes, "h4")


@pytest.mark.parametrize("constraint", ["set_a|h4", "h4|set_a"])
def test_matching_or_static_operand(one_set_a_node_drained, constraint):
    """Test that '|' with one changeable operand never mixes node sets"""
    job_id = atf.submit_job_sbatch(f"-N2 -C '{constraint}' --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    assert_allocated(job_id, set_b_nodes, "h4")


@pytest.mark.xfail(reason="Ticket 15661: --nodelist lets a matching OR mix node sets")
def test_matching_or_nodelist():
    """Test that '|' never runs on a node list that mixes node sets"""
    job_id = atf.submit_job_sbatch(
        f"-N2 -w {set_a_nodes[0]},{set_b_nodes[0]} -C 'h1|h4' --wrap true"
    )
    if job_id:
        for t in atf.timer(xfail=True):
            state = atf.get_job_parameter(job_id, "JobState")
            if state != "PENDING":
                break
        assert state == "PENDING", f"Job {job_id} mixed node sets and became {state}"
