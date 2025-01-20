############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import time

node_prefix = "atf_node"
# Note that power_interval has to be at least 5 seconds, recommended 10 seconds
# for later tests
power_interval = 10
suspend_time = 10
suspend_timeout = 10
resume_timeout = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Runs slurmd on same machine as slurmctld")
    atf.require_config_parameter("NodeFeaturesPlugins", "node_features/helpers")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")

    # Time for cloud node to sit idle with no jobs before told to power down
    atf.require_config_parameter("SuspendTime", suspend_time)

    # Time allowed for cloud node to finish POWERING_DOWN
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)

    # Time to wait for cloud node to power up and register with slurmctld after
    # being ALLOCATED job
    atf.require_config_parameter("ResumeTimeout", resume_timeout)

    # Mark nodes as IDLE, regardless of current state, when suspending nodes with
    # SuspendProgram so that nodes will be eligible to be resumed at a later time
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")

    # Register the cloud node in slurm.conf
    atf.require_config_parameter(
        "NodeName", {f"{node_prefix}1": {"Feature": "f1,nf1", "State": "CLOUD"}}
    )

    # Create set/group of nodes that have feature f1 and assign to partitions
    atf.require_config_parameter("Nodeset", {"ns1": {"Feature": "f1"}})
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL"},
            "cloud1": {"Nodes": "ns1"},
            "powerdownonidle": {"Nodes": "ns1", "PowerDownOnIdle": "yes"},
        },
    )

    # Define nf1 feature in 'helpers.conf'. Used for testing Node Features
    atf.require_config_parameter(
        "NodeName",
        {f"{node_prefix}1": {"Feature": "nf1", "Helper": "/bin/true"}},
        source="helpers",
    )

    # Don't run the usual atf.require_slurm_running() because tests will start
    # slurmds manually
    atf.start_slurmctld(clean=True)

    yield

    # Have to manually kill the slurmctld and slurmd in the teardown
    kill_slurmds()
    kill_slurmctld()


@pytest.fixture(scope="function", autouse=True)
def cloud_state():
    """stop all jobs and reset cloud node state after each test"""
    yield
    atf.cancel_all_jobs()
    kill_slurmds()
    atf.restart_slurmctld(clean=True)
    time.sleep(2)


# Helper teardown functions
# Since our teardown doesn't seem to remove cloud nodes' slurmds, we need to
# delete the slurmds manually
def kill_slurmds():
    get_slurmd_processes = atf.run_command(
        f"pidof {atf.properties['slurm-sbin-dir']}/slurmd"
    )
    pids = get_slurmd_processes["stdout"].strip().split()
    for pid in pids:
        atf.run_command(f"kill {pid}", fatal=True, user="root")


# Since our teardown doesn't seem to remove slurmctlds started with
# "atf.start_slurmctld()", we need to delete the slurmctld
def kill_slurmctld():
    get_slurmctld_process = atf.run_command(
        f"pidof {atf.properties['slurm-sbin-dir']}/slurmctld"
    )
    pids = get_slurmctld_process["stdout"].strip().split()
    for pid in pids:
        atf.run_command(f"kill {pid}", fatal=True, user="root")


