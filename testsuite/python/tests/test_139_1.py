############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os

import pytest

import atf

# Note that node_prefix needs to be known or handled properly in s2n variant
node_prefix = "node"
suspend_time = 10
suspend_timeout = 10
resume_time = 20
max_nodes = 5
power_programs = ["ResumeProgram", "SuspendProgram"]
power_logs = {}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to set required dynamic node parameters")
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("TreeWidth", 65533)

    # The tests start and stop the slurmds themselves, so the programs only
    # record the nodes slurmctld calls them with. slurmscriptd runs them as
    # SlurmUser, so slurm.conf needs absolute paths and the logs must be
    # writable by that user.
    for program in power_programs:
        log = power_logs[program] = os.path.abspath(f"{program.lower()}.log")
        atf.run_command(f"touch {log}", fatal=True, quiet=True)
        atf.run_command(f"chmod 666 {log}", user="root", fatal=True, quiet=True)
        script = os.path.abspath(f"{program.lower()}.sh")
        atf.make_bash_script(script, f'echo "$1" >> {log}')
        atf.require_config_parameter(program, script)

    # Time for node to sit idle with no jobs before being told to power down
    atf.require_config_parameter("SuspendTime", suspend_time)

    # Time for node to be POWERING_DOWN after suspend_time
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)

    # Time to wait for node to power up and register with slurmctld after being assigned job
    atf.require_config_parameter("ResumeTimeout", resume_time)

    atf.require_config_parameter(
        "Nodeset", {"ns1": {"Feature": "f1"}, "ns2": {"Feature": "f2"}}
    )

    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL"},
            "dynamic1": {"Nodes": "ns1"},
            "dynamic2": {"Nodes": "ns2"},
            "dynamic3": {"Nodes": "ns1,ns2"},
        },
    )

    # Needed for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Needed for cons_tres
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.require_slurm_running()


# Helper fixtures
@pytest.fixture
def power_calls():
    for log in power_logs.values():
        atf.run_command(f"truncate -s 0 {log}", fatal=True, quiet=True)

    def recorded_nodes(program):
        hostlists = atf.run_command_output(
            f"cat {power_logs[program]}", fatal=True, quiet=True
        ).split()
        return {node for hl in hostlists for node in atf.node_range_to_list(hl)}

    return recorded_nodes


# Every dynamic slurmd runs on this host, so each node needs its own port, and
# the node created with scontrol must have the port its slurmd listens on
@pytest.fixture
def node_port():
    # Above the ports the kernel picks for other sockets (32768-60999 by
    # default), so that none can take a port before its slurmd binds it
    base_port = 61800
    ports = {}

    def get_node_port(node_name):
        if node_name not in ports:
            ports[node_name] = base_port + len(ports)
        return ports[node_name]

    return get_node_port


@pytest.fixture
def create_node(node_port):
    created_nodes = []

    def scontrol_create_node(node_name, state, feature, xfail=False, fatal=False):
        command = f"scontrol create NodeName={node_name} State={state}"
        command += f" Feature={feature} Port={node_port(node_name)}"

        results = atf.run_command(
            command,
            user=atf.properties["slurm-user"],
            xfail=xfail,
            fatal=fatal,
        )

        if results["exit_code"] == 0:
            created_nodes.append(node_name)

        return results

    yield scontrol_create_node

    # Delete the created nodes Slurm can see
    delete_nodes_or_fail(created_nodes, wait_for_idle=True)


@pytest.fixture
def register_node(node_port):
    registered_nodes = []

    def register_node_slurmd(node_name, feature, wait="registered", fatal=False):
        registered_nodes.append(node_name)
        return atf.start_slurmd(
            node_name,
            f"-Z -b --conf 'feature={feature} port={node_port(node_name)}'",
            wait=wait,
            fatal=fatal,
        )

    yield register_node_slurmd

    # Kill the registered slurmds for each test
    for node_name in registered_nodes:
        # A slurmd whose registration was rejected has no node to wait for
        if node_name in atf.get_nodes(quiet=True):
            # Wait for jobs to finish to facilitate deleting node later
            for _ in atf.timer(fatal=True):
                states = atf.get_node_parameter(node_name, "state")
                if "ALLOCATED" not in states and "MIXED" not in states:
                    break
        atf.stop_slurmd(node_name)

    # Nodes registered without scontrol create still need to be deleted
    delete_nodes_or_fail(registered_nodes)


