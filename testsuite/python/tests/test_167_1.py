############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SLURM_NODE_IS_HEALTHY reporting to HealthCheckProgram."""

import os

import pytest

import atf

pytestmark = pytest.mark.slow

# Short interval so periodic health checks fire quickly. The timeout must be
# strictly less than the interval (slurmctld logs an error otherwise).
hc_interval = 10
hc_timeout = 5

resv_name = "resv_167_1"


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup(hc_dir):
    atf.require_version(
        (26, 5, 2),
        "sbin/slurmd",
        reason="Issue 50799: SLURM_NODE_IS_HEALTHY is set by 26.05.2+ slurmd",
    )
    atf.require_nodes(3)
    # TODO: Issue 50799 - remove when 26.05 is no longer supported. Health check
    # reports were enabled 26.05.2+ in Slurm, though it wasn't by default and
    # was a hidden option. From 26.11 slurmd always reports.
    if atf.get_version("sbin/slurmd") < (26, 11):
        atf.require_config_parameter_includes(
            "SlurmctldParameters", "health_check_report"
        )

    # The recorder writes the value of SLURM_NODE_IS_HEALTHY to a per-node file
    # each time it runs, substituting "unset" only when the variable is absent
    # so that an empty value stays distinguishable. The record is moved into
    # place so a reader never observes a partial write.
    recorder = f"{hc_dir}/hc_recorder.sh"
    record = f"{hc_dir}/health_${{SLURMD_NODENAME}}"
    atf.make_bash_script(
        recorder,
        f'echo "${{SLURM_NODE_IS_HEALTHY-unset}}" > "{record}.tmp"\n'
        f'mv "{record}.tmp" "{record}"\n',
    )
    atf.require_config_parameter("HealthCheckProgram", recorder)
    atf.require_config_parameter("HealthCheckInterval", hc_interval)
    atf.require_config_parameter("HealthCheckTimeout", hc_timeout)

    # The rebooter restarts the node's slurmd in place. slurmd runs
    # RebootProgram with SLURM_NODE_NAME set, so one script serves every node.
    pid_file = atf.get_config_parameter(
        "SlurmdPidFile", live=False, quiet=True
    ).replace("%n", "$SLURM_NODE_NAME")
    rebooter = f"{hc_dir}/rebooter.sh"
    atf.make_bash_script(
        rebooter,
        f'slurmd_pid=$(<"{pid_file}")\n'
        f"slurmd_start_cmd=$(ps -p $slurmd_pid -o cmd=)\n"
        f"kill $slurmd_pid\n"
        f"($slurmd_start_cmd -b)\n",
    )
    atf.require_config_parameter("RebootProgram", rebooter)
    # Require unset so every test starts from the default (ANY);
    # restore_state resets it after any test that changes it.
    atf.require_config_parameter("HealthCheckNodeState", None)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def hc_dir():
    """Absolute path of the directory holding the recorder and its records.

    Resolved once so nothing depends on the working directory a test runs in,
    which is not the one this fixture is created in.
    """
    hc_dir = os.path.abspath("hc_records")
    os.makedirs(hc_dir, exist_ok=True)
    yield hc_dir


@pytest.fixture(autouse=True)
def restore_state():
    """Resume drained nodes and clear per-test config after each test."""
    yield
    # Deleting the reservation is what clears MAINTENANCE, so a silent failure
    # here would leave a node no resume below can recover.
    if resv_name in atf.get_reservations(quiet=True):
        atf.run_command(
            f"scontrol delete reservation {resv_name}", user="root", fatal=True
        )
    # Resuming a node that needs no resume is an invalid state transition.
    for node, node_dict in atf.get_nodes().items():
        if {"DOWN", "DRAIN", "FAIL"} & set(node_dict["state"]):
            atf.run_command(
                f"scontrol update nodename={node} state=resume",
                user="root",
                fatal=True,
            )
    # Each reset rewrites slurm.conf and reconfigures, which restarts the
    # health check interval, so only pay for it when a test set the value.
    if (
        atf.get_config_parameter("HealthCheckNodeState", live=False, quiet=True)
        is not None
    ):
        atf.set_config_parameter("HealthCheckNodeState", None)


