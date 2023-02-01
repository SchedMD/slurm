############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import pytest
import re
import pexpect


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


# Global variables used in the tests

cluster1 = 'cluster1'
cluster2 = 'cluster2'
cluster3 = 'cluster3'
cluster4 = 'cluster4'
cluster5 = 'cluster5'

federation1 = 'federation1'
federation2 = 'federation2'
federation3 = 'federation3'

max_fed_clusters = 64

cluster_list = []
for i in range(1, max_fed_clusters):
    cluster_list.append(f"cluster{i}")
cluster_string = ",".join(cluster_list)

# Variables and functions to perform subsequent matching
pos = 0
text = ''


# Performs an initial match and prepares to do subsequent matching
def first_match(pattern, initial_string):
    global pos, text

    pos = 0
    text = initial_string

    match = re.search(pattern, text)

    if match is not None:
        pos = match.end()
        return True
    else:
        return False


# Looks for matches continuing from where the previous match left off
def next_match(pattern):
    global pos, text

    match = re.compile(pattern).search(text, pos)

    if match is not None:
        pos = match.end()
        return True
    else:
        return False


def test_add_federation_nonexistent_cluster():
    """Add federation with non-existent cluster(s)"""

    error = atf.run_command_error(f"sacctmgr -i add federation {federation1} cluster={cluster1}", user=atf.properties['slurm-user'])
    assert re.search(rf"The cluster {cluster1} doesn't exist\. Please add first\.", error) is not None

    error = atf.run_command_error(f"sacctmgr -i add federation {federation1} cluster={cluster1},{cluster2}", user=atf.properties['slurm-user'])
    assert re.search(rf"The cluster {cluster1} doesn't exist\. Please add first\.", error) is not None
    assert re.search(rf"The cluster {cluster2} doesn't exist\. Please add first\.", error) is not None


def test_add_cluster_nonexistent_federation():
    """Add cluster to non-existent federation"""

    error = atf.run_command_error(f"sacctmgr -i add cluster {cluster1} federation={federation1}", user=atf.properties['slurm-user'])
    assert re.search(rf"The federation {federation1} doesn't exist\.", error) is not None


def test_add_federation():
    """Add new federation"""

    output = atf.run_command_output(f"sacctmgr -i add federation {federation1}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Federation\(s\)', output)
    assert next_match(rf"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20", user=atf.properties['slurm-user'])
    assert first_match(r'Federation', output)
    assert next_match(rf"(?m)^ +{federation1}")


def test_add_existing_federation():
    """Add new federation - already exists"""

    output = atf.run_command_output(f"sacctmgr -i add federation {federation1}", user=atf.properties['slurm-user'])
    assert re.search(rf"This federation {federation1} already exists\. +Not adding\.", output) is not None


def test_add_second_federation():
    """Add second federation and make sure that you can select only one federation"""

    output = atf.run_command_output(f"sacctmgr -i add federation {federation2}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Federation\(s\)', output)
    assert next_match(rf"(?m)^ +{federation2}")

    output = atf.run_command_output(f"sacctmgr show federation format=federation%20", user=atf.properties['slurm-user'])
    assert first_match('Federation', output)
    assert next_match(rf"(?m)^ +{federation1}")
    assert next_match(rf"(?m)^ +{federation2}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20", user=atf.properties['slurm-user'])
    assert first_match('Federation', output)
    assert next_match(rf"(?m)^ +{federation1}")
    assert not next_match(rf"(?m)^ +{federation2}")


def test_add_cluster_state_federation():
    """Add new cluster with state to existing federation"""

    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster1} federation={federation1} fedstate=drain", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +{cluster1}")
    assert next_match(r'Setting')
    assert next_match(rf"(?m)^ +Federation += +{federation1}")
    assert next_match(r"(?m)^ +FedState += +DRAIN")