# Helper functions
def node_powered_up(node_name):
    # Check if node is powered up. If we catch the node while it is powering up,
    # we wait while it is transitioning states to being powered up
    atf.wait_for_node_state(
        node_name, "POWERING_DOWN", reverse=True, timeout=resume_time + 10
    )
    atf.wait_for_node_state(
        node_name, "POWERED_DOWN", reverse=True, timeout=resume_time + 10
    )
    atf.wait_for_node_state(
        node_name, "POWERING_UP", reverse=True, timeout=resume_time + 10
    )

    current_states = atf.get_node_parameter(node_name, "state")
    return all(
        undesired_state not in current_states
        for undesired_state in ["POWERING_DOWN", "POWERED_DOWN", "POWERING_UP"]
    )


def node_is_idle(node_name):
    return "IDLE" in atf.get_node_parameter(node_name, "state")


# Delete every node that still exists, then report all the failures at once. A
# fatal delete inside the loop would skip every node after it, leaking them
# into the next test's MaxNodeCount budget.
def delete_nodes_or_fail(node_names, wait_for_idle=False):
    errors = []

    for node_name in node_names:
        if node_name not in atf.get_nodes(live=True, quiet=True):
            continue

        if wait_for_idle:
            # Node must have no jobs running to be deleted
            for _ in atf.timer():
                states = atf.get_node_parameter(node_name, "state")
                if "ALLOCATED" not in states and "MIXED" not in states:
                    break
            else:
                errors.append(f"Node {node_name} never left ALLOCATED/MIXED")
                continue

        results = atf.run_command(
            f"scontrol delete NodeName={node_name}",
            user=atf.properties["slurm-user"],
        )
        if results["exit_code"] != 0:
            errors.append(f"Failed to delete node {node_name}: {results['stderr']}")

    if errors:
        pytest.fail("; ".join(errors))


# Tests
# Ensure illegal dynamic node values are rejected
@pytest.mark.parametrize(
    "illegal_state", ["DOWN", "DRAIN", "FAIL", "FAILING", "UNKNOWN"]
)
def test_illegal_scontrol_creation_states(create_node, illegal_state):
    create_node(f"{node_prefix}1", illegal_state, "f1", xfail=True, fatal=True)


# Ensure legal dynamic node values are accepted
@pytest.mark.parametrize(
    "legal_state",
    [
        "CLOUD",
        "FUTURE",
        pytest.param(
            "EXTERNAL",
            marks=pytest.mark.skipif(
                atf.get_version("bin/scontrol") < (25, 11),
                reason="Issue 50689: scontrol omits NODE_STATE_EXTERNAL from --json output before 25.11",
            ),
        ),
    ],
)
def test_legal_scontrol_creation_states(create_node, legal_state):
    create_node(f"{node_prefix}1", legal_state, "f1", fatal=True)

    node_states = atf.get_node_parameter(f"{node_prefix}1", "state")
    assert (
        legal_state in node_states
    ), f"Dynamic node should have {legal_state} in its state"
    assert (
        "DYNAMIC_NORM" in node_states
    ), "Dynamic node should have DYNAMIC_NORM in its state"
    if legal_state == "CLOUD":
        assert (
            "POWERED_DOWN" in node_states
        ), "Dynamic CLOUD node should start POWERED_DOWN"


# Test Slurm doesn't allow creation of more nodes than MaxNodeCount
@pytest.mark.parametrize("creation_method", ["scontrol", "slurmd"])
def test_max_node_count(create_node, register_node, creation_method):
    # Create maximum number of legal nodes
    for i in range(len(atf.get_nodes()), max_nodes):
        if creation_method == "scontrol":
            create_node(f"{node_prefix}{i}", "CLOUD", "f1", fatal=True)
        register_node(f"{node_prefix}{i}", "f1", fatal=True)

    # Test there are now the maximum number of nodes allowed
    assert (
        num_nodes := len(atf.get_nodes())
    ) == max_nodes, (
        f"Number of nodes ({num_nodes}) should equal MaxNodeCount ({max_nodes})"
    )

    # Try creating one more node than the allowed maximum
    if creation_method == "scontrol":
        create_node(f"{node_prefix}{max_nodes}", "CLOUD", "f1", xfail=True, fatal=True)
    else:
        node_name = f"{node_prefix}{max_nodes}"
        register_node(node_name, "f1", wait="process", fatal=True)

        # slurmctld logs the registration it rejects
        log_file = atf.get_config(live=False, quiet=True)["SlurmctldLogFile"]
        for _ in atf.timer(fatal=True):
            if atf.run_command_output(
                f"grep -F 'node={node_name}:' {log_file}",
                user=atf.properties["slurm-user"],
                quiet=True,
            ):
                break

    # Test still max number of nodes
    assert (
        num_nodes := len(atf.get_nodes())
    ) == max_nodes, (
        f"Number of nodes ({num_nodes}) should equal MaxNodeCount ({max_nodes})"
    )


