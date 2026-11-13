############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test broadcast overlap, queued-response fencing, and restart recovery."""

import re
import shlex
from contextlib import contextmanager

import pytest

import atf

pytestmark = pytest.mark.slow

SLURMD_TIMEOUT = 15
PING_INTERVAL = SLURMD_TIMEOUT // 3
# Keep the default 60-second HC timeout: older shared-config readers do not
# understand HealthCheckTimeout. Leave time for HC inside a stopped ping RPC.
HC_INTERVAL = 65
MESSAGE_TIMEOUT = 50
PING_RPCS = ("REQUEST_PING", "REQUEST_NODE_REGISTRATION_STATUS")
HC_RPCS = ("REQUEST_HEALTH_CHECK", "REQUEST_NODE_HEALTH_CHECK")
ACCT_RPCS = ("REQUEST_ACCT_GATHER_UPDATE",)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51187: concurrent broadcast cycles",
    )
    atf.require_nodes(2)
    atf.require_config_parameter("SlurmdTimeout", SLURMD_TIMEOUT)
    atf.require_config_parameter("SlurmctldDebug", "debug")
    atf.require_config_parameter_includes("DebugFlags", "Agent")
    atf.require_config_parameter("MessageTimeout", MESSAGE_TIMEOUT)
    atf.require_config_parameter("AcctGatherNodeFreq", HC_INTERVAL)
    atf.require_config_parameter("HealthCheckInterval", HC_INTERVAL)
    atf.require_config_parameter("HealthCheckTimeout", None)
    atf.require_config_parameter("HealthCheckNodeState", "ANY")
    atf.require_config_parameter("HealthCheckProgram", "/bin/true")
    atf.require_config_parameter("ReturnToService", 0)
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def restore_state():
    """Administratively recover any DOWN nodes."""
    yield
    if not atf.is_slurmctld_running(quiet=True):
        atf.start_slurmctld()
    for node in atf.get_nodes(fatal=True):
        resume_node(node)
    atf.require_slurm_running()


