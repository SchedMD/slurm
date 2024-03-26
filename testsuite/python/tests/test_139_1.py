############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

node_prefix = "atf_node"
suspend_time = 10
resume_time = 10
max_nodes = 5


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to set required dynamic node parameters")
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    # Time for node to sit idle with no jobs before being told to power down
    atf.require_config_parameter("SuspendTime", suspend_time)

    # Time to wait for node to power up and register with slurmctld after being assigned job
    atf.require_config_parameter("ResumeTimeout", resume_time)

    # Tells slurmctld to set NodeName and NodeAddr for nodes when they get registered
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")

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

    # Needed fo MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Needed fo cons_tres
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    atf.require_slurm_running()


# Helper fixtures
@pytest.fixture
def create_node():
    created_nodes = []

    def scontrol_create_node(node_name, state, feature, xfail=False, fatal=False):
        creation_exit_code = atf.run_command_exit(
            f"scontrol create NodeName={node_name} State={state} Feature={feature}",
            user="slurm",
            xfail=xfail,
            fatal=fatal,
        )
        created = 0 == creation_exit_code

        if created:
            created_nodes.append(node_name)

        return created != xfail

    yield scontrol_create_node

    # Delete the created nodes Slurm can see
    for node_name in created_nodes:
        # Node must have no jobs running to be deleted
        atf.repeat_until(
            lambda: atf.get_node_parameter(node_name, "State").split("+"),
            lambda states: "ALLOCATED" not in states and "MIXED" not in states,
            fatal=True,
        )
        atf.run_command(
            f"scontrol delete NodeName={node_name}", fatal=True, user="slurm"
        )


@pytest.fixture
def register_node():
    registered_nodes = []

    def register_node_slurmd(node_name, feature, xfail=False, fatal=False):
        registration_exit_code = atf.run_command_exit(
            f"{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name} -Z -b --conf 'feature={feature}'",
            user="root",
            xfail=xfail,
            fatal=fatal,
        )
        registered = 0 == registration_exit_code

        if registered:
            registered_nodes.append(node_name)

        return registered != xfail

    yield register_node_slurmd

    # Kill the registered slurmds for each test
    for node_name in registered_nodes:
        # Wait for jobs to finish to facilitate deleting node later
        atf.repeat_until(
            lambda: atf.get_node_parameter(node_name, "State").split("+"),
            lambda states: "ALLOCATED" not in states and "MIXED" not in states,
            fatal=True,
        )
        node_pid = atf.run_command_output(
            f"pgrep -f '{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name}'",
            fatal=True,
        ).strip()
        atf.run_command(f"kill {node_pid}", fatal=True, user="root")


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

    current_states = atf.get_node_parameter(node_name, "State").split("+")
    return all(
        undesired_state not in current_states
        for undesired_state in ["POWERING_DOWN", "POWERED_DOWN", "POWERING_UP"]
    )


def node_is_idle(node_name):
    return "IDLE" in atf.get_node_parameter(node_name, "State").split("+")


# Tests
# Ensure illegal dynamic node values are rejected
@pytest.mark.parametrize(
    "illegal_state", ["DOWN", "DRAIN", "FAIL", "FAILING", "UNKNOWN"]
)
def test_illegal_scontrol_creation_states(create_node, illegal_state):
    create_node(f"{node_prefix}1", illegal_state, "f1", xfail=True, fatal=True)


# Ensure legal dynamic node values are accepted
@pytest.mark.parametrize("legal_state", ["CLOUD", "FUTURE"])
def test_legal_scontrol_creation_states(create_node, legal_state):
    create_node(f"{node_prefix}1", legal_state, "f1", fatal=True)

    node_states = atf.get_node_parameter(f"{node_prefix}1", "State").split("+")
    assert (
        legal_state in node_states
    ), f"Dynamic node should have {legal_state} in its state"
    assert (
        "DYNAMIC_NORM" in node_states
    ), "Dynamic node should have DYNAMIC_NORM in its state"


# Test Slurm doesn't allow creation of more nodes than MaxNodeCount
def test_max_node_count(create_node):
    # Create maximum number of legal nodes
    for i in range(len(atf.get_nodes()), max_nodes):
        create_node(f"{node_prefix}{i}", "CLOUD", "f1", fatal=True)

    # Test there are now the maximum number of nodes allowed
    assert (
        num_nodes := len(atf.get_nodes())
    ) == max_nodes, (
        f"Number of nodes ({num_nodes}) should equal MaxNodeCount ({max_nodes})"
    )

    # Try creating one more node than the allowed maximum
    create_node(f"{node_prefix}{max_nodes}", "CLOUD", "f1", xfail=True, fatal=True)


# Test a newly created dynamic node powers up and is idle
def test_full_node_registration(create_node, register_node):
    create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    register_node(f"{node_prefix}1", "f1", fatal=True)
    assert node_powered_up(
        f"{node_prefix}1"
    ), f"Dynamic node should have finished POWERING_UP"
    assert node_is_idle(
        f"{node_prefix}1"
    ), f"Dynamic node should be IDLE because it has powered up without any jobs"


# Test a newly created dynamic node that never registers powers down
def test_missing_node_registration(create_node):
    create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    assert atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=resume_time + 10
    ), f"Dynamic node should have powered down when not registered"


# Test dynamic nodes correctly assigned to nodesets/partitions
def test_nodesets(create_node):
    num_f1_nodes = 0
    num_f2_nodes = 0

    # Create maximum number of legal nodes, alternating between features
    for i in range(len(atf.get_nodes()), max_nodes):
        create_node(
            f"{node_prefix}{i}",
            "CLOUD",
            f"f{i % 2 + 1}",
            fatal=True,
        )

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
    # Power up a dynamic node and maintain idle state
    create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    register_node(f"{node_prefix}1", "f1", fatal=True)
    assert node_powered_up(
        f"{node_prefix}1"
    ), f"Dynamic node should have finished POWERING_UP"
    assert node_is_idle(
        f"{node_prefix}1"
    ), f"Dynamic node should be IDLE because it has powered up without any jobs"

    # Submit job and wait for completion
    job_id = atf.submit_job_sbatch("-p dynamic1 --wrap 'srun hostname'", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", timeout=30, fatal=True)

    # Test job on the dynamic node completed successfully
    assert (
        atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
        == "COMPLETED"
    ), "Dynamic Node job should be COMPLETED"


# Test dynamic node powers down when idle and powered up too long
def test_node_suspension(create_node, register_node):
    # Power up a dynamic node and maintain idle state
    create_node(f"{node_prefix}1", "CLOUD", "f1", fatal=True)
    register_node(f"{node_prefix}1", "f1", fatal=True)
    assert node_powered_up(
        f"{node_prefix}1"
    ), f"Dynamic node should have finished POWERING_UP"
    assert node_is_idle(
        f"{node_prefix}1"
    ), f"Dynamic node should be IDLE because it has powered up without any jobs"

    # Test node powers down without a job
    assert atf.wait_for_node_state(
        f"{node_prefix}1", "POWERING_DOWN", timeout=suspend_time + 10
    ), f"Dynamic node should be POWERING_DOWN after suspend_time ({suspend_time} secs)"