# Tests
# Test state cycle of cloud nodes: POWERED_DOWN, POWERING_UP, IDLE,
# POWERING_DOWN, POWERED_DOWN
def test_cloud_state_cycle():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # Schedule a job to cloud node's partition, transitioning node to ALLOCATED
    # and POWERING_UP state
    job_id = atf.submit_job_sbatch(f"-p cloud1 --wrap 'srun hostname'", fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "ALLOCATED", timeout=5, fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be in CONFIGURING state while its ALLOCATED cloud node is POWERING_UP"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Cloud node takes PERIODIC_TIMEOUT seconds to register and resume.
    # The job has 45 seconds to finish
    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # Make sure the cloud node starts suspending once it hasn't received a job
    # for suspend_time
    atf.wait_for_node_state_any(
        f"{node_prefix}1",
        ["POWER_DOWN", "POWERING_DOWN"],
        timeout=suspend_time + 5,
        fatal=True,
    )

    # Make sure the cloud node is fully POWERED_DOWN by suspend_timeout
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"


# Test that cloud node powers down if exceeding resume_timeout
def test_resume_timeout():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # Schedule a job to cloud node's partition, transitioning node to ALLOCATED
    # and POWERING_UP state
    job_id = atf.submit_job_sbatch(f"-p cloud1 --wrap 'srun hostname'", fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "ALLOCATED", timeout=5, fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be in CONFIGURING state while its ALLOCATED cloud node is POWERING_UP"

    # Never spin up the accompanying slurmd, waiting resume_timeout for cloud
    # node to be DOWN
    atf.wait_for_node_state(
        f"{node_prefix}1", "DOWN", timeout=resume_timeout + 5, fatal=True
    )

    # Assert surpassing resume_timeout correctly set cloud node's state
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Cloud node didn't enter POWERED_DOWN state immediately after surpassing resume timeout"


# Test scontrol setting cloud node state using POWER_UP and POWER_DOWN
def test_scontrol_power_up_down():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # scontrol POWER_UP
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_UP",
        fatal=True,
        user="slurm",
    )
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_UP", "POWERING_UP"]) & node_state
    ), "Cloud node should enter powering up process after scontrol command"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # scontrol POWER_DOWN
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_DOWN",
        fatal=True,
        user="slurm",
    )
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_DOWN", "POWERING_DOWN"]) & node_state
    ), "POWER_DOWN should immediately be added to cloud node's state"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_DOWN", fatal=True)
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"


# Test scontrol setting cloud node state using POWER_DOWN_ASAP
def test_scontrol_power_down_asap():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # scontrol POWER_UP
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_UP",
        fatal=True,
        user="slurm",
    )
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_UP", "POWERING_UP"]) & node_state
    ), "Cloud node should enter powering up process after scontrol command"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Submit job in preparation for POWER_DOWN_ASAP
    job_id = atf.submit_job_sbatch(f"-p cloud1 --wrap 'srun sleep 5'", fatal=True)
    atf.wait_for_job_state(
        job_id, "RUNNING", timeout=atf.PERIODIC_TIMEOUT + 5, fatal=True
    )
    assert "ALLOCATED" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Cloud node should be ALLOCATED while running a job"

    # scontrol POWER_DOWN_ASAP
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_DOWN_ASAP",
        fatal=True,
        user="slurm",
    )
    assert "POWER_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "POWER_DOWN should immediately be added to cloud node's state"
    assert "DRAIN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Cloud node should be in DRAIN state to prepare for POWER_DOWN state"

    # Wait for job to finish and then assert cloud node becomes POWERED_DOWN
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_DOWN", fatal=True)
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
    )


# Test scontrol setting cloud node state using POWER_DOWN_FORCE and RESUME
def test_scontrol_power_down_force_and_resume():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # scontrol POWER_UP
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_UP",
        fatal=True,
        user="slurm",
    )
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_UP", "POWERING_UP"]) & node_state
    ), "Cloud node should enter powering up process after scontrol command"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Submit job in preparation for POWER_DOWN_FORCE to cancel
    job_id = atf.submit_job_sbatch(f"-p cloud1 --wrap 'srun sleep 300'", fatal=True)
    atf.wait_for_job_state(
        job_id, "RUNNING", timeout=atf.PERIODIC_TIMEOUT + 5, fatal=True
    )
    assert "ALLOCATED" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Cloud node should be ALLOCATED while running a job"

    # scontrol POWER_DOWN_FORCE when cloud node is already up
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_DOWN_FORCE",
        fatal=True,
        user="slurm",
    )

    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # Make sure job is requeued and cloud node is POWERED_DOWN
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_DOWN", "POWERING_DOWN"]) & node_state
    ), "Cloud node should enter powering down process immediately after scontrol command"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_DOWN", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "PENDING", timeout=10, fatal=True
    ), "Job should be requeued and PENDING after node is powered down"

    # Test scontrol RESUME sets cloud node to POWERED_DOWN when POWERING_DOWN
    # already
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=RESUME",
        fatal=True,
        user="slurm",
    )
    atf.wait_for_node_state(f"{node_prefix}1", "POWERED_DOWN", timeout=5, fatal=True)