def read_log():
    """Read the controller log; missing or unreadable logs must fail."""
    path = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    return atf.run_command_output(
        f"awk '!/AGENT: agent_trigger:/' {shlex.quote(path)}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def log_since(offset):
    text = read_log()
    assert len(text) >= offset, "controller log truncated during observation"
    return text[offset:]


def sent_requests(text, rpc_types, node=None):
    """Find actual agent sends, optionally requiring a specific target node."""
    requests = []
    for match in re.finditer(r"_thread_per_group_rpc: sending (\w+) to (\S+)", text):
        rpc_type, hosts = match.groups()
        if rpc_type not in rpc_types:
            continue
        if node is None or node in atf.node_range_to_list(hosts):
            requests.append((match.start(), rpc_type))
    return requests


def agent_counts(text, rpc_types):
    """Count agents in log order from controller startup, recording class drains."""
    pending = 0
    drains = []
    for match in re.finditer(r"agent: (New|end) agent [^\n]*msg_type:(\w+)", text):
        event, rpc_type = match.groups()
        if rpc_type not in rpc_types:
            continue
        pending += 1 if event == "New" else -1
        assert pending >= 0, f"unmatched {rpc_type} agent completion"
        if not pending:
            drains.append(match.end())
    return pending, drains


def completed(text, rpc_types, node=None, after=0):
    """Find class drains following a new send, not an arbitrary sibling's end."""
    _, drains = agent_counts(text, rpc_types)
    sends = sent_requests(text, rpc_types, node)
    cycles = []
    for position in drains:
        if any(after <= send < position for send, _ in sends):
            cycles.append(position)
        after = max(after, position)
    return cycles


def outstanding(text, rpc_types, after):
    """Require that the class has not drained since the stopped-node send."""
    pending, drains = agent_counts(text, rpc_types)
    return pending > 0 and not any(position > after for position in drains)


def wait_outstanding(offset, after, rpc_types, node, timeout):
    """Require a new send to a stopped daemon while its class remains active."""
    # HC can need a full 65-second interval; sample sends each second.
    for _ in atf.timer(timeout=timeout, poll_interval=1, fatal=True):
        text = log_since(offset)
        for position, _ in sent_requests(text, rpc_types, node):
            if position >= after - offset and outstanding(text, rpc_types, position):
                return position


def signal_slurmd(node, signal):
    pids = atf.slurmd_pids(node)
    assert pids, f"no slurmd running for {node}"
    atf.run_command(
        f"kill -{signal} {' '.join(str(pid) for pid in pids)}",
        user="root",
        fatal=True,
        quiet=True,
    )


def resume_node(node):
    if "DOWN" in atf.get_nodes(fatal=True)[node]["state"]:
        atf.run_command(
            f"scontrol update nodename={node} state=resume",
            user="root",
            fatal=True,
        )
    atf.wait_for_node_state(node, ["DOWN", "NOT_RESPONDING"], reverse=True, fatal=True)


@contextmanager
def stopped_slurmd(node):
    """Own the stopped daemon and restore it even after assertion failures."""
    signal_slurmd(node, "STOP")
    try:
        yield
    finally:
        # A stopped slurmd cannot act on SIGTERM, so kill it directly rather
        # than let start_slurmd() wait out its TERM timeout first.
        atf.stop_slurmd(node, signal="KILL", quiet=True)
        # FUTURE nodes must be resumed before slurmd can register again.
        if "FUTURE" in atf.get_nodes(fatal=True)[node]["state"]:
            atf.run_command(
                f"scontrol update nodename={node} state=resume",
                user="root",
                fatal=True,
            )
        # This also registers the node again; ReturnToService=0 needs RESUME.
        atf.start_slurmd(node, quiet=True)
        resume_node(node)


def restart_observed(rpc_types=PING_RPCS):
    """Restart and observe a completed broadcast before disrupting its next run."""
    offset = len(read_log())
    atf.restart_slurmctld()
    # Exclude the previous controller's shutdown; count every new agent.
    offset += log_since(offset).rindex("slurmctld version ")
    # Periodic HC begins after 65 seconds, beyond the default polling timeout.
    for _ in atf.timer(timeout=HC_INTERVAL + 20, poll_interval=1, fatal=True):
        if completed(log_since(offset), rpc_types):
            break
    return offset


def test_agent_completion_accounting():
    """A sibling end must not finish a held request or hide an intervening drain."""
    start = "agent: New agent msg_type:REQUEST_PING\n"
    end = "agent: end agent msg_type:REQUEST_PING\n"
    text = start * 2 + "_thread_per_group_rpc: sending REQUEST_PING to node1\n" + end
    request = text.index("_thread_per_group_rpc")
    assert agent_counts(text, PING_RPCS) == (1, []), "sibling end drained held ping"
    assert outstanding(text, PING_RPCS, request), "sibling end cleared held request"
    assert not completed(text, PING_RPCS), "sibling end counted as class completion"
    text += end + start
    assert not outstanding(text, PING_RPCS, request), "intervening drain went unnoticed"
    assert len(completed(text, PING_RPCS)) == 1, "expected exactly one complete cycle"
    text += "_thread_per_group_rpc: sending REQUEST_PING to node1\n"
    held_request = text.rindex("_thread_per_group_rpc")
    assert not outstanding(text, PING_RPCS, request), "drained candidate became pending"
    assert outstanding(text, PING_RPCS, held_request), "later held candidate was lost"


def test_health_check_and_acct_gather_unblocked_by_stuck_ping():
    """Both other classes must complete inside a confirmed pending ping."""
    offset = restart_observed()
    nodes = sorted(atf.get_nodes(fatal=True))
    live_node, stopped_node = nodes[0], nodes[-1]
    after = len(read_log())
    with stopped_slurmd(stopped_node):
        request = wait_outstanding(
            offset,
            after,
            PING_RPCS,
            stopped_node,
            max(PING_INTERVAL * 3, atf.default_polling_timeout),
        )
        # Preserve the already-sent ping, but exclude the stopped node from
        # subsequent HC/accounting broadcasts so they can actually complete.
        atf.run_command(
            f"scontrol update nodename={stopped_node} state=NoResp",
            user="root",
            fatal=True,
        )

        def progressed(text):
            assert outstanding(
                text, PING_RPCS, request
            ), "ping ended before class progress"
            return completed(text, HC_RPCS, live_node, after=request) and completed(
                text, ACCT_RPCS, live_node, after=request
            )

        # Allow a full periodic interval while proving ping remains outstanding.
        for _ in atf.timer(timeout=HC_INTERVAL + 15, poll_interval=1, fatal=True):
            if progressed(log_since(offset)):
                break


def test_cycles_resume_across_restart():
    """Shutdown during a pending HC must leave all classes usable on restart."""
    offset = restart_observed(HC_RPCS)
    node = sorted(atf.get_nodes(fatal=True))[-1]
    after = len(read_log())
    with stopped_slurmd(node):
        wait_outstanding(offset, after, HC_RPCS, node, HC_INTERVAL + 20)
        atf.run_command(
            "scontrol shutdown slurmctld",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        # Let shutdown drain its pending requests instead of waiting for the
        # transport timeout. The request was outstanding when shutdown began.
        signal_slurmd(node, "CONT")
        for _ in atf.timer(fatal=True):
            if not atf.is_slurmctld_running(quiet=True):
                break
        restart_offset = len(read_log())
        atf.start_slurmctld()
        resume_node(node)
        # HC/accounting timers need 65 seconds after restart before first dispatch.
        for _ in atf.timer(timeout=HC_INTERVAL + 20, poll_interval=1, fatal=True):
            text = log_since(restart_offset)
            if all(completed(text, kinds) for kinds in (PING_RPCS, HC_RPCS, ACCT_RPCS)):
                break
        assert "in_flight < 0" not in log_since(offset), "broadcast counter underflow"


def test_ping_unblocked_by_stuck_health_check():
    """Two live-node ping cycles must complete while HC remains outstanding."""
    offset = restart_observed()
    nodes = sorted(atf.get_nodes(fatal=True))
    live_node, stopped_node = nodes[0], nodes[-1]
    after = len(read_log())
    with stopped_slurmd(stopped_node):
        wait_outstanding(
            offset,
            after,
            PING_RPCS,
            stopped_node,
            max(PING_INTERVAL * 3, atf.default_polling_timeout),
        )
        request = wait_outstanding(
            offset, after, HC_RPCS, stopped_node, HC_INTERVAL + 20
        )
        # FUTURE skips even registration probes, without closing the HC socket.
        atf.run_command(
            f"scontrol update nodename={stopped_node} state=future",
            user="root",
            fatal=True,
        )
        # Allow tree-forwarding timeouts; observe the actual preceding ping drain.
        for _ in atf.timer(
            timeout=4 * MESSAGE_TIMEOUT + atf.default_polling_timeout,
            poll_interval=1,
            fatal=True,
        ):
            text = log_since(offset)
            assert outstanding(text, HC_RPCS, request), "HC ended before ping progress"
            _, drains = agent_counts(text, PING_RPCS)
            initial_drain = next(
                (position for position in drains if position > request), None
            )
            if (
                initial_drain is not None
                and len(completed(text, PING_RPCS, live_node, after=initial_drain)) >= 2
            ):
                break
