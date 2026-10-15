############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmctld job and node states while switch/lua hooks are running."""

import os
import pathlib

import pytest

import atf

NODE_COUNT = 2

# Tests that observe a hold must drop the gate within this.
SWITCH_TIMEOUT = 10

# How long a hold must last to not be a slow epilog; must fit well inside
# SWITCH_TIMEOUT.
HOLD_PROOF = 2

# How long slurmctld may take to act on a report; short enough that the
# switch_timeout deadline cannot be what ends the wait.
REPORT_WAIT = 3

# Switch deadlines and retries are acted on every PERIODIC_TIMEOUT.
SWITCH_TICK = 30

# The job record must outlast a retried teardown.
MIN_JOB_AGE = 2 * SWITCH_TICK

SHARED_DIR = None
GATE_FILE = None
MARKER_DIR = None

DRAIN_REASON = "Switch job complete failed"

DIRECTIVE_PREFIX = "switchlua:"

switch_lua = f"""
local GATE = "@GATE_FILE@"
local MARKER_DIR = "@MARKER_DIR@"

-- Block while the gate file exists, capped in case a test fails.
local function hold()
    local deadline = os.time() + 120

    while os.time() < deadline do
        local gate = io.open(GATE, "r")

        if gate == nil then
            return
        end

        gate:close()

        for _ = 1, 200000 do end
    end
end

-- Record the sluid of a job_complete call; return the number of calls so far.
local function record(job)
    local path = MARKER_DIR .. "/complete." .. job.job_id
    local f = io.open(path, "a")

    if f ~= nil then
        f:write(tostring(job.sluid) .. "\\n")
        f:close()
    end

    local calls = 0

    f = io.open(path, "r")

    if f ~= nil then
        for _ in f:lines() do
            calls = calls + 1
        end
        f:close()
    end

    return calls
end

local function directive(network)
    if network == nil then
        return ""
    end

    return string.match(network, "{DIRECTIVE_PREFIX}([%w_]+)") or ""
end

-- Hold the nodes until the teardown reports (not the default).
function slurm_switch_block_job_complete()
    return true
end

function slurm_switch_job_start(job)
    local mode = directive(job.network)

    if mode == "decline" then
        return slurm.SUCCESS, nil
    end

    if
        mode == "start_hold"
        or mode == "start_timeout"
        or mode == "start_timeout_retry"
    then
        hold()
    end

    if mode == "start_fail" then
        return -1, nil
    end

    -- More than the 32768 entries a jobinfo can be saved with.
    if mode == "start_big" then
        local entries = {{}}

        for i = 1, 40000 do
            entries[i] = i
        end

        return slurm.SUCCESS, {{ entries = entries }}
    end

    return slurm.SUCCESS, {{ mode = mode }}
end

function slurm_switch_job_complete(job)
    local mode = directive(job.network)
    local calls = record(job)

    if mode == "complete_hold" or mode == "complete_timeout" then
        hold()
    end

    if mode == "complete_fail" then
        return -1
    end

    -- Ask to be retried once.
    if mode == "start_timeout_retry" and calls == 1 then
        return slurm.EAGAIN
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, GATE_FILE, MARKER_DIR

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    GATE_FILE = f"{SHARED_DIR}/gate"
    MARKER_DIR = f"{SHARED_DIR}/markers"

    atf.require_version(
        (26, 11), "sbin/slurmd", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 51019: switch/lua was added in 26.11"
    )
    atf.require_build_config(
        "HAVE_LUA",
        reason="Issue 51019: Slurm was built without lua, so switch/lua is absent",
    )
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter("SwitchType", "switch/lua")
    atf.require_config_parameter("MinJobAge", MIN_JOB_AGE)
    atf.require_config_parameter_includes(
        "SlurmctldParameters", ("switch_timeout", SWITCH_TIMEOUT)
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )
    atf.require_config_file(
        "switch.lua",
        switch_lua.replace("@GATE_FILE@", GATE_FILE).replace(
            "@MARKER_DIR@", MARKER_DIR
        ),
    )

    os.makedirs(MARKER_DIR)
    os.chmod(SHARED_DIR, 0o777)
    os.chmod(MARKER_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="function")
def gate():
    """Hold the script's hooks until the test calls the yielded release."""

    path = pathlib.Path(GATE_FILE)
    path.touch()
    os.chmod(GATE_FILE, 0o666)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


