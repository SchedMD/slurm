############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create helpers.conf, helper scripts and reboot script")
    atf.require_config_parameter('NodeFeaturesPlugins', 'node_features/helpers')
    atf.require_nodes(1)
    helper_script = make_helper_script()
    feature_helpers_dict = {}
    for i in range(2):
        feature_helpers_dict[f"f{i+1}"] = {'Helper': helper_script}
    atf.require_config_parameter('Feature', feature_helpers_dict, source='helpers')
    atf.require_config_parameter('RebootProgram', f"{atf.module_tmp_path}/rebooter.sh")
    atf.require_slurm_running()


# Creates the script called by each helper in the helpers.conf
def make_helper_script():

    # Helper variable storage location: 
    helpers_variable_file = f"{atf.module_tmp_path}/helper.vars"

    helper_script = f"{atf.module_tmp_path}/helper.sh"
    atf.make_bash_script(helper_script, f"""
touch {helpers_variable_file}
if [ -n \"$1\" ]; then
    echo $1 > {helpers_variable_file}
fi
cat {helpers_variable_file}"""
    )

    # Set initial active feature as f1 by storing it in the helper variable file
    atf.run_command(f"echo f1 > {helpers_variable_file}", fatal=True, quiet=True)
    atf.run_command(f"chmod 0666 {helpers_variable_file}", user='root', fatal=True, quiet=True)
    return helper_script


# Creates the rebooter script referenced in slurm.conf that kills and restarts the proper slurmd 
def make_rebooter_script(node_name, out_file):
    reboot_file = f"{atf.module_tmp_path}/rebooter.sh"
    slurmd_path = f"{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name}"

    atf.make_bash_script(reboot_file, f"""
slurmd_pid=$(ps -p $PPID -o ppid:1=)
slurmd_start_cmd=$(ps -p $slurmd_pid -o cmd=)
kill -9 $slurmd_pid
while [ $SECONDS -lt 10 ]; do
    if ! ps -p $slurmd_pid; then
        break
    fi
done
$($slurmd_start_cmd -b)
while [ $SECONDS -lt 10 ]; do
    if ps -p $(pgrep -f '{slurmd_path}'); then
        echo 'done' > {out_file}
        break
    fi
done
""")
    
    atf.run_command(f"chmod 0777 {reboot_file}", user='root', fatal=True, quiet=True)


@pytest.fixture(scope="module")
def our_node():
    # Determine our node name and wait until the node is up and at 'idle' state
    node_name = list(atf.nodes.keys())[0]
    atf.repeat_command_until(f"sinfo -h -n {node_name} -o %t", lambda results: re.search(r'idle', results['stdout']))
    return node_name


def test_plugin_features(our_node):
    """Verify plugin is enabled and ActiveFeatures != AvailableFeatures"""

    active_feats = atf.get_node_parameter(our_node, 'ActiveFeatures')
    avail_feats = atf.get_node_parameter(our_node, 'AvailableFeatures')
    assert active_feats != avail_feats, "AvailableFeatures should not equal ActiveFeatures"


def test_request_adds_new_ActiveFeature(our_node):
    """Verify job request with new ActiveFeature restarts, adds new ActiveFeature, and runs"""
    
    out_file = f"{atf.module_tmp_path}/file.out"
    make_rebooter_script(our_node, out_file)
    
    # Submit a job with inactive available feature 'f2', should trigger reboot
    atf.submit_job(f"--wrap='true' -C f2 -w {our_node}", fatal=True)
    
    # Wait for output from rebooter script that indicates a successful reboot
    atf.repeat_command_until(f"cat {out_file}", lambda results: re.search(r'done', results['stdout']), fatal=True, timeout=30)

    assert atf.repeat_until(lambda : atf.get_node_parameter(our_node, 'ActiveFeatures'), lambda f: f == 'f2', timeout=30, fatal=True), "f2 is not included in ActiveFeatures"
