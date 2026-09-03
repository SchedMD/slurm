############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that powered down nodes are never left in the UNKNOWN state.

The UNKNOWN base state is only a placeholder used while the slurmctld waits
for a slurmd to register. A powered down node has no slurmd to answer, so the
placeholder can never be resolved and the node is stranded: it is left out of
the up/available node bitmaps, so it is never picked for a job and never gets
powered back up on its own.

A reconfigure, and a plain slurmctld restart, both rebuild the node records
out of slurm.conf (where such nodes carry the default State=UNKNOWN) and then
layer the saved state on top. The recovered POWERED_DOWN/POWERING_DOWN flag
must therefore land on an IDLE base state rather than on UNKNOWN.
"""

import os

import pytest

import atf

pytestmark = pytest.mark.slow

# Applied per test rather than module-wide. test_down_node_stays_down pins an
# invariant that already held before the guard was restored, so it passes on
# the broken releases too, and xfail_strict would report that as a failure.
xfail_missing_guard = pytest.mark.xfail(
    (26, 5) <= atf.get_version("sbin/slurmctld") < (26, 5, 5),
    reason="Ticket 25750: 53be38cdc7 dropped the UNKNOWN+POWERED_DOWN"
    " guard in 26.05.0; restored in 26.05.5",
)

# Resolved from slurm.conf by setup(), which every test and helper runs after.
# The whole module operates on a single node.
node_name = None

# Time the node is allowed to spend in POWERING_DOWN. It also bounds how long
# the reconfigure/restart under test may take while the node is still expected
# to be POWERING_DOWN.
suspend_timeout = 60
resume_timeout = 60

# SuspendTime has to be a real value rather than INFINITE. An INFINITE
# SuspendTime turns off automatic power save entirely, and that also stops a
# job from resuming the node it was allocated. Make it long enough that no node
# suspends on its own while the tests run.
suspend_time = 3600


@pytest.fixture(scope="module", autouse=True)
def setup(power_scripts):
    global node_name

    suspend_script, resume_script = power_scripts

    atf.require_nodes(1)
    # Take the node atf manages rather than the first one slurm.conf happens to
    # declare: this is the one whose spool and tmpfs directories conftest
    # created and will clean up. It is only populated under auto-config, which
    # this module needs anyway to rewrite the node line and stop slurmds.
    node_name = atf.properties["nodes"][0]

    atf.require_config_parameter_includes("DebugFlags", "Power")
    atf.require_config_parameter("ReturnToService", 2)
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_config_parameter("SuspendProgram", suspend_script)
    atf.require_config_parameter("ResumeProgram", resume_script)
    # State=UNKNOWN is the slurm.conf default, but pin it so the precondition
    # this test cares about does not depend on how the node was declared.
    # Edit the existing node line rather than replacing it with
    # require_config_parameter("NodeName", {...}): that form drops every other
    # NodeName line along with this node's NodeAddr/NodeHostname/Port. This node
    # is not CLOUD, so _validate_slurmd_addr() does not skip it while it is
    # powered down, and an unresolvable name there means slurmctld overwrites
    # the recovered state with FUTURE / "NO NETWORK ADDRESS FOUND".
    atf.set_node_parameter(node_name, "State", "UNKNOWN")

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def power_scripts():
    """Create the SuspendProgram and ResumeProgram, yielding their paths.

    Gates on auto-config before building anything. The checks below fail on a
    slurm.conf that takes the SlurmdPidFile default, which may occur under
    local-config.

    A SuspendProgram of /bin/true is not enough here: a slurmd that is left
    running would answer the config push a reconfigure sends out and register
    itself, which would hide the state we are trying to observe.

    The ResumeProgram starts the slurmd back up. It keeps the "slurmd -N
    <node>" prefix that atf.start_slurmd() matches on, and adds -b so the
    slurmctld takes the registration as a fresh boot.

    slurmscriptd runs both scripts, so slurm.conf needs absolute paths.
    """

    if not atf.properties["auto-config"]:
        pytest.skip("Needs auto-config to rewrite the node line and stop slurmds")

    scontrol = f"{atf.properties['slurm-bin-dir']}/scontrol"
    slurmd = f"{atf.properties['slurm-sbin-dir']}/slurmd"

    # Kill by pidfile rather than 'pkill -f "slurmd -N $node"'. That pattern
    # also matches a longer node name (node1 matches node10) and sudo's own
    # command line, and pkill does not skip its own parent, so the script can
    # signal sudo, exit non-zero, and have the slurmctld log a SuspendProgram
    # failure.
    # get_config_parameter() casefolds values, which would corrupt a path
    # containing an uppercase character, so read the raw config instead.
    config = atf.get_config(live=False, quiet=True)
    pidfile = next(
        (value for key, value in config.items() if key.casefold() == "slurmdpidfile"),
        None,
    )
    if pidfile is None:
        pytest.fail("slurm.conf must set SlurmdPidFile for the SuspendProgram")
    if "%n" not in pidfile:
        pytest.fail(
            f"SlurmdPidFile ({pidfile}) must contain %n, otherwise every slurmd"
            " shares one pidfile and the SuspendProgram kills the wrong one"
        )
    pidfile = pidfile.replace("%n", "$node")

    suspend_script = os.path.abspath("suspend.sh")
    atf.make_bash_script(
        suspend_script,
        f"""
