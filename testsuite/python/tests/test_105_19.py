############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that nodes held for an expedited requeue stay held until it is done.

A three node job loses one node; one survivor's epilog finishes at once and
the other's waits on a gate, leaving the first held only by the requeue.
"""

import os
import pathlib

import pytest

import atf

NODE_COUNT = 3

SHARED_DIR = None
GATE_FILE = None
FAST_DONE_FILE = None
DRAINED_FILE = None

# How long the held node is watched; an unheld node would be released and
# scheduled well within this.
HOLD_WINDOW = 3
# Triggers are processed every 16 s (TRIGGER_INTERVAL + 1).
TRIGGER_DELAY = 19


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global SHARED_DIR, GATE_FILE, FAST_DONE_FILE, DRAINED_FILE

    SHARED_DIR = f"{atf.module_tmp_path}/shared"
    GATE_FILE = f"{SHARED_DIR}/gate"
    FAST_DONE_FILE = f"{SHARED_DIR}/fast_epilog_done"
    DRAINED_FILE = f"{SHARED_DIR}/drained"

    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51019: node holds were added in 26.11",
    )
    atf.require_version(
        (25, 11),
        component="bin/sbatch",
        reason="Issue 51019: sbatch --requeue=expedite was added in 25.11",
    )
    atf.require_version(
        (25, 11),
        component="bin/scontrol",
        reason="Issue 51019: the EXPEDITING job state was added in 25.11",
    )
    atf.require_nodes(NODE_COUNT)
    atf.require_config_parameter_includes(
        "SlurmctldParameters", "enable_expedited_requeue"
    )
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("batch_sched_delay", 0)
    )

    os.makedirs(SHARED_DIR)
    os.chmod(SHARED_DIR, 0o777)

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes(setup):
    """Return the node taken down, the fast node and the slow node."""

    failed, fast, slow = sorted(atf.nodes)[:NODE_COUNT]
    return failed, fast, slow


@pytest.fixture(scope="function")
def gate():
    """Hold the slow node's epilog until the test lets it go."""

    path = pathlib.Path(GATE_FILE)
    path.touch()
    os.chmod(GATE_FILE, 0o666)
    pathlib.Path(FAST_DONE_FILE).unlink(missing_ok=True)

    yield lambda: path.unlink(missing_ok=True)

    path.unlink(missing_ok=True)


@pytest.fixture(scope="function")
def epilog(nodes, gate):
    """Set an Epilog that finishes at once on the fast node only."""

    _, fast, _ = nodes
    prev_epilog = atf.get_config_parameter("Epilog")

    script = f"{SHARED_DIR}/epilog.sh"
    atf.make_bash_script(
        script,
        f"""#!/bin/bash
if [ "$SLURMD_NODENAME" = "{fast}" ]; then
    touch {FAST_DONE_FILE}
    exit 0
fi

# Capped in case a test fails.
deadline=$((SECONDS + 120))
while [ -e {GATE_FILE} ] && [ $SECONDS -lt $deadline ]; do
    sleep 0.2
done
exit 0
""",
    )
    os.chmod(script, 0o755)
    atf.set_config_parameter("Epilog", script)

    yield

    atf.set_config_parameter("Epilog", prev_epilog)
    pathlib.Path(script).unlink(missing_ok=True)