def test_add_multiple_clusters_federation():
    """Add multiple new clusters to single federation"""

    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster2} {cluster3} federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +{cluster2}")
    assert next_match(rf"(?m)^ +Name += +{cluster3}")
    assert next_match(r'Setting')
    assert next_match(rf"Federation += +{federation2}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=cluster%20,federation%20,fedstate%20,id", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(rf"(?m)^ +{cluster1} +{federation1} +DRAIN +1")
    assert next_match(rf"(?m)^ +{cluster2} +{federation2} +ACTIVE +1")
    assert next_match(rf"(?m)^ +{cluster3} +{federation2} +ACTIVE +2")


def test_show_cluster_WithFed():
    """Show cluster WithFed"""

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} WithFed", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster.*?Federation +ID +Features +FedState', output)
    assert next_match(rf"(?m)^ +{cluster1}.*?federatio.{{1,2}} +1 +DRAIN")
    assert next_match(rf"(?m)^ +{cluster2}.*?federatio.{{1,2}} +1 +ACTIVE")
    assert next_match(rf"(?m)^ +{cluster3}.*?federatio.{{1,2}} +2 +ACTIVE")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} {federation2} format=\"federation%20,cluster%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Federation +Cluster', output)
    assert next_match(rf"(?m)^ +{federation1} +{cluster1}")
    assert next_match(rf"(?m)^ +{federation2} +{cluster2}")
    assert next_match(rf"(?m)^ +{federation2} +{cluster3}")


def test_show_federation_tree():
    """Test tree option - shows one federation line per federation"""

    output = atf.run_command_output(f"sacctmgr show federation {federation1} {federation2} format=\"federation%20,cluster%20\" tree", user=atf.properties['slurm-user'])
    assert first_match(r'Federation +Cluster', output)
    assert next_match(rf"{federation1} *\n +{cluster1}")
    assert next_match(rf"{federation2} *\n +{cluster2} *\n +{cluster3}")


def test_clusters_multiple_federations():
    """Attempt to set clusters to multiple federations"""

    error = atf.run_command_error(f"sacctmgr -i modify federation {federation1} {federation2} set clusters={cluster1}", user=atf.properties['slurm-user'])
    assert re.search(r"Can't assign clusters to multiple federations", error) is not None


def test_add_cluster_no_federation():
    """Attempt to set clusters with no where clause"""

    error = atf.run_command_error(f"sacctmgr -i modify federation set clusters={cluster1}", user=atf.properties['slurm-user'])
    assert re.search(r"Can't assign clusters to multiple federations", error) is not None

    error = atf.run_command_error(f"sacctmgr -i modify federation set clusters={cluster1} where cluster={cluster1}", user=atf.properties['slurm-user'])
    assert re.search(r"Can't assign clusters to multiple federations", error) is not None


def test_modify_cluster_fedstate():
    """Modify clusters with fed options"""

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} {cluster3} set fedstate=DRAIN", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +FedState += +DRAIN")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")
    assert next_match(fr"(?m)^ +{cluster3}")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster2} set fedstate=DRAIN+REMOVE", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +FedState += +DRAIN\+REMOVE")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster2}")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} set fedstate=ACTIVE", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +FedState += +ACTIVE")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=\"cluster%20,federation%20,fedstate%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState', output)
    assert next_match(fr"(?m)^ +{cluster1} +{federation1} +ACTIVE")
    assert next_match(fr"(?m)^ +{cluster2} +{federation2} +DRAIN\+REMOVE")
    assert next_match(fr"(?m)^ +{cluster3} +{federation2} +DRAIN")


def test_modify_cluster_federation():
    """Modify cluster to federation. Check ids. Create hole in fed2 ids"""

    results = atf.run_command(f"sacctmgr -i modify cluster {cluster2} set federation={federation1}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster2} is assigned to federation {federation2}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Federation += +{federation1}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster2}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster1} +{federation1} +ACTIVE +1")
    assert next_match(fr"(?m)^ +{cluster2} +{federation1} +ACTIVE +2")
    assert next_match(fr"(?m)^ +{cluster3} +{federation2} +DRAIN +2")

    # Move cluster1 into whole
    results = atf.run_command(f"sacctmgr -i modify cluster {cluster1} set federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster1} is assigned to federation {federation1}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Federation += +{federation2}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster1} +{federation2} +ACTIVE +1")
    assert next_match(fr"(?m)^ +{cluster2} +{federation1} +ACTIVE +2")
    assert next_match(fr"(?m)^ +{cluster3} +{federation2} +DRAIN +2")

    # Move cluster2 back to federation2 and get new id -- 3
    results = atf.run_command(f"sacctmgr -i modify cluster {cluster2} set federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster2} is assigned to federation {federation1}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Federation += +{federation2}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster2}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster1} +{federation2} +ACTIVE +1")
    assert next_match(fr"(?m)^ +{cluster2} +{federation2} +ACTIVE +3")
    assert next_match(fr"(?m)^ +{cluster3} +{federation2} +DRAIN +2")


