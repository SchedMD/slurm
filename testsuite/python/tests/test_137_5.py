############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test job step constraints with changeable features."""

import os

import pytest

import atf

# Constants
HELPER_FEATURES = ["help_a", "help_b", "help_c"]
CONFIG_FEATURES = ["conf_a", "conf_b"]

# Test constraints with their associated helper features that should be tested in steps
CONSTRAINT_TESTS = (
    (("help_b", "conf_b"), ["help_b"]),
    (("help_b",), ["help_b"]),
    (("help_a", "conf_b", "conf_a"), ["help_a"]),
    (("help_a", "conf_a", "conf_b"), ["help_a"]),
)

# Job options, step constraint, and whether the step may start. A step
# constraint is looked up as a single feature name, unless it is identical to
# the expression the job was allocated with (its --prefer value when that was
# satisfied, otherwise its constraint), in which case it is ignored. No job
# requests help_c, so it is never active.
STEP_CASES = (
    ("-C 'help_a&conf_b'", "help_a&conf_b", True),
    ("-C 'help_a&conf_b'", "conf_b&help_a", False),
    ("-C 'help_a&conf_b'", "(help_a&conf_b)", False),
    ("-C 'help_a&conf_b'", "help_c", False),
    ("-C 'conf_b'", "conf_a", True),
    ("-C 'conf_b'", "help_a&conf_b", False),
    ("-C 'conf_b'", "conf_a|conf_b", False),
    ("-C 'conf_a&conf_b' --prefer 'help_a&conf_b'", "help_a&conf_b", True),
    ("-C 'conf_a&conf_b' --prefer 'help_a&conf_b'", "conf_a&conf_b", False),
)

# List to track feature nodes
feature_nodes = []


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    """Set up the test environment including nodes with features and helper scripts"""
    global feature_nodes

    # --------------------------- CREATE SLURM.CONF ---------------------------
    atf.require_config_parameter_includes(
        "NodeFeaturesPlugins", "node_features/helpers"
    )
    # Rebootless features never run it, but 26.05.0-26.05.5 need one set to
    # change them (Ticket 25619)
    atf.require_config_parameter("RebootProgram", "/bin/true")

    # Create enough nodes for all constraint combinations to run in parallel
    needed_nodes = len(CONSTRAINT_TESTS)
    feature_nodes = atf.require_nodes(
        needed_nodes, [("Features", ",".join(CONFIG_FEATURES))]
    )[:needed_nodes]

    # -------------------------- CREATE HELPERS.CONF --------------------------
    helper_node_entries = {}

    # Create helper entries and scripts for all nodes
    for node in feature_nodes:
        helper_script = os.path.abspath(f"{node}_helper.sh")
        feature_file = os.path.abspath(f"{node}_feature")
        helper_node_entries[node] = {
            "Feature": ",".join(HELPER_FEATURES),
            "Helper": helper_script,
            "Flags": "rebootless",
        }

        # Make the helper script for each node
        atf.make_bash_script(
            helper_script,
            f"""
            if [[ -n "$1" ]]; then
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

    atf.require_slurm_running()


@pytest.mark.parametrize("command", ["salloc", "sbatch"])
def test_step_single_feature(command):
    """Test that job steps are allowed only one feature at a time

    This test verifies that:
    1. Jobs can run with multiple features specified in constraints
    2. Individual steps within those jobs can each use a helper feature
    3. All steps complete successfully
    """
    # Reset all feature nodes at start of test
    atf.run_command(
        f"scontrol update NodeName={','.join(feature_nodes)} ActiveFeatures=conf_a,conf_b",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    jobs = []

    # Submit all jobs in parallel, each on its own node
    for (constraint_tuple, helper_features), assigned_node in zip(
        CONSTRAINT_TESTS, feature_nodes
    ):
        file_name = os.path.abspath(f"step_{'_'.join(constraint_tuple)}")

        # Create script with steps for each helper feature
        script_contents = ""
        for helper_feat in helper_features:
            script_contents += (
                f"\nsrun -C {helper_feat} echo 'ran' >> {file_name}.output"
            )
        atf.make_bash_script(f"{file_name}.sh", script_contents)

        # Submit job with all steps
        job_id = atf.submit_job(
            command,
            f"-w {assigned_node} -C '{'&'.join(constraint_tuple)}'",
            file_name + ".sh",
            wrap_job=False,
            fatal=True,
            background=True,
        )

        jobs.append(
            (job_id, constraint_tuple, helper_features, file_name, assigned_node)
        )

    # Wait for all jobs to complete
    for job_id, constraint_tuple, _, _, _ in jobs:
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Verify results for all completed jobs
    for job_id, constraint_tuple, helper_features, file_name, expected_node in jobs:
        # Verify job completed successfully
        assert (
            atf.get_job_parameter(job_id, "JobState") == "COMPLETED"
        ), f"Job {job_id} did not complete successfully with constraint: {'&'.join(constraint_tuple)}"

        # Verify job ran on assigned node
        node = atf.get_job_parameter(job_id, "NodeList")
        assert (
            node == expected_node
        ), f"Job {job_id} ran on node {node} instead of assigned node {expected_node}"

        # Verify each job step ran successfully
        atf.assert_file_contents(
            f"{file_name}.output", "\n".join(["ran"] * len(helper_features))
        )


@pytest.mark.parametrize("job_options,step_constraint,allowed", STEP_CASES)
def test_step_constraint(job_options, step_constraint, allowed):
    """Test which step constraints a job step accepts"""
    result = atf.run_command(
        f"salloc -w {feature_nodes[0]} {job_options} "
        f"srun -C '{step_constraint}' true",
        xfail=not allowed,
    )
    if allowed:
        assert (
            result["exit_code"] == 0
        ), f"Step constraint '{step_constraint}' should start in a job with {job_options}: {result['stderr']}"
    else:
        assert (
            "Unable to create step" in result["stderr"]
            and "Invalid feature specification" in result["stderr"]
        ), f"Step constraint '{step_constraint}' should be refused in a job with {job_options}"