def record_file(hc_dir, node):
    return f"{hc_dir}/health_{node}"


def recorded_health(hc_dir, node):
    """Return the value the recorder last wrote for node, or None if absent."""
    record = record_file(hc_dir, node)
    if not os.path.isfile(record):
        return None
    result = atf.run_command(f"cat {record}", user="root", fatal=True, quiet=True)
    return result["stdout"].strip()


def clear_recorders(hc_dir, nodes):
    for node in nodes:
        record = record_file(hc_dir, node)
        atf.run_command(
            f"rm -f {record} {record}.tmp", user="root", fatal=True, quiet=True
        )


def wait_recorded(hc_dir, nodes):
    """Return {node: value} for the first record written since the last clear."""
    values = {}
    for node in nodes:
        atf.wait_for_file(record_file(hc_dir, node), fatal=True)
        values[node] = recorded_health(hc_dir, node)
    return values


def assert_no_periodic_check(hc_dir, node, node_state):
    """Assert node runs no periodic health check under node_state.

    A sweep dispatched under the previous configuration is absorbed and
    discarded first, so only a sweep under node_state can fail the assert.
    """
    clear_recorders(hc_dir, [node])
    atf.wait_for_file(record_file(hc_dir, node), timeout=hc_interval, xfail=True)
    clear_recorders(hc_dir, [node])
    assert not atf.wait_for_file(
        record_file(hc_dir, node), timeout=hc_interval * 2, xfail=True
    ), f"node {node} ran a periodic health check under {node_state}"


def test_healthy_and_unhealthy_reported(hc_dir):
    """Every node HealthCheckNodeState selects runs HealthCheckProgram and
    receives SLURM_NODE_IS_HEALTHY: yes for a healthy node and no for a drained
    one."""

    nodes = sorted(atf.get_nodes())
    # All nodes are selected by default; drain the last one so the sweep
    # reports both healthy and unhealthy verdicts at once.
    healthy_nodes, unhealthy_node = nodes[:-1], nodes[-1]

    atf.run_command(
        f"scontrol update nodename={unhealthy_node} state=drain reason=test_167_1",
        user="root",
        fatal=True,
    )
    atf.wait_for_node_state(unhealthy_node, "DRAIN", fatal=True)

    # Discard one round so a check dispatched before the drain cannot be
    # mistaken for the verdict under test.
    clear_recorders(hc_dir, nodes)
    wait_recorded(hc_dir, nodes)

    clear_recorders(hc_dir, nodes)
    values = wait_recorded(hc_dir, nodes)

    for node in healthy_nodes:
        assert (
            values[node] == "yes"
        ), f"healthy node {node} recorded {values[node]!r}, expected 'yes'"
    assert (
        values[unhealthy_node] == "no"
    ), f"drained node {unhealthy_node} recorded {values[unhealthy_node]!r}, expected 'no'"


def make_down(node):
    # slurmd stays up, so the node keeps answering health checks while DOWN.
    atf.run_command(
        f"scontrol update nodename={node} state=down reason=test_167_1",
        user="root",
        fatal=True,
    )
    atf.wait_for_node_state(node, "DOWN", fatal=True)


def make_draining(node):
    """Drain node while a job runs on it, which leaves it DRAINING."""
    job_id = atf.submit_job_sbatch(f'-w {node} --wrap "sleep infinity"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(
        f"scontrol update nodename={node} state=drain reason=test_167_1",
        user="root",
        fatal=True,
    )
    atf.wait_for_node_state(node, "DRAIN", fatal=True)