def test_add_federation_existing_cluster():
    """Add federation with existing clusters"""

    command = f"sacctmgr add federation {federation3} clusters={cluster1},{cluster2}"
    logging.log(logging.NOTE, f"Running command {command} as user {atf.properties['slurm-user']}")
    child = pexpect.spawn('sudo', ['-nu', atf.properties['slurm-user'], '/bin/bash', '-lc', command], encoding='utf-8')
    assert child.expect(f"The cluster {cluster1} is assigned to federation {federation2}") == 0
    assert child.expect(f"The cluster {cluster2} is assigned to federation {federation2}") == 0
    assert child.expect("Are you sure") == 0
    assert child.expect(r"\(N/y\):") == 0
    child.send('y')
    assert child.expect(r"Adding Federation\(s\)") == 0
    assert child.expect(f"(?m)^ +{federation3}") == 0
    assert child.expect("Setting") == 0
    assert child.expect(f"(?m)^ +Cluster += +{cluster1}") == 0
    assert child.expect(f"(?m)^ +Cluster += +{cluster2}") == 0
    assert child.expect("Would you like") == 0
    assert child.expect(r"\(N/y\):") == 0
    child.send('y')
    assert child.expect(pexpect.EOF) == 0

    output = atf.run_command_output(f"sacctmgr show federation {federation1} {federation2} {federation3} format=federation%20,cluster%20,fedstate%20,id", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState +ID', output)
    assert next_match(rf"(?m)^ +{federation1}")
    assert next_match(rf"(?m)^ +{federation2} +{cluster3} +DRAIN +2")
    assert next_match(rf"(?m)^ +{federation3} +{cluster1} +ACTIVE +1")
    assert next_match(rf"(?m)^ +{federation3} +{cluster2} +ACTIVE +2")


def test_modify_cluster_clear_federation():
    """Modify cluster. Clear federation"""

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster3} set federation=", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Federation += +$")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster3}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster3} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster3} +NA +0")


def test_select_clusters_by_federation():
    """Test selecting clusters by federations"""

    output = atf.run_command_output(f"sacctmgr show cluster where fed={federation3} format=\"cluster%20,federation%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +ID', output)
    assert next_match(fr"(?m)^ +{cluster1} +{federation3} +1")
    assert next_match(fr"(?m)^ +{cluster2} +{federation3} +2")


def test_add_cluster_verify_state():
    """Test selecting clusters by federations"""

    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster4}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +{cluster4}")

    # New clusters should have fed_id=0, federation="", fed_state=NA
    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +NA +0")

def test_add_cluster_federation_activates():
    """Test adding cluster to federation sets state to ACTIVE"""

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster4} set federation={federation3}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Federation += +{federation3}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster4}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation3} +ACTIVE +3")


def test_modify_cluster_same_federation():
    """Modifying cluster to same federation shouldln't change fed_id or fed_state"""

    # set state to something other than ACTIVE, it should stay the same
    assert atf.run_command_exit(f"sacctmgr -i modify cluster {cluster4} set fedstate=DRAIN", user=atf.properties['slurm-user']) == 0

    results = atf.run_command(f"sacctmgr -i modify cluster {cluster4} set federation={federation3}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster4} is already assigned to federation {federation3}", results['stderr'])
    assert first_match(r"Nothing to change", results['stdout'])

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation3} +DRAIN +3")


