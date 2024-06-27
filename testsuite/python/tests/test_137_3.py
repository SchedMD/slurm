############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test helpers MutuallyExclusive features and RebootProgram arguments."""

import os

import pytest

import atf

# Constants
HELPER_FEATURES = ["help_a", "help_b", "help_c", "help_d", "help_e"]
CONFIG_FEATURES = ["conf_a", "conf_b", "conf_c"]

# Each ORs two mutually exclusive features
OR_CONSTRAINTS = (
    "help_a|conf_a",
    "conf_a|help_a",
    "(help_a)|(conf_a)",
    "(conf_a)|(help_a)",
    "(help_a)|conf_a",
    "(conf_a)|help_a",
    "help_a|(conf_a)",
    "conf_a|(help_a)",
    "(help_a|conf_a)",
    "(conf_a|help_a)",
    "help_b|conf_b",
    "conf_b|help_b",
    "(help_b)|(conf_b)",
    "(conf_b)|(help_b)",
    "(help_b)|conf_b",
    "(conf_b)|help_b",
    "help_b|(conf_b)",
    "conf_b|(help_b)",
    "(help_b|conf_b)",
    "(conf_b|help_b)",
    "help_c|conf_c",
    "conf_c|help_c",
    "(help_c)|(conf_c)",
    "(conf_c)|(help_c)",
    "(help_c)|conf_c",
    "(conf_c)|help_c",
    "help_c|(conf_c)",
    "conf_c|(help_c)",
    "(help_c|conf_c)",
    "(conf_c|help_c)",
)

# The same constraints AND-ing the mutually exclusive features
AND_CONSTRAINTS = [c.replace("|", "&") for c in OR_CONSTRAINTS]

# Constraint and whether it is accepted, with changeable help_d and help_e
# mutually exclusive. One expanded feature set holding both rejects the job.
HELPER_EXCLUSIVE_CASES = (
    ("help_d&help_e", False),
    ("help_d|help_e", True),
    ("(help_d|help_a)&(help_e|help_b)", False),
    ("help_d&help_e|help_a", False),
    ("help_d|help_e&help_a", True),
)

# Node with changeable features
feature_node = None


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup(reboot_params):
    global feature_node

    # --------------------------- CREATE SLURM.CONF ---------------------------

    atf.require_config_parameter_includes(
        "NodeFeaturesPlugins", "node_features/helpers"
    )
    # Make DOWN nodes available for use upon registration of rebooted node
    atf.require_config_parameter("ReturnToService", 2)

    # Assure nodes that have all features
    feature_node = atf.require_nodes(1, [("Features", ",".join(CONFIG_FEATURES))])[0]

    # -------------------------- CREATE HELPERS.CONF --------------------------

    helper_node_entries = {}

    helper_script = os.path.abspath("helper.sh")
    feature_file = os.path.abspath("feature")

    # Create node's entry to be added to helpers.conf
    helper_node_entries[feature_node] = {
        "Feature": ",".join(HELPER_FEATURES),
        "Helper": helper_script,
    }

    # Make the helper script for the node
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

    # Add each node's helpers.conf entry to helpers.conf
    atf.require_config_parameter("NodeName", helper_node_entries, source="helpers")

    mutually_exclusive_entries = {}

    # Make each "help_" and "conf_" feature mutually exclusive if they end with
    # the same letter
    # Ex: ("help_a", "conf_a"), ("help_b", "conf_b"), etc.
    for help_feat, conf_feat in zip(HELPER_FEATURES, CONFIG_FEATURES):
        mutually_exclusive_entries[help_feat + "," + conf_feat] = {}
    mutually_exclusive_entries["help_d,help_e"] = {}

    atf.require_config_parameter(
        "MutuallyExclusive", mutually_exclusive_entries, source="helpers"
    )

    # Record the features RebootProgram gets, then restart slurmd as rebooted
    rebooter = os.path.abspath("rebooter.sh")
    atf.make_bash_script(
        rebooter,
        f"""
echo "$1" > {reboot_params}
slurmd_pid=$(<"{atf.get_run_dir_path()}/slurmd.{feature_node}.pid")
slurmd_start_cmd=$(ps -p $slurmd_pid -o cmd=)
kill $slurmd_pid
($slurmd_start_cmd -b)
""",
    )
    atf.require_config_parameter("RebootProgram", rebooter)

    atf.require_slurm_running()

    # Without the helper features every rejection below would pass trivially
    all_features = set(HELPER_FEATURES + CONFIG_FEATURES)
    for t in atf.timer(fatal=True):
        if set(atf.get_node_parameter(feature_node, "features")) == all_features:
            break