# Test cloud nodes POWER_DOWN and then power up with different ActiveFeatures
# to handle jobs
def test_node_features():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # Periodically make sure that the AvailableFeatures for the cloud node are correct
    available_features = sorted(["f1", "nf1"])
    assert available_features == sorted(
        atf.get_node_parameter(f"{node_prefix}1", "AvailableFeatures").split(",")
    ), "Cloud node's AvailableFeatures don't match what was set in slurm.conf"

    # Schedule a job to cloud node's partition, transitioning node to ALLOCATED
    # and POWERING_UP state
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 -C f1 --wrap 'srun hostname'", fatal=True
    )
    atf.wait_for_node_state(f"{node_prefix}1", "ALLOCATED", timeout=5, fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be in CONFIGURING state while its ALLOCATED cloud node is POWERING_UP"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Assert cloud node maintains expected configuration of features (not nf1)
    assert available_features == sorted(
        atf.get_node_parameter(f"{node_prefix}1", "AvailableFeatures").split(",")
    ), "Cloud node's AvailableFeatures don't match what was set in slurm.conf"
    assert "f1" == atf.get_node_parameter(
        f"{node_prefix}1", "ActiveFeatures"
    ), "Cloud node should only have the 'f1' feature when none are explicitly requested"

    # Cloud node takes PERIODIC_TIMEOUT seconds to register and resume.
    # The job has 45 seconds to finish
    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"

    # Now submit job requiring nf1 feature, which cloud node doesn't currently
    # have active
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 -C nf1 -w {node_prefix}1 --wrap 'srun hostname'", fatal=True
    )

    # Make sure cloud node enters POWERING_DOWN state without running the job
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERING_DOWN", timeout=suspend_time + 15, fatal=True
    )
    assert "PENDING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Job should be pending because there are no cloud nodes with the requested feature active"

    # Cloud node should enter "POWERED_DOWN" state
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
    )

    # Should become ALLOCATED once scheduler runs, notwithstanding whether node
    # was already PLANNED or not
    atf.wait_for_node_state(
        f"{node_prefix}1", "ALLOCATED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )

    # Cloud node then enters POWERING_UP once the power thread runs
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be in CONFIGURING state while its ALLOCATED cloud node is POWERING_UP"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Test the cloud node now has the activated feature needed to run the job
    assert available_features == sorted(
        atf.get_node_parameter(f"{node_prefix}1", "AvailableFeatures").split(",")
    ), "Cloud node's AvailableFeatures don't match what was set in slurm.conf"
    assert available_features == sorted(
        atf.get_node_parameter(f"{node_prefix}1", "ActiveFeatures").split(",")
    ), "Cloud node should have both of its available features active"

    # Cloud node takes PERIODIC_TIMEOUT seconds to register and resume.
    # The job has 45 seconds to finish
    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )
    atf.wait_for_node_state(f"{node_prefix}1", "IDLE", timeout=5, fatal=True)