@pytest.fixture(scope="function")
def resume_drained_nodes():
    """Resume any node drained by a test."""

    yield

    for node, node_dict in atf.get_nodes(quiet=True).items():
        if {"DOWN", "DRAIN", "FAIL"} & set(node_dict["state"]):
            atf.run_command(
                f"scontrol update nodename={node} state=resume",
                user="root",
                quiet=True,
            )


def teardowns(job_id):
    """Return the sluid each slurm_switch_job_complete() call got for the job."""

    return atf.run_command_output(
        f"cat {MARKER_DIR}/complete.{job_id}", quiet=True
    ).split()


def complete_marker(job_id):
    """Return whether slurm_switch_job_complete() ran for the job."""

    return bool(teardowns(job_id))


def node_drained(node):
    """Return whether the node is drained, down or failed."""

    states = atf.get_nodes(quiet=True)[node]["state"]
    return bool({"DOWN", "DRAIN", "FAIL"} & set(states))


def test_job_start_holds_the_job_in_configuring(gate):
    """Test that a job stays CONFIGURING while job_start runs."""

    job_id = atf.submit_job_sbatch(
        f'-N1 --network={DIRECTIVE_PREFIX}start_hold --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", fatal=True
    ), f"Job {job_id} never reached CONFIGURING while job_start was running"

    # The job must stay CONFIGURING until the timer expires.
    for _ in atf.timer(timeout=HOLD_PROOF, poll_interval=0.5, xfail=True):
        state = atf.get_job_parameter(job_id, "JobState", fatal=True, quiet=True)
        assert state == "CONFIGURING", (
            f"Job {job_id} left CONFIGURING for {state} while"
            " slurm_switch_job_start() was still running"
        )

    gate()

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} did not run once slurm_switch_job_start() returned"


def wait_for_requeue(job_id, timeout=atf.default_polling_timeout):
    """Wait for a job to be requeued and pending again."""

    for _ in atf.timer(timeout=timeout, poll_interval=1):
        restarts = atf.get_job_parameter(job_id, "Restarts", quiet=True)
        if restarts and int(restarts) > 0:
            return atf.wait_for_job_state(job_id, "PENDING")

    return False


def test_job_start_failure_requeues_the_job():
    """Test that a negative return from job_start requeues the job."""

    job_id = atf.submit_job_sbatch(
        f'-N1 --requeue --network={DIRECTIVE_PREFIX}start_fail --wrap "true"',
        fatal=True,
    )

    # Within the deadline, as a timed out setup is also requeued.
    assert wait_for_requeue(job_id, timeout=SWITCH_TIMEOUT // 2), (
        f"Job {job_id} was not requeued after slurm_switch_job_start()"
        f" returned -1: {atf.get_job_parameter(job_id, 'JobState')}"
    )


def test_job_start_timeout_requeues_the_job(gate, resume_drained_nodes):
    """Test that a job_start timeout requeues the job and tears it down."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node} --requeue"
        f' --network={DIRECTIVE_PREFIX}start_timeout --wrap "true"',
        fatal=True,
    )

    assert wait_for_requeue(job_id, timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20), (
        f"Job {job_id} was not requeued after slurm_switch_job_start() failed"
        f" to report within switch_timeout={SWITCH_TIMEOUT}"
    )

    for t in atf.timer():
        if complete_marker(job_id):
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() never ran for the abandoned setup of job {job_id}"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL
    ), f"{node} did not come back after the abandoned setup was torn down"

    assert not node_drained(
        node
    ), f"{node} was drained for a setup timeout, which owes a teardown but no drain"


def test_job_start_jobinfo_too_large_fails_and_tears_down_the_setup(
    resume_drained_nodes,
):
    """Test that a jobinfo too large to save fails and tears down the setup."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --no-requeue --network={DIRECTIVE_PREFIX}start_big --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "CANCELLED", desired_reason="switch_setup_failed"
    ), (
        f"Job {job_id} was not killed with the switch setup failure as its"
        " reason after slurm_switch_job_start() returned a jobinfo that is too"
        " large to save"
    )

    # The marker is written before the teardown releases the node.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} did not come back after the refused setup was torn down"

    assert complete_marker(job_id), (
        f"slurm_switch_job_complete() never ran for job {job_id}, whose setup"
        " succeeded before its jobinfo was refused"
    )

    assert not node_drained(node), f"{node} was drained for a teardown that succeeded"