for node in $({scontrol} show hostnames "$1"); do
    sudo pkill -F {pidfile} -x slurmd
done
""",
    )

    resume_script = os.path.abspath("resume.sh")
    atf.make_bash_script(
        resume_script,
        f"""
sleep 2 # bug 16459: let the slurmctld update the node state first
for node in $({scontrol} show hostnames "$1"); do
    sudo {slurmd} -N $node -b
done
""",
    )

    yield suspend_script, resume_script


@pytest.fixture(scope="function", autouse=True)
def powered_up_node():
    """Start every test with the node up and IDLE, and leave it that way."""

    atf.wait_for_node_state(node_name, "IDLE", fatal=True)
    state = atf.get_node_parameter(node_name, "state")
    if "POWERED_DOWN" in state or "POWERING_DOWN" in state:
        pytest.fail(f"{node_name} must start powered up (state={state})")

    yield

    # Drop the saved state so the next test starts from a clean slate
    atf.restart_slurmctld(clean=True)
    atf.start_slurmd(node_name, quiet=True)
    # wait_for_node_state matches by intersection, so an IDLE wait is also
    # satisfied by IDLE+POWERED_DOWN. Check the power flags explicitly, or a
    # teardown that left the node dirty only surfaces in the next test.
    atf.wait_for_node_state(node_name, "IDLE", fatal=True)
    state = atf.get_node_parameter(node_name, "state")
    if "POWERED_DOWN" in state or "POWERING_DOWN" in state:
        pytest.fail(f"{node_name} was left powered down by teardown (state={state})")


def power_down_node():
    """Ask the slurmctld to power the node down, stopping its slurmd."""

    atf.run_command(
        f"scontrol update nodename={node_name} state=POWER_DOWN",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node_name, "POWERING_DOWN", fatal=True)


def wait_for_powered_down_node():
    """Wait for the node to finish powering down.

    The node only leaves POWERING_DOWN once SuspendTimeout has expired, which
    is deliberately longer than atf's default polling timeout.
    """

    atf.wait_for_node_state(
        node_name, "POWERED_DOWN", timeout=suspend_timeout + 15, fatal=True
    )


def recover_node_state(method):
    """Make the slurmctld recover the node state from the state file.

    Neither path needs a wait afterwards. scontrol reconfigure only returns
    once the re-exec'd controller has recovered node state and told its parent
    it started, and atf.restart_slurmctld() polls scontrol ping itself.
    """

    if method == "reconfigure":
        atf.run_command(
            "scontrol reconfigure", user=atf.properties["slurm-user"], fatal=True
        )
    elif method == "restart":
        atf.restart_slurmctld()
    else:
        pytest.fail(f"Unknown node state recovery method: {method}")


def assert_idle_not_unknown(power_flag, method):
    state = atf.get_node_parameter(node_name, "state")

    assert "UNKNOWN" not in state, (
        f"{node_name} must not be UNKNOWN after a {method}. A {power_flag} node"
        f" has no slurmd to clear the UNKNOWN state, so it would be stranded"
        f" there (state={state})"
    )
    assert "IDLE" in state, f"{node_name} must be IDLE after a {method} (state={state})"
    # The power flag is not latched for the duration of the test: SuspendTimeout
    # is measured from the power down request and survives the recovery, so a
    # slow reconfigure or restart can let a POWERING_DOWN node finish powering
    # down before this runs. Say so, otherwise that reads as the regression.
    assert power_flag in state, (
        f"{node_name} must still be {power_flag} after a {method} (state={state})."
        f" If the other power flag is set instead, the node changed power state"
        f" before the recovery finished, which means SuspendTimeout is too short"
        f" for this runner rather than a regression"
    )


@xfail_missing_guard
@pytest.mark.parametrize("method", ["reconfigure", "restart"])
def test_powered_down_node_state(method):
    """Test that a POWERED_DOWN node is recovered as IDLE, not UNKNOWN."""

    power_down_node()
    wait_for_powered_down_node()

    recover_node_state(method)

    assert_idle_not_unknown("POWERED_DOWN", method)


@xfail_missing_guard
@pytest.mark.parametrize("method", ["reconfigure", "restart"])
def test_powering_down_node_state(method):
    """Test that a POWERING_DOWN node is recovered as IDLE, not UNKNOWN."""

    power_down_node()

    recover_node_state(method)

    assert_idle_not_unknown("POWERING_DOWN", method)


def test_down_node_stays_down():
    """Test that a DOWN node is recovered as DOWN, not resurrected as IDLE.

    The guard rewrites the base state to IDLE for any node that is still
    UNKNOWN when the recovered power flag is applied. A node whose saved base
    state was DOWN must not take that path: it has already been rewritten to
    DOWN by then, so the power flag is applied without touching the base state.
    That is the only branch of the guard the tests above never reach, and
    losing it would put an unusable node back into the schedulable pool.

    Not parametrized over the recovery method: both reach the same recovery
    path, which the tests above establish.
    """

    power_down_node()
    wait_for_powered_down_node()

    # ReturnToService only applies when a slurmd registers, which cannot happen
    # here - the SuspendProgram stopped it and nothing restarts it before the
    # assertions - so this case is unaffected by the value setup() pins.
    atf.run_command(
        f"scontrol update nodename={node_name} state=DOWN reason=test_141_5",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node_name, "DOWN", fatal=True)

    recover_node_state("restart")

    state = atf.get_node_parameter(node_name, "state")
    assert "DOWN" in state, (
        f"{node_name} must still be DOWN after a restart. Resurrecting it as"
        f" IDLE would return an unusable node to the schedulable pool"
        f" (state={state})"
    )
    assert (
        "IDLE" not in state
    ), f"{node_name} must not be rewritten to IDLE after a restart (state={state})"
    assert (
        "POWERED_DOWN" in state
    ), f"{node_name} must still be POWERED_DOWN after a restart (state={state})"


@xfail_missing_guard
def test_powered_down_node_is_allocatable():
    """Test that a POWERED_DOWN node can still be allocated after a reconfigure.

    This is what the UNKNOWN base state costs in practice: an UNKNOWN node is
    left out of the available node bitmaps, so a job asking for it is never
    given the node and the node power up never starts.
    """

    power_down_node()
    wait_for_powered_down_node()

    recover_node_state("reconfigure")
    assert_idle_not_unknown("POWERED_DOWN", "reconfigure")
    job_id = atf.submit_job_sbatch(f'-w {node_name} --wrap "hostname"', fatal=True)

    # POWERED_DOWN is cleared as soon as the slurmctld picks the node and
    # starts resuming it, so this covers the regression on its own: an UNKNOWN
    # node is never picked and keeps the flag.
    atf.wait_for_node_state(node_name, "POWERED_DOWN", reverse=True, fatal=True)

    # Waiting for the job proves the node was really allocated. Clearing
    # POWERED_DOWN alone does not: a resume that then fails clears the flag
    # too, and the node ends up DOWN with the job never run. It also lets the
    # ResumeProgram finish before teardown starts its own slurmd, which would
    # otherwise leave two slurmds racing for this node's port.
    atf.wait_for_job_state(job_id, "COMPLETED", timeout=resume_timeout + 60, fatal=True)