def make_failing(node):
    atf.run_command(
        f"scontrol update nodename={node} state=fail reason=test_167_1",
        user="root",
        fatal=True,
    )
    atf.wait_for_node_state(node, "FAIL", fatal=True)


def make_maint(node):
    atf.run_command(
        f"scontrol create reservation reservationname={resv_name}"
        f" starttime=now duration=1:00:00 user=root nodes={node} flags=MAINT",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node, "MAINTENANCE", fatal=True)


@pytest.mark.parametrize(
    "make_unhealthy",
    [make_down, make_draining, make_failing, make_maint],
    ids=["down", "draining", "failing", "maint"],
)
def test_unhealthy_states_reported(hc_dir, make_unhealthy):
    """SLURM_NODE_IS_HEALTHY is no for each unhealthy condition slurm.conf(5)
    documents. Drained is covered by test_healthy_and_unhealthy_reported; this
    covers down, draining, failing and in a maintenance reservation."""

    nodes = sorted(atf.get_nodes())
    healthy_node, unhealthy_node = nodes[0], nodes[-1]

    make_unhealthy(unhealthy_node)

    # Discard one round so a check dispatched before the state change cannot
    # be mistaken for the verdict under test.
    clear_recorders(hc_dir, nodes)
    wait_recorded(hc_dir, nodes)

    clear_recorders(hc_dir, nodes)
    values = wait_recorded(hc_dir, [healthy_node, unhealthy_node])

    assert (
        values[unhealthy_node] == "no"
    ), f"unhealthy node {unhealthy_node} recorded {values[unhealthy_node]!r}, expected 'no'"
    assert (
        values[healthy_node] == "yes"
    ), f"healthy node {healthy_node} recorded {values[healthy_node]!r}, expected 'yes'"