def test_modify_federation_same_cluster():
    """Modifying federation to same cluster shouldln't change anything"""

    error = atf.run_command_error(f"sacctmgr -i modify federation {federation3} set clusters+={cluster4}", user=atf.properties['slurm-user'])
    assert re.search(fr"The cluster {cluster4} is already assigned to federation {federation3}", error) is not None

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation3} +DRAIN +3")


def test_move_cluster_federation_activates():
    """Changing from one federation to another should set the state to active"""

    results = atf.run_command(f"sacctmgr -i modify cluster {cluster4} set federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster4} is assigned to federation {federation3}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Federation += +{federation2}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster4}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation2} +ACTIVE +1")


def test_move_federation_cluster_activates():
    """Same thing for modifying federation - state should go to active"""

    assert atf.run_command_exit(f"sacctmgr -i modify cluster {cluster4} set fedstate=DRAIN", user=atf.properties['slurm-user']) == 0

    results = atf.run_command(f"sacctmgr -i modify federation {federation3} set clusters+={cluster4}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster4} is assigned to federation {federation2}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Cluster +\+= +{cluster4}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation3}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation3} +ACTIVE +3")


def test_set_state_inactive():
    """Test setting state to INACTIVE"""

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster4} set fedstate=INACTIVE", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +FedState += +INACTIVE")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster4}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster4} format=\"cluster%20,federation%20,fedstate%20,id\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(fr"(?m)^ +{cluster4} +{federation3} +INACTIVE +3")


def test_invalid_state():
    """Test invalid state"""

    error = atf.run_command_error(f"sacctmgr -i modify cluster {cluster4} set fedstate=abcdefg", user=atf.properties['slurm-user'])
    assert first_match(r"Invalid FedState abcdefg", error)


def test_modify_federation_change_clusters():
    """Modify federation change clusters - must remove others"""

    atf.run_command(f"sacctmgr -i modify cluster {cluster1} {cluster2} {cluster3} set federation=", user=atf.properties['slurm-user'], fatal=True)

    output = atf.run_command_output(f"sacctmgr -i modify federation {federation1} set clusters={cluster1},{cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Cluster +\+= +{cluster1}")
    assert next_match(fr"(?m)^ +Cluster +\+= +{cluster2}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +{cluster1} +ACTIVE")
    assert next_match(rf"(?m)^ +{federation1} +{cluster2} +ACTIVE")

    results = atf.run_command(f"sacctmgr -i modify federation {federation1} set clusters={cluster1},{cluster3}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster1} is already assigned to federation {federation1}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Cluster +\+= +{cluster3}")
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster2}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +{cluster1} +ACTIVE")
    assert next_match(rf"(?m)^ +{federation1} +{cluster3} +ACTIVE")

    output = atf.run_command_output(f"sacctmgr -i modify federation {federation1} set clusters+={cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Cluster +\+= +{cluster2}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +{cluster1} +ACTIVE")
    assert next_match(rf"(?m)^ +{federation1} +{cluster2} +ACTIVE")
    assert next_match(rf"(?m)^ +{federation1} +{cluster3} +ACTIVE")

    output = atf.run_command_output(f"sacctmgr -i modify federation {federation1} set clusters-={cluster1},{cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster1}")
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster2}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +{cluster3} +ACTIVE")

    results = atf.run_command(f"sacctmgr -i modify federation {federation1} set clusters-={cluster1},{cluster3}", user=atf.properties['slurm-user'])
    assert first_match(rf"The cluster {cluster1} isn't assigned to federation {federation1}", results['stderr'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster3}")
    assert next_match(r'Modified federation\.\.\.')
    assert next_match(fr"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +$")


def test_operators():
    """Eror checking on using +, - and ="""

    error = atf.run_command_error(f"sacctmgr -i modify federation {federation1} set cluster={cluster1},+{cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r"You can't use '=' and '\+' or '-' in the same line", error)

    error = atf.run_command_error(f"sacctmgr -i modify federation {federation1} set cluster={cluster1},-{cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r"You can't use '=' and '\+' or '-' in the same line", error)


def test_modify_federation_clear_clusters():
    """Modify federation, clear clusters"""

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} {cluster2} {cluster3} set federation={federation1}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Federation += +{federation1}")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")
    assert next_match(fr"(?m)^ +{cluster2}")
    assert next_match(fr"(?m)^ +{cluster3}")

    output = atf.run_command_output(f"sacctmgr -i modify federation {federation1} set clusters=", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster1}")
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster2}")
    assert next_match(fr"(?m)^ +Cluster +-= +{cluster3}")
    assert next_match(r'Modified federation\.\.\.')

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20,fedstate%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster +FedState', output)
    assert next_match(rf"(?m)^ +{federation1} +$")

    # Verify clusters fed_id=0, federation="", fed_state=NA after being removed from federation
    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} {cluster2} {cluster3} format=cluster%20,federation%20,fedstate%20,id", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    assert next_match(rf"(?m)^ +{cluster1} +NA +0")
    assert next_match(rf"(?m)^ +{cluster2} +NA +0")
    assert next_match(rf"(?m)^ +{cluster3} +NA +0")