@pytest.fixture(scope="function", autouse=True)
def restore_nodes(setup):
    """Resume any node taken out of service and clear triggers."""

    yield

    for node, node_dict in atf.get_nodes(quiet=True, fatal=True).items():
        if {"DOWN", "DRAIN", "FAIL"} & set(node_dict["state"]):
            atf.run_command(
                f"scontrol update nodename={node} state=resume",
                user=atf.properties["slurm-user"],
                quiet=True,
            )
    atf.run_command(
        f"strigger --clear --user={atf.properties['slurm-user']}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def node_states(node):
    return set(atf.get_nodes(quiet=True, fatal=True)[node]["state"])


@pytest.fixture(scope="function")
def held(nodes, epilog, gate):
    """Return a job requeued with its fast node finished and held."""

    failed, fast, slow = nodes

    # Allow the requeued job to restart on the two nodes left.
    job_id = atf.submit_job_sbatch(
        f"--requeue=expedite -N{NODE_COUNT - 1}-{NODE_COUNT} "
        f'-w {failed},{fast},{slow} --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    atf.run_command(
        f"scontrol update nodename={failed} state=down reason=test_node_failure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.wait_for_file(FAST_DONE_FILE, fatal=True)

    return job_id, failed, fast, slow


def hold_is_kept(fast, window=HOLD_WINDOW):
    """Return whether the node stays COMPLETING for the whole window."""

    for _ in atf.timer(timeout=window, poll_interval=1, quiet=True):
        if "COMPLETING" not in node_states(fast):
            return False
    return True


def test_held_node_is_kept_until_requeued(held, gate):
    """Test that a held node is not given to another job before the requeue."""

    job_id, failed, fast, slow = held

    other_id = atf.submit_job_sbatch(f'-N1 -w {fast} --wrap "true"', fatal=True)

    assert hold_is_kept(
        fast
    ), f"{fast} left COMPLETING while it was held for the expedited requeue"
    assert (
        atf.get_job_parameter(other_id, "JobState", fatal=True) == "PENDING"
    ), f"Job {other_id} took {fast} while it was held for the expedited requeue"

    gate()

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} did not get its nodes back after the requeue"
    assert (
        int(atf.get_job_parameter(job_id, "Restarts", fatal=True)) > 0
    ), f"Job {job_id} is running its first time, not requeued"
    assert (
        atf.get_job_parameter(other_id, "JobState", fatal=True) == "PENDING"
    ), f"Job {other_id} was given {fast} ahead of the job it was held for"


def test_drain_of_held_node_waits_for_release(held, gate):
    """Test that a held node is only reported DRAINED once released."""

    job_id, failed, fast, slow = held

    program = f"{SHARED_DIR}/on_drained.sh"
    atf.make_bash_script(program, f'echo "$@" >> {DRAINED_FILE}\n')
    os.chmod(program, 0o755)
    pathlib.Path(DRAINED_FILE).unlink(missing_ok=True)
    atf.run_command(
        f"strigger --set --node={fast} --drained --program={program}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    atf.run_command(
        f"scontrol update nodename={fast} state=drain reason=test_held_drain",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    assert hold_is_kept(
        fast, TRIGGER_DELAY
    ), f"{fast} left COMPLETING while it was held"
    states = node_states(fast)
    assert (
        "DRAIN" in states
    ), f"{fast} should be draining while it is held, got {states}"
    assert (
        atf.run_command_exit(f"test -e {DRAINED_FILE}", quiet=True) != 0
    ), f"{fast} was reported DRAINED while it was still held"

    gate()

    assert atf.wait_for_node_state(
        fast, "COMPLETING", reverse=True
    ), f"{fast} stayed COMPLETING after it was released"
    assert atf.wait_for_file(
        DRAINED_FILE
    ), f"{fast} was never reported DRAINED once it was released"
    assert "DRAIN" in node_states(fast), f"{fast} lost its drain when it was released"


def test_held_node_taken_down_stays_down(held, gate):
    """Test that a held node taken down stays down when released."""

    job_id, failed, fast, slow = held

    atf.run_command(
        f"scontrol update nodename={fast} state=down reason=test_held_down",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "DOWN" in node_states(fast), f"{fast} was not taken down"

    gate()

    assert atf.wait_for_node_state(
        slow, "COMPLETING", reverse=True
    ), f"{slow} never finished completing after the requeue"
    assert atf.wait_for_job_state(
        job_id, "EXPEDITING"
    ), f"Job {job_id} was not requeued after a held node was taken down"
    assert "DOWN" in node_states(
        fast
    ), f"{fast} did not stay down after its hold was released"
    assert "COMPLETING" not in node_states(
        fast
    ), f"{fast} went back to COMPLETING when its hold was released"

    log_file = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    result = atf.run_command(
        f"grep -F underflow {log_file}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] in (
        0,
        1,
    ), f"Could not search {log_file}: {result['stderr']}"
    assert (
        "hold_cnt underflow" not in result["stdout"]
    ), f"Releasing the hold underflowed a hold count: {result['stdout']}"


def test_held_node_returned_to_service_stays_held(held, gate):
    """Test that a held node that goes down and is resumed stays held."""

    job_id, failed, fast, slow = held

    atf.run_command(
        f"scontrol update nodename={fast} state=down reason=test_held_bounce",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol update nodename={fast} state=resume",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    other_id = atf.submit_job_sbatch(f'-N1 -w {fast} --wrap "true"', fatal=True)

    assert hold_is_kept(
        fast
    ), f"{fast} left COMPLETING after it came back while it was held"
    assert (
        atf.get_job_parameter(other_id, "JobState", fatal=True) == "PENDING"
    ), f"Job {other_id} took {fast} after it came back while it was held"

    gate()

    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} did not get its nodes back after the requeue"
    assert (
        int(atf.get_job_parameter(job_id, "Restarts", fatal=True)) > 0
    ), f"Job {job_id} is running its first time, not requeued"
    assert (
        atf.get_job_parameter(other_id, "JobState", fatal=True) == "PENDING"
    ), f"Job {other_id} was given {fast} ahead of the job it was held for"