@pytest.fixture(scope="module")
def reboot_params():
    return os.path.abspath("reboot_params.txt")


@pytest.mark.parametrize("constraint", AND_CONSTRAINTS)
@pytest.mark.parametrize("command", ["salloc", "srun", "sbatch"])
def test_mutually_exclusive_rejection(command, constraint):
    """Test mutually exclusive feature combinations with '&' are rejected."""
    job = "--wrap hostname" if command == "sbatch" else "hostname"
    result = atf.run_command(f"{command} --constraint='{constraint}' {job}", xfail=True)
    assert (
        "Invalid feature specification" in result["stderr"]
    ), f"{command} should reject mutually exclusive constraint '{constraint}'"


@pytest.mark.parametrize("constraint", OR_CONSTRAINTS)
def test_mutually_exclusive_acception(constraint):
    """Test mutually exclusive feature combinations with '|' are accepted."""
    job_id = atf.submit_job_sbatch(f"-H --constraint='{constraint}' --wrap true")
    assert job_id != 0, f"Job with constraint '{constraint}' should be accepted"


@pytest.mark.parametrize("constraint,accepted", HELPER_EXCLUSIVE_CASES)
def test_helper_mutually_exclusive(constraint, accepted):
    """Test mutually exclusive changeable features across expanded feature sets"""
    result = atf.run_command(
        f"sbatch -H --constraint='{constraint}' --wrap true", xfail=not accepted
    )
    if accepted:
        assert (
            result["exit_code"] == 0
        ), f"Constraint '{constraint}' should be accepted: {result['stderr']}"
    else:
        assert (
            "Invalid feature specification" in result["stderr"]
        ), f"Constraint '{constraint}' should be rejected as mutually exclusive"


@pytest.mark.parametrize(
    "features",
    [
        "help_a",
        "help_b,help_c",
        "help_c,help_b",
        "help_a,help_b,help_c",
        "help_c,help_a,help_b",
        "help_c,help_b,help_a",
    ],
)
def test_reboot_script_args(reboot_params, features):
    """Test reboot script receives correct feature arguments"""
    # Start the node with no helper features active
    reset_active_features(reboot_params)

    # Submit job with features that'll force node to reboot
    job_id = atf.submit_job_sbatch(
        f"--constraint='{features.replace(',', '&')}' -t1 --wrap 'srun -t1 hostname'",
        fatal=True,
    )

    # The job completes only once the rebooted node has registered
    atf.wait_for_job_state(job_id, "COMPLETED", timeout=60, fatal=True)

    # Assert reboot script received expected parameters
    output = atf.run_command_output(f"cat {reboot_params}", fatal=True)
    assert set(output.strip().split(",")) == set(
        features.split(",")
    ), f"RebootProgram got features '{output.strip()}' instead of '{features}'"


def reset_active_features(reboot_params):
    """Reset the active features of the helper node with scontrol"""
    new_active_features = CONFIG_FEATURES

    # Update the ActiveFeatures of the helper node using scontrol
    atf.run_command(
        f"scontrol update NodeName={feature_node} ActiveFeatures={','.join(new_active_features)}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Wait while the node's active features don't match the new active features
    for t in atf.timer(fatal=True):
        if set(atf.get_node_parameter(feature_node, "active_features")) == set(
            new_active_features
        ):
            break

    # Clean any reboot file due to this reset
    atf.run_command(f"rm -f {reboot_params}", fatal=True)