# Test a newly created dynamic node powers up and is idle
@pytest.mark.parametrize("creation_method", ["scontrol", "slurmd"])
def test_full_node_registration(create_node, register_node, creation_method):
    if creation_method == "scontrol":
        create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    register_node(f"{node_prefix}1", "f1", fatal=True)
    assert node_powered_up(
        f"{node_prefix}1"
    ), "Dynamic node should have finished POWERING_UP"
    assert node_is_idle(
        f"{node_prefix}1"
    ), "Dynamic node should be IDLE because it has powered up without any jobs"


# Test a dynamic node whose slurmd never registers goes DOWN after ResumeTimeout
@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 3),
    reason="Issue 51106: a deleted and recreated CLOUD node could not be allocated before 26.05.3",
)
def test_missing_node_registration(create_node, power_calls):
    node = f"{node_prefix}1"
    create_node(node, "CLOUD", "f1", fatal=True)

    job_id = atf.submit_job_sbatch(
        "-p dynamic1 --requeue --wrap 'srun hostname'", fatal=True
    )
    assert atf.wait_for_node_state(
        node, "POWERING_UP", timeout=2 * atf.PERIODIC_TIMEOUT
    ), f"Node {node} should be POWERING_UP for the job"
    for _ in atf.timer(fatal=True):
        if power_calls("ResumeProgram") == {node}:
            break

    assert atf.wait_for_node_state(
        node, "DOWN"
    ), f"Node {node} should be DOWN after ResumeTimeout ({resume_time} secs)"
    assert "POWERED_DOWN" in atf.get_node_parameter(
        node, "state"
    ), f"Node {node} should be POWERED_DOWN after ResumeTimeout"
    assert atf.wait_for_job_state(
        job_id, "PENDING"
    ), f"Job {job_id} should be requeued after ResumeTimeout"
    assert (
        int(atf.get_job_parameter(job_id, "Restarts", default=0)) == 1
    ), f"Job {job_id} should have been requeued once"


# Test dynamic nodes correctly assigned to nodesets/partitions
@pytest.mark.parametrize("creation_method", ["scontrol", "slurmd"])
def test_nodesets(create_node, creation_method, register_node):
    num_f1_nodes = 0
    num_f2_nodes = 0

    # Create maximum number of legal nodes, alternating between features
    for i in range(len(atf.get_nodes()), max_nodes):
        feature = f"f{i % 2 + 1}"
        if creation_method == "scontrol":
            create_node(f"{node_prefix}{i}", "CLOUD", feature, fatal=True)
        register_node(f"{node_prefix}{i}", feature, fatal=True)

        # Keep track of feature node counts
        if i % 2 + 1 == 1:
            num_f1_nodes += 1
        else:
            num_f2_nodes += 1

    # Get number of nodes registered to each partition
    cmd = "sinfo -ho %D -p"
    num_primary = int(atf.run_command_output(f"{cmd} primary", fatal=True))
    num_d1 = atf.run_command_output(f"{cmd} dynamic1", fatal=True)
    num_d2 = atf.run_command_output(f"{cmd} dynamic2", fatal=True)
    num_d3 = atf.run_command_output(f"{cmd} dynamic3", fatal=True)

    # Convert number of nodes in partition to ints. If blank, set to 0
    num_d1 = 0 if not num_d1 else int(num_d1)
    num_d2 = 0 if not num_d2 else int(num_d2)
    num_d3 = 0 if not num_d3 else int(num_d3)

    assert (
        num_primary == max_nodes
    ), f"'primary' partition should have {max_nodes} nodes, got {num_primary}"
    assert (
        num_d1 == num_f1_nodes
    ), f"'dynamic1' partition should have {num_f1_nodes} nodes, got {num_d1}"
    assert (
        num_d2 == num_f2_nodes
    ), f"'dynamic2' partition should have {num_f2_nodes} nodes, got {num_d2}"
    assert (
        num_d3 == num_f1_nodes + num_f2_nodes
    ), f"'dynamic3' partition should have {num_f1_nodes + num_f2_nodes} nodes, got {num_d3}"


