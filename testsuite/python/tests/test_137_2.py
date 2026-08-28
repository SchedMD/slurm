############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test rebootless helpers features change without RebootProgram.

Ticket 25619: with no RebootProgram, slurmctld never asked slurmd to activate a
rebootless feature, so the node was left powering up, the job never ran, and
the node was set DOWN at ResumeTimeout.
"""

import os

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup(vars_file):
    atf.require_config_parameter_includes(
        "NodeFeaturesPlugins", "node_features/helpers"
    )
    # The premise of this test: the feature change must work with no reboot
    # program at all. Enforce it rather than relying on the default config.
    atf.require_config_parameter("RebootProgram", None)
    helper_script = make_helper_script(vars_file)
    feature_helpers_dict = {
        "foo=on": {"Helper": helper_script, "Flags": "rebootless"},
        "foo=off": {"Helper": helper_script, "Flags": "rebootless"},
    }
    atf.require_config_parameter("Feature", feature_helpers_dict, source="helpers")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def vars_file():
    # Holds the node's active feature, initially foo=off. The helper rewrites it
    # whenever slurmd sets a feature.
    path = os.path.abspath("helper.vars")
    atf.run_command(f"echo foo=off > {path}", fatal=True, quiet=True)
    atf.run_command(f"chmod 0666 {path}", user="root", fatal=True, quiet=True)
    return path


# Creates the script called by each helper in the helpers.conf
def make_helper_script(vars_file):
    helper_script = os.path.abspath("helper.sh")
    atf.make_bash_script(
        helper_script,
        f"""
touch {vars_file}
if [ -n \"$1\" ]; then
    echo $1 > {vars_file}
fi
cat {vars_file}""",
    )
    return helper_script


@pytest.fixture(scope="module")
def our_node():
    node_name = list(atf.nodes.keys())[0]
    # Match the state exactly. IDLE with NOT_RESPONDING, POWERED_DOWN or
    # POWERING_UP ('idle*', 'idle~' and 'idle#' in sinfo) cannot run the job
    # these tests submit.
    atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)
    return node_name


@pytest.mark.xfail(
    (26, 5) <= atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25619: rebootless features were not activated without a"
    " RebootProgram",
)
def test_rebootless_feature_change(our_node, vars_file):
    """Verify a rebootless feature change is applied without RebootProgram"""

    # Submit a job requesting the inactive rebootless feature 'foo=on'. The
    # controller issues a reboot RPC to change the feature; because the
    # feature is rebootless it does so without a RebootProgram configured,
    # and slurmd applies the feature without rebooting.
    job_id = atf.submit_job_sbatch(f"--wrap='true' -C foo=on -w {our_node}", fatal=True)

    # Assert on the helper vars file rather than on ActiveFeatures.
    # slurmctld sets ActiveFeatures optimistically when it schedules the job,
    # before the reboot RPC is sent, so ActiveFeatures reaching foo=on says
    # nothing about whether the node ever got the request. Only slurmd runs
    # the helper, so this file changing is what proves it did.
    assert atf.repeat_command_until(
        f"cat {vars_file}",
        lambda results: results["stdout"].strip() == "foo=on",
        timeout=60,
    ), "The helper never set foo=on, so the feature change never reached slurmd"

    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), "Job never completed, the node was left waiting on a boot"

    node_state = atf.get_node_parameter(our_node, "state")
    assert "POWERING_UP" not in node_state, "Node was left powering up"
    assert "DOWN" not in node_state, "Node should not have been downed"

    assert atf.get_node_parameter(our_node, "active_features") == [
        "foo=on"
    ], "foo=on is not included in ActiveFeatures"