# Test partition flag 'PowerDownOnIdle=yes'
def test_power_down_on_idle():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # Schedule a job to cloud node's partition, transitioning node to ALLOCATED
    # and POWERING_UP state
    job_id = atf.submit_job_sbatch(
        f"-p powerdownonidle --wrap 'srun hostname'", fatal=True
    )
    atf.wait_for_node_state(f"{node_prefix}1", "ALLOCATED", timeout=5, fatal=True)
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_UP", fatal=True)
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be in CONFIGURING state while its ALLOCATED cloud node is POWERING_UP"

    # TODO: Wait 2 seconds to avoid race condition between slurmd and slurmctld
    #       Remove once bug 16459 is fixed.
    time.sleep(2)

    # Register the new slurmd
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -b -N {node_prefix}1 --conf 'feature=f1'",
        fatal=True,
        user="root",
    )

    # Make sure the cloud node resumes
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )

    # Cloud node takes PERIODIC_TIMEOUT seconds to register and resume.
    # The job has 45 seconds to finish
    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )

    # Immediately upon job completion and becoming IDLE, cloud node should
    # POWER_DOWN
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_DOWN", "POWERING_DOWN"]) & node_state
    ), "Cloud node wasn't immediately POWER_DOWN once idle, in contrary to 'PowerDownOnIdle=yes' flag for parition"
    atf.wait_for_node_state(f"{node_prefix}1", "POWERING_DOWN", fatal=True)
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN", timeout=suspend_timeout + 5, fatal=True
    )
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"


# Make the check-in interval of the power save thread longer so we have time to
# POWER_DOWN_FORCE a cloud node that's already POWERED_DOWN and ALLOCATED before
# it's powered up. Saved as last test due to changing the slurm.conf file
def test_scontrol_power_down_force():
    assert "CLOUD" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node state should always contain CLOUD"
    assert "IDLE" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in IDLE state"
    assert "POWERED_DOWN" in atf.get_node_parameter(f"{node_prefix}1", "State").split(
        "+"
    ), "CLOUD node must be in POWERED_DOWN state"

    # Change power save thread minimum interval and restart slurmctld
    atf.require_config_parameter_includes(
        "SlurmctldParameters", f"power_save_min_interval={power_interval}"
    )
    atf.restart_slurmctld(clean=True)

    # Submit job, get it assigned to the cloud node, and make sure everything
    # goes well before cloud node enters POWERING_UP state
    job_id = atf.submit_job_sbatch(f"-p cloud1 --wrap 'srun sleep 300'", fatal=True)
    atf.wait_for_node_state(
        f"{node_prefix}1", "ALLOCATED", timeout=5, poll_interval=0.1, fatal=True
    )
    assert "CONFIGURING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Submitted job should be CONFIGURING while there is a corresponding ALLOCATED cloud node"

    # Now POWER_DOWN_FORCE the cloud node, make sure it enters POWER_DOWN and
    # never POWERING_UP, and assure that the job gets requeued
    assert atf.wait_for_node_state(
        f"{node_prefix}1", "POWERED_DOWN"
    ), "Couldn't run POWER_DOWN_FORCE on cloud node before it left POWERED_DOWN state. Try making power_interval longer to give the test more time"
    atf.run_command(
        f"scontrol update nodename={node_prefix}1 state=POWER_DOWN_FORCE",
        fatal=True,
        user="slurm",
    )
    assert atf.wait_for_node_state(
        f"{node_prefix}1", "IDLE"
    ), "Per 'SlurmctldParameters=idle_on_node_suspend' in slurm.conf, cloud node should always be in IDLE state except when ALLOCATED/MIXED for an assigned job"
    # Handle a slight race condition where the node could be POWERING_DOWN before the check
    # Pretty unlikely
    node_state = set(atf.get_node_parameter(f"{node_prefix}1", "State").split("+"))
    assert (
        set(["POWER_DOWN", "POWERING_DOWN"]) & node_state
    ), "POWER_DOWN should immediately be added to cloud node's state"
    assert "PENDING" == atf.get_job_parameter(
        job_id, "JobState", default="NOT_FOUND", quiet=True
    ), "Job should be PENDING after being requeued"

    # Make sure node finishes powering down correctly
    atf.wait_for_node_state(
        f"{node_prefix}1", "POWERING_DOWN", timeout=power_interval + 5, fatal=True
    )
    atf.wait_for_node_state(
        f"{node_prefix}1",
        "POWERED_DOWN",
        timeout=suspend_timeout + 5,
        fatal=True,
    )