def test_job_start_timeout_kills_a_job_that_cannot_be_requeued(
    gate, resume_drained_nodes
):
    """Test that a job_start timeout kills the job and its setup call."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --no-requeue --network={DIRECTIVE_PREFIX}start_timeout --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id,
        "CANCELLED",
        desired_reason="switch_setup_timed_out",
        timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20,
    ), (
        f"Job {job_id} was not killed after slurm_switch_job_start() failed to"
        f" report within switch_timeout={SWITCH_TIMEOUT}"
    )

    for t in atf.timer():
        if complete_marker(job_id):
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() never ran for the abandoned setup of job {job_id}"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL
    ), f"{node} did not come back after the abandoned setup was torn down"

    assert not node_drained(
        node
    ), f"{node} was drained for a setup timeout, which owes a teardown but no drain"

    sluids = teardowns(job_id)
    assert len(sluids) == 1, (
        f"slurm_switch_job_complete() ran {len(sluids)} times for job {job_id}"
        " before its abandoned setup reported"
    )

    gate()

    # The setup was killed at switch_timeout, so it cannot report late.
    for t in atf.timer(timeout=REPORT_WAIT, xfail=True):
        if len(teardowns(job_id)) > 1:
            pytest.fail(
                f"The setup of job {job_id} reported after it should have been killed"
            )


def test_job_ended_during_setup_is_torn_down_at_the_deadline(
    gate, resume_drained_nodes
):
    """Test that a job ended during a timed out setup is torn down, not drained."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}start_timeout --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", fatal=True
    ), f"Job {job_id} never reached CONFIGURING while job_start was running"

    atf.run_command(f"scancel {job_id}", fatal=True)

    for _ in atf.timer(timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20, poll_interval=1):
        if complete_marker(job_id):
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() never ran for job {job_id}, which"
            " ended while a setup that never reported was outstanding"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL
    ), f"{node} did not come back after the abandoned setup was torn down"

    assert not node_drained(
        node
    ), f"{node} was drained for a setup timeout, which owes a teardown but no drain"


def test_job_ended_during_setup_is_torn_down_when_the_setup_reports(
    gate, resume_drained_nodes
):
    """Test that a job ended during setup is torn down once the setup reports."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}start_hold --wrap "true"',
        fatal=True,
    )

    # The deadline is running; the gate must be dropped within it.
    assert atf.wait_for_job_state(
        job_id, "CONFIGURING", poll_interval=0.2, fatal=True
    ), f"Job {job_id} never reached CONFIGURING while job_start was running"

    atf.run_command(f"scancel {job_id}", fatal=True)

    assert atf.wait_for_node_state(
        node, "COMPLETING", fatal=True
    ), f"{node} was not held for the setup of job {job_id}, which had ended"

    for t in atf.timer(timeout=HOLD_PROOF, xfail=True):
        if complete_marker(job_id):
            pytest.fail(
                f"slurm_switch_job_complete() ran for job {job_id} while its setup"
                " was still running"
            )

    gate()

    for t in atf.timer(timeout=REPORT_WAIT, poll_interval=0.2):
        if complete_marker(job_id):
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() did not run for job {job_id} when its"
            " setup reported after the job had ended"
        )

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL
    ), f"{node} was not released after the setup of job {job_id} was torn down"

    assert not node_drained(
        node
    ), f"{node} was drained though its setup reported and was torn down"


def test_teardown_of_an_abandoned_setup_is_retried(gate, resume_drained_nodes):
    """Test that the teardown of a timed out setup can be retried."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node} --no-requeue"
        f' --network={DIRECTIVE_PREFIX}start_timeout_retry --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id,
        "CANCELLED",
        desired_reason="switch_setup_timed_out",
        timeout=SWITCH_TIMEOUT + 2 * SWITCH_TICK + 20,
    ), (
        f"Job {job_id} was not killed after slurm_switch_job_start() failed to"
        f" report within switch_timeout={SWITCH_TIMEOUT}"
    )

    # The retry comes a SWITCH_TICK after the deadline, hence the second
    # SWITCH_TICK above.
    for t in atf.timer():
        if len(teardowns(job_id)) >= 2:
            break
    else:
        pytest.fail(
            f"slurm_switch_job_complete() was not called again for job {job_id}"
            f" after it returned slurm.EAGAIN ({len(teardowns(job_id))} calls)"
        )

    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL
    ), f"{node} was not released after the retried teardown of job {job_id}"

    assert not node_drained(
        node
    ), f"{node} was drained though the retried teardown succeeded"

    assert (
        len(teardowns(job_id)) == 2
    ), "slurm_switch_job_complete() was called more often than it asked for"