def make_allocated(node):
    """Run a job on node so it is in service and busy rather than IDLE.

    A one-CPU job leaves a multi-CPU node MIXED rather than ALLOCATED, and
    both callers only need the node to be busy.
    """
    job_id = atf.submit_job_sbatch(f'-w {node} --wrap "sleep infinity"', fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.wait_for_node_state(node, ["ALLOCATED", "MIXED"], fatal=True)


def make_reserved(node):
    """Reserve node without the MAINT flag, which leaves it in service."""
    atf.run_command(
        f"scontrol create reservation reservationname={resv_name}"
        f" starttime=now duration=1:00:00 user=root nodes={node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.wait_for_node_state(node, "RESERVED", fatal=True)


@pytest.mark.parametrize(
    "make_healthy",
    [make_allocated, make_reserved],
    ids=["allocated", "reserved"],
)
def test_healthy_states_reported(hc_dir, make_healthy):
    """SLURM_NODE_IS_HEALTHY is yes for a node that is in service but not
    plain IDLE. slurm.conf(5) enumerates the unhealthy conditions, so an
    allocated node and a node in a reservation without MAINT are both
    healthy, and only the MAINT flag makes a reserved node unhealthy."""

    nodes = sorted(atf.get_nodes())
    healthy_node = nodes[-1]

    make_healthy(healthy_node)

    # Discard one round so a check dispatched before the state change cannot
    # be mistaken for the verdict under test.
    clear_recorders(hc_dir, nodes)
    wait_recorded(hc_dir, nodes)

    clear_recorders(hc_dir, nodes)
    values = wait_recorded(hc_dir, [healthy_node])

    assert (
        values[healthy_node] == "yes"
    ), f"in-service node {healthy_node} recorded {values[healthy_node]!r}, expected 'yes'"


def test_unselected_node_not_reported(hc_dir):
    """A node that node selection excludes never runs HealthCheckProgram, so it
    gets no SLURM_NODE_IS_HEALTHY record at all rather than a no verdict. This
    covers the HealthCheckNodeState exclusion; NOT_RESPONDING nodes are
    excluded separately (slurm.conf(5) HealthCheckProgram)."""

    nodes = sorted(atf.get_nodes())
    checked_node, drained_node, busy_node = nodes[0], nodes[1], nodes[2]
    excluded_nodes = [drained_node, busy_node]

    # NONDRAINED_IDLE selects only non-drained IDLE nodes, so a drained node
    # and a busy one are both dropped by node selection before any health
    # verdict is computed.
    atf.set_config_parameter("HealthCheckNodeState", "NONDRAINED_IDLE")
    atf.run_command(
        f"scontrol update nodename={drained_node} state=drain reason=test_167_1",
        user="root",
        fatal=True,
    )
    atf.wait_for_node_state(drained_node, "DRAIN", fatal=True)
    make_allocated(busy_node)

    # Sync to a sweep boundary under the new config so any sweep that was
    # in flight under the previous config can't taint the check below.
    clear_recorders(hc_dir, [checked_node])
    values = wait_recorded(hc_dir, [checked_node])
    assert (
        values[checked_node] == "yes"
    ), f"non-drained node {checked_node} recorded {values[checked_node]!r}"

    # slurmctld dispatches the healthy and unhealthy nodes as separate agent
    # requests, so a single checked_node record does not prove the excluded
    # node's sweep is over. Two consecutive records bracket a whole sweep.
    clear_recorders(hc_dir, [checked_node] + excluded_nodes)
    for _ in range(2):
        values = wait_recorded(hc_dir, [checked_node])
        assert (
            values[checked_node] == "yes"
        ), f"non-drained node {checked_node} recorded {values[checked_node]!r}"
        clear_recorders(hc_dir, [checked_node])

    for node in excluded_nodes:
        value = recorded_health(hc_dir, node)
        assert (
            value is None
        ), f"excluded node {node} ran a health check (recorded {value!r})"


def test_reboot_verdict_reported(hc_dir):
    """The health check REBOOT_ONLY runs after a reboot gets a
    SLURM_NODE_IS_HEALTHY verdict rather than the unset value slurm.conf(5)
    documents for a run that happens before the node registers.

    Under REBOOT_ONLY slurmd skips its own startup run, so this check is the
    only writer of the record. The verdict itself is not pinned: it reflects
    the state the node registers in, which varies with ReturnToService and
    with whether the node was marked down while it was away.
    """

    node = sorted(atf.get_nodes())[0]

    atf.set_config_parameter("HealthCheckNodeState", "REBOOT_ONLY")

    # Without this the record asserted below could come from a sweep rather
    # than from the reboot check.
    assert_no_periodic_check(hc_dir, node, "REBOOT_ONLY")

    atf.run_command(f"scontrol reboot {node}", user="root", fatal=True)

    # A full reboot cycle (slurmd restart, registration, then the health
    # check dispatch) takes longer than the default polling timeout allows.
    atf.wait_for_file(record_file(hc_dir, node), fatal=True, timeout=120)
    value = recorded_health(hc_dir, node)
    assert value in (
        "yes",
        "no",
    ), f"node {node} recorded {value!r} after a reboot, expected a verdict"


def test_unset_before_registration(hc_dir):
    """The startup execution of HealthCheckProgram runs before the node
    registers with slurmctld, so SLURM_NODE_IS_HEALTHY is unset. The other
    documented unset case, a node that does not support the variable, isn't
    covered here: setup() requires slurmd 26.05.2+ on every node, so no node
    in this test ever lacks the variable."""

    node = sorted(atf.get_nodes())[0]

    # START_ONLY stops the periodic sweep, so the startup execution is the only
    # thing that writes a record.
    atf.set_config_parameter("HealthCheckNodeState", "START_ONLY")
    assert_no_periodic_check(hc_dir, node, "START_ONLY")
    atf.start_slurmd(node)

    values = wait_recorded(hc_dir, [node])
    assert (
        values[node] == "unset"
    ), f"node {node} recorded {values[node]!r} at startup, expected 'unset'"