# Test dynamic node already powered up and idle runs a job
def test_powered_up_node_job(create_node, register_node):
    # Create two dynamic nodes, one with each creation pattern
    create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    register_node(f"{node_prefix}1", "f1", fatal=True)
    register_node(f"{node_prefix}2", "f1", fatal=True)

    for node in [f"{node_prefix}1", f"{node_prefix}2"]:
        assert node_powered_up(
            node
        ), f"Dynamic node {node} should have finished POWERING_UP"
        assert node_is_idle(
            node
        ), f"Dynamic node {node} should be IDLE because it has powered up without any jobs"

    # Submit job requesting both nodes and wait for completion
    job_id = atf.submit_job_sbatch("-N2 -p dynamic1 --wrap 'srun hostname'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", timeout=2 * atf.PERIODIC_TIMEOUT, fatal=True)

    # Test job on the dynamic nodes completed successfully
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "COMPLETED"
    ), "Dynamic Node job should be COMPLETED"


# Test dynamic nodes pass through a "creation" cycle and a normal "up-down" CLOUD nodes cycle
@pytest.mark.parametrize(
    "command, job",
    [("srun", "printenv SLURMD_NODENAME"), ("sbatch", "srun printenv SLURMD_NODENAME")],
    ids=["srun", "sbatch"],
)
def test_node_lifecycle(create_node, register_node, power_calls, command, job):
    # Create two dynamic nodes, one with each method
    nodes = [f"{node_prefix}1", f"{node_prefix}2"]
    create_node(nodes[0], "CLOUD", "f1", fatal=True)
    for node in nodes:
        register_node(node, "f1", fatal=True)
        assert node_powered_up(
            node
        ), f"Dynamic node {node} should have finished POWERING_UP"
        assert node_is_idle(
            node
        ), f"Dynamic node {node} should be IDLE because it has powered up without any jobs"

    # Test nodes power down without a job
    assert atf.wait_for_node_state(
        nodes, "POWERING_DOWN"
    ), f"Dynamic nodes should be POWERING_DOWN after suspend_time ({suspend_time} secs)"
    assert atf.wait_for_node_state(
        nodes, "POWERED_DOWN"
    ), f"Dynamic nodes should be POWERED_DOWN after suspend_timeout ({suspend_timeout} secs)"
    for _ in atf.timer(fatal=True):
        if set(nodes) <= power_calls("SuspendProgram"):
            break

    # Submit job that will force both nodes to power up
    job_id = atf.submit_job(
        command,
        "-N2 -p dynamic1 -o lifecycle_%t.out",
        job,
        fatal=True,
        background=True,
    )
    assert atf.wait_for_node_state(
        nodes, "POWERING_UP", timeout=2 * atf.PERIODIC_TIMEOUT
    ), "Nodes should be POWERING_UP for the job"
    for _ in atf.timer(fatal=True):
        if power_calls("ResumeProgram") == set(nodes):
            break

    # Start the two slurmds
    for node in nodes:
        register_node(node, "f1", fatal=True)
    assert atf.wait_for_node_state(
        nodes, "POWERING_UP", reverse=True
    ), "Nodes should leave POWERING_UP state once their slurmds register"

    # Wait for job to complete
    atf.wait_for_job_state(job_id, "DONE", timeout=2 * atf.PERIODIC_TIMEOUT, fatal=True)
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "COMPLETED"
    ), f"Dynamic Node job ({command}) should be COMPLETED"
    tasks = sorted(atf.run_command_output("cat lifecycle_*.out", fatal=True).split())
    assert tasks == nodes, f"Each powered up node should run one task, got {tasks}"

    # Check that nodes enter POWERING_DOWN and then POWERED_DOWN states
    assert atf.wait_for_node_state(
        nodes, "POWERING_DOWN"
    ), "Nodes should enter POWERING_DOWN state after being idle for suspend time"
    assert atf.wait_for_node_state(
        nodes, "POWERED_DOWN"
    ), "Nodes should enter POWERED_DOWN state after suspend timeout"