def test_delete_cluster_by_federation():
    """Test deleting cluster with where federation= clause"""

    atf.run_command(f"sacctmgr -i delete federation {federation2} {federation3}", user=atf.properties['slurm-user'], fatal=True)

    output = atf.run_command_output(f"sacctmgr -i add federation {federation2} clusters={cluster1},{cluster2}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Federation\(s\)', output)
    assert next_match(rf"(?m)^ +{federation2}")
    assert next_match(r'Setting')
    assert next_match(fr"(?m)^ +Cluster += +{cluster1}")
    assert next_match(fr"(?m)^ +Cluster += +{cluster2}")

    # Add second cluster to make sure selectin only on federation
    output = atf.run_command_output(f"sacctmgr -i add federation {federation3} clusters={cluster3},{cluster4}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Federation\(s\)', output)
    assert next_match(rf"(?m)^ +{federation3}")
    assert next_match(r'Setting')
    assert next_match(fr"(?m)^ +Cluster += +{cluster3}")
    assert next_match(fr"(?m)^ +Cluster += +{cluster4}")

    output = atf.run_command_output(f"sacctmgr -i delete cluster where federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(r'Deleting clusters\.\.\.', output)
    assert next_match(rf"(?m)^ +{cluster1}")
    assert next_match(rf"(?m)^ +{cluster2}")

    # Add back clusters back to federation to verify both federation clusters are selected
    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster1},{cluster2} federation={federation2}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +{cluster1}")
    assert next_match(rf"(?m)^ +Name += +{cluster2}")
    assert next_match(r'Setting')
    assert next_match(fr"(?m)^ +Federation += +{federation2}")

    output = atf.run_command_output(f"sacctmgr -i delete cluster where federation={federation2},{federation3}", user=atf.properties['slurm-user'])
    assert first_match(r'Deleting clusters\.\.\.', output)
    assert next_match(rf"(?m)^ +{cluster1}")
    assert next_match(rf"(?m)^ +{cluster2}")
    assert next_match(rf"(?m)^ +{cluster3}")
    assert next_match(rf"(?m)^ +{cluster4}")


def test_add_max_clusters_federation():
    """Test adding more than 63 clusters to a federation"""

    atf.run_command(f"sacctmgr -i modify federation {federation1} set clusters=", user=atf.properties['slurm-user'])

    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster_string} federation={federation1}", user=atf.properties['slurm-user'], timeout=300)
    assert first_match(r'Adding Cluster\(s\)', output)
    for cluster in cluster_list:
        assert next_match(rf"(?m)^ +Name += +{cluster}")
    assert next_match(r'Setting')
    assert next_match(fr"(?m)^ +Federation += +{federation1}")

    results = atf.run_command(f"sacctmgr -i add cluster cluster{max_fed_clusters} federation={federation1}", user=atf.properties['slurm-user'])
    assert first_match(r'Problem adding clusters: Too many clusters in federation', results['stderr'])
    assert first_match(r'Adding Cluster\(s\)', results['stdout'])
    assert next_match(rf"(?m)^ +Name += +cluster{max_fed_clusters}")
    assert next_match(r'Setting')
    assert next_match(fr"(?m)^ +Federation += +{federation1}")


def test_modify_max_clusters_federation():
    """Modify cluster to exceed max clusters in federation"""

    output = atf.run_command_output(f"sacctmgr -i add cluster cluster{max_fed_clusters}", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +cluster{max_fed_clusters}")

    results = atf.run_command(f"sacctmgr -i modify cluster cluster{max_fed_clusters} set federation={federation1}", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', results['stdout'])
    assert next_match(fr"(?m)^ +Federation += +{federation1}")
    assert first_match(r"Too many clusters in federation", results['stderr'])

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster', output)
    for cluster in cluster_list:
        assert re.search(rf"(?m)^ +{federation1} +{cluster} *$", output) is not None


def test_delete_cluster():
    """Delete cluster - should delete it from federation"""

    output = atf.run_command_output(f"sacctmgr -i delete cluster {cluster1}", user=atf.properties['slurm-user'])
    assert first_match(r'Deleting clusters\.\.\.', output)
    assert next_match(rf"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster', output)
    assert re.search(rf"(?m)^ +{federation1} +{cluster1} *$", output) is None


def test_delete_federation():
    """Delete federation - should clean clusters from federation"""

    output = atf.run_command_output(f"sacctmgr -i delete federation {federation1}", user=atf.properties['slurm-user'], fatal=True)
    assert first_match(r'Deleting federations\.\.\.', output)
    assert next_match(rf"(?m)^ +{federation1}")

    output = atf.run_command_output(f"sacctmgr show federation {federation1} format=federation%20,cluster%20", user=atf.properties['slurm-user'])
    assert first_match('Federation +Cluster', output)
    assert re.search(rf"(?m)^ +{federation1} +$", output) is None

    output = atf.run_command_output(f"sacctmgr show cluster format=cluster%20,federation%20", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation', output)
    assert re.search(rf"(?m)^ +{federation1}", output) is None

    # Verify clusters fed_id=0, federation="", fed_state=NA after federation is deleted
    output = atf.run_command_output(f"sacctmgr show cluster {cluster_string} format=cluster%20,federation%20,fedstate%20,id", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Federation +FedState +ID', output)
    for cluster in cluster_list[1:]:
        assert re.search(rf"(?m)^ +{cluster} +NA +0", output) is not None


def test_add_modify_cluster_features():
    """Add/modify cluster features"""

    output = atf.run_command_output(f"sacctmgr -i add cluster {cluster1} features=a,b", user=atf.properties['slurm-user'])
    assert first_match(r'Adding Cluster\(s\)', output)
    assert next_match(rf"(?m)^ +Name += +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} format=\"cluster%20,features%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Features', output)
    assert next_match(fr"(?m)^ +{cluster1} +a,b")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} set features=aa,ab", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(rf"(?m)^ +Feature += +aa")
    assert next_match(rf"(?m)^ +Feature += +ab")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} format=\"cluster%20,features%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Features', output)
    assert next_match(fr"(?m)^ +{cluster1} +aa,ab")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} set features+=fc", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(rf"(?m)^ +Feature +\+= +fc")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} format=\"cluster%20,features%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Features', output)
    assert next_match(fr"(?m)^ +{cluster1} +aa,ab,fc")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} set features-=ab", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(rf"(?m)^ +Feature +-= +ab")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} format=\"cluster%20,features%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Features', output)
    assert next_match(fr"(?m)^ +{cluster1} +aa,fc")

    output = atf.run_command_output(f"sacctmgr -i modify cluster {cluster1} set features-=aa,fc", user=atf.properties['slurm-user'])
    assert first_match(r'Setting', output)
    assert next_match(rf"(?m)^ +Feature +-= +aa")
    assert next_match(rf"(?m)^ +Feature +-= +fc")
    assert next_match(r'Modified cluster\.\.\.')
    assert next_match(fr"(?m)^ +{cluster1}")

    output = atf.run_command_output(f"sacctmgr show cluster {cluster1} format=\"cluster%20,features%20\"", user=atf.properties['slurm-user'])
    assert first_match(r'Cluster +Features', output)
    assert next_match(fr"(?m)^ +{cluster1} +$")