def test_job_complete_holds_the_nodes(gate, resume_drained_nodes):
    """Test that a completing job holds its nodes until job_complete reports."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}complete_hold --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETING", fatal=True
    ), f"Job {job_id} never reached COMPLETING"

    assert atf.wait_for_node_state(
        node, "COMPLETING", fatal=True
    ), f"{node} was not held while slurm_switch_job_complete() was running"

    assert not atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=HOLD_PROOF, poll_interval=0.5
    ), (
        f"Job {job_id} finished completing while"
        " slurm_switch_job_complete() was still running"
    )

    gate()

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} stayed completing after slurm_switch_job_complete() returned"

    # Exact, as a held or drained node also reports IDLE.
    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} was not released after slurm_switch_job_complete() returned"


def test_job_complete_failure_drains_the_allocation(resume_drained_nodes):
    """Test that a negative return from job_complete drains the allocation."""

    nodes = atf.run_job_nodes(
        f"-N{NODE_COUNT} --network={DIRECTIVE_PREFIX}complete_fail true",
        fatal=True,
    )
    assert len(nodes) == NODE_COUNT, f"Expected {NODE_COUNT} nodes, got {nodes}"

    # Within the deadline, as a timed out teardown drains with the same reason.
    assert atf.wait_for_node_state(
        nodes, "DRAIN", timeout=REPORT_WAIT, fatal=True
    ), f"{nodes} were not all drained after slurm_switch_job_complete() returned -1"

    for node in nodes:
        reason = atf.get_node_parameter(node, "reason") or ""
        assert (
            DRAIN_REASON in reason
        ), f"{node} was drained with an unexpected reason: {reason!r}"


def test_job_complete_timeout_drains_the_allocation(gate, resume_drained_nodes):
    """Test that a job_complete timeout drains the allocation."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}complete_timeout --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETING", fatal=True
    ), f"Job {job_id} never reached COMPLETING"

    assert atf.wait_for_node_state(
        node,
        "DRAIN",
        fatal=True,
        timeout=SWITCH_TIMEOUT + SWITCH_TICK + 20,
    ), (
        f"{node} was not drained after slurm_switch_job_complete() failed to"
        f" report within switch_timeout={SWITCH_TIMEOUT}"
    )

    reason = atf.get_node_parameter(node, "reason") or ""
    assert (
        DRAIN_REASON in reason
    ), f"{node} was drained with an unexpected reason: {reason!r}"


def test_declined_job_does_not_reach_job_complete():
    """Test that a declined job is not torn down and does not hold its nodes."""

    node = sorted(atf.nodes)[0]
    job_id = atf.submit_job_sbatch(
        f'-N1 -w {node} --network={DIRECTIVE_PREFIX}decline --wrap "true"',
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} did not complete"

    assert atf.wait_for_node_state(
        node, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True
    ), f"{node} was held back for a job slurm_switch_job_start() declined"

    assert not complete_marker(job_id), (
        "slurm_switch_job_complete() ran for a job slurm_switch_job_start()"
        " declined, so the plugin had nothing set up to tear down"
    )


def test_managed_job_reaches_job_complete():
    """Test that a managed job is torn down."""

    job_id = atf.submit_job_sbatch('-N1 --network=managed --wrap "true"', fatal=True)

    assert atf.wait_for_job_state(
        job_id, "COMPLETED", fatal=True
    ), f"Job {job_id} did not complete"

    for t in atf.timer():
        if complete_marker(job_id):
            break
    else:
        pytest.fail(f"slurm_switch_job_complete() never ran for managed job {job_id}")
