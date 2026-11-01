############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""The two node updates that change what is drained on a node.

Two halves, each under its own banner below. First "scontrol update
TresName=...", which drains and resumes a selection of TRES -- what the request
form accepts and refuses. Then "scontrol update NodeName=... State=...", which
resumes the node and, with it, everything drained on it.

The two are the whole administrative surface for drained TRES, and they meet in
one place: State=RESUME is the bulk form of Action=Resume, and
State=RESUME_KEEP_TRES is the way to opt out of it. Keeping them together is
what lets the State= half assert it resumes GRES, CPU and memory in one step,
which no per-TRES module can express.

What is deliberately elsewhere: what a drain then does to scheduling (the
per-TRES modules), the auto-drained absent device (test_190_13), and the
accounting interval an Action=Resume closes (test_190_9). The interval a
State=RESUME closes is checked here, with the resume itself.

The TresName= forms under test are the shared ones:

  - a TRES can be named exactly one way, so a second selector is
    ambiguous and is refused rather than resolved by precedence
  - Action is required; TresName alone must not fall back to an assumed one
  - a node-local TRES must say which node it lives on
  - a device-tracked (topology) GRES identifies its TRES individually,
    so a bare count cannot name one
  - an index outside the node's device set is refused
  - an Index list that is not well formed is refused
  - Reason is required

The per-TRES parse paths keep their own coverage beside the TRES they belong to
(Count= for cpu/mem in test_190_4 and test_190_5, count-only GRES in
test_190_6); what lives here is the shared front end they all pass through.
GPUs are used as the carrier because a device-tracked GRES exercises both the
index and the count paths through it.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Node names need to be in the form of nodeNN for the s2n variant.
DYN_NODE = "node100"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: GRES drain added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="bin/sacctmgr",
        reason="Ticket 25180: TresReference event format field added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_config_parameter("MaxNodeCount", 16)
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_tty(3)
    # The State= half asserts on the event RESUME_KEEP_TRES leaves open, and a
    # drained GPU reaches the event table only if accounting tracks it.
    atf.require_accounting()
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_slurm_running()


def _gpu_node(dynamic_slurmds, count=4):
    """Start the dynamic node with `count` untyped-UUID a100 GPUs."""
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf(*((i, "(null)") for i in range(count)))
    )
    atf.restart_slurm(clean=True, quiet=True)

    return dynamic_slurmds(DYN_NODE, f"Gres=gpu:a100:{count}")


# Drained well inside anything the node will report, so the request cannot fail
# for naming more memory than the node has.
MEM_DRAIN_MB = 256

# check_node_timers() only runs on the controller's PERIODIC_TIMEOUT tick, so a
# ResumeAfter fires some way after the delay it was given, not at it.
RESUME_AFTER_TIMEOUT = 120


def _set_node_state(node, state, **kwargs):
    """Run "scontrol update NodeName=<node> State=<state>"."""
    return atf.run_command(
        f"scontrol update NodeName={node} State={state}",
        user=atf.properties["slurm-user"],
        **kwargs,
    )


def _drained(node):
    """The node's DrainedTRES text, or "" when nothing is drained."""
    return atf.get_node_tres_drained(node) or ""


def _wait_nothing_drained(node):
    """Poll until the node reports no drained TRES at all."""
    for _ in atf.timer(fatal=True):
        text = _drained(node)
        if text == "":
            break


@pytest.fixture
def gpu_node(dynamic_slurmds):
    """A started dynamic node with two a100 GPUs, left clean at teardown."""
    node = _gpu_node(dynamic_slurmds, count=2)
    yield node
    # Whatever the test left behind, put the node back before the next one.
    atf.update_tres_drain(node, "gres/gpu", "Index=ALL", action="Resume", quiet=True)
    atf.update_tres_drain(node, "cpu", "Index=ALL", action="Resume", quiet=True)
    atf.update_tres_drain(node, "mem", "Count=ALL", action="Resume", quiet=True)
    _set_node_state(node, "RESUME", quiet=True)


# ---------------------------------------------------------------------------
# The TresName= form: what a drain/resume request accepts and refuses.
# ---------------------------------------------------------------------------


def test_drain_parenthesized_idx_rejected(dynamic_slurmds):
    """A drain rejects a parenthesized index in TresName."""
    node = _gpu_node(dynamic_slurmds)

    result = atf.run_command(
        f"scontrol update 'TresName=gres/gpu(IDX:0)' NodeName={node} "
        "Action=Drain Reason=rng",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] != 0, result
    assert "TresName requires Index, Uuid or Count" in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_drain_without_mode_rejected(dynamic_slurmds):
    """TresName without Action is rejected rather than assuming one."""
    node = _gpu_node(dynamic_slurmds, count=2)
    result = atf.run_command(
        f"scontrol update TresName=gres/gpu NodeName={node} Index=0 " f"Reason=nomode",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] != 0, result
    assert (
        "TresName requires Action=Drain, Resume or SetDrain" in result["stderr"]
    ), result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)
    assert "DEGRADED" not in (
        atf.get_node_parameter(node, "state") or ""
    ), atf.get_node_detail(node)


@pytest.mark.parametrize("tres", ["gres/gpu", "cpu", "mem"])
def test_node_local_tres_requires_nodename(tres):
    """A node-local TRES has to say which node it lives on.

    The requirement is keyed off the TRES rather than being unconditional, so
    that cluster-scoped TRES (licenses) can omit NodeName when they land.
    """
    result = atf.run_command(
        f"scontrol update TresName={tres} Count=1 Action=Drain Reason=nonode",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] != 0, result
    assert "requires NodeName" in result["stderr"], result


def test_drain_index_range(dynamic_slurmds):
    """A drain accepts an index range in Index.

    The comma inside the index list must stay with its spec, so this also
    exercises the top-level splitter for an index range.
    """
    node = _gpu_node(dynamic_slurmds)

    # No quotes around the value: an index range has no shell metacharacters.
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0,2-3", reason="idx")
    assert result["exit_code"] == 0, result
    detail = atf.get_node_detail(node)
    assert re.search(r"DrainedTRES=.*gpu\(IDX:0,2-3\)\(idx\)", detail), detail


@pytest.mark.parametrize(
    ("selector", "error"),
    [
        pytest.param("", "TresName requires Index, Uuid or Count", id="bare"),
        pytest.param("Count=2", "Invalid generic resource", id="count"),
        pytest.param(
            "Index=0 Count=1",
            "Count cannot be combined with Index or Uuid",
            id="count-plus-index",
        ),
    ],
)
def test_drain_topo_count_forms_rejected(dynamic_slurmds, selector, error):
    """A topology GRES rejects a drain with no selector and one given as a count.

    A count combined with an index is rejected as well.
    """
    node = _gpu_node(dynamic_slurmds, count=3)

    result = atf.update_tres_drain(node, "gres/gpu", selector, reason="cnt")
    assert result["exit_code"] != 0, result
    assert error in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_drain_out_of_range_index_rejected(dynamic_slurmds):
    """Draining a device index that does not exist is rejected."""
    node = _gpu_node(dynamic_slurmds, count=2)
    result = atf.update_tres_drain(node, "gres/gpu", "Index=5", reason="oor")
    assert result["exit_code"] != 0, result
    assert "Invalid generic resource" in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)


def test_drain_malformed_index_list_rejected(gpu_node):
    """An Index list with an empty, open-ended or reversed part is refused.

    Each is refused outright rather than read as some other set of devices,
    so nothing is drained.
    """
    for index in (",1", "1,", "0,,1", "-", "1-", "1-0"):
        result = atf.update_tres_drain(
            gpu_node, "gres/gpu", f"Index={index}", reason="shape"
        )
        assert result["exit_code"] != 0, f"Index={index} was accepted: {result}"
        assert "Invalid Trackable RESource" in result["stderr"], result
        assert atf.get_node_tres_drained(gpu_node) is None, atf.get_node_detail(
            gpu_node
        )


@pytest.mark.parametrize("action", ["Drain", "SetDrain"])
def test_drain_requires_reason(dynamic_slurmds, action):
    """A drain without a Reason is rejected and changes nothing.

    scontrol checks the Reason before it looks at the TRES, so one TRES
    covers them all.
    """
    node = _gpu_node(dynamic_slurmds, count=2)
    result = atf.update_tres_drain(node, "gres/gpu", "Index=0", action=action)
    assert result["exit_code"] != 0, result
    assert "You must specify a reason when draining TRES" in result["stderr"], result
    assert atf.get_node_tres_drained(node) is None, atf.get_node_detail(node)
    assert "DEGRADED" not in (
        atf.get_node_parameter(node, "state") or ""
    ), "node should not be DEGRADED"


# ---------------------------------------------------------------------------
# State=RESUME resumes everything.
# ---------------------------------------------------------------------------


def test_resume_clears_gres_drain_on_only_degraded_node(gpu_node):
    """State=RESUME returns a drained GPU to service on a node that is
    DEGRADED but was never DRAIN, clears DEGRADED, and closes the event.

    This is the node the feature mostly exists for -- one healthy enough to keep
    running jobs, with a single bad GPU. Its state is IDLE+DEGRADED, and
    RESUME has to be accepted from that state, not only from DRAIN.
    Resuming the node resumes its drained TRES with it, so it has to leave the
    accounting event closed just as resuming each TRES by hand does -- an
    interval left open charges for a GPU that came back.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        gpu_node, "DEGRADED"
    ), f"node should be DEGRADED after the GPU drain: {atf.get_node_detail(gpu_node)}"
    state = atf.get_node_parameter(gpu_node, "state") or ""
    assert "DRAIN" not in state, f"node must not be DRAIN for this test: {state!r}"
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(gpu_node)
        if reason == "ecc":
            break

    result = _set_node_state(gpu_node, "RESUME")
    assert result["exit_code"] == 0, f"RESUME refused on a DEGRADED-only node: {result}"

    _wait_nothing_drained(gpu_node)
    assert "DEGRADED" not in (
        atf.get_node_parameter(gpu_node, "state") or ""
    ), f"DEGRADED outlived the resume: {atf.get_node_parameter(gpu_node, 'state')!r}"
    for _ in atf.timer(fatal=True):
        count = atf.get_open_node_event_count(gpu_node)
        if count == 0:
            break


def test_resume_clears_every_tres_kind_at_once(gpu_node):
    """One State=RESUME clears GRES, CPU and memory drains together.

    Each kind is tracked separately -- GRES on the gres_node_state_t, CPUs in a
    bitmap, memory as a count -- so a resume that reached only some of them
    would still look right in a single-TRES test.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(gpu_node, "cpu", "Index=0", reason="cpu_hw")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(
            gpu_node, "mem", f"Count={MEM_DRAIN_MB}", reason="bad_dimm"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    drained = _drained(gpu_node).lower()
    for kind in ("gpu", "cpu", "mem"):
        assert kind in drained, f"{kind} drain not reported: {drained!r}"

    assert (
        _set_node_state(gpu_node, "RESUME")["exit_code"] == 0
    ), "state update should succeed"

    _wait_nothing_drained(gpu_node)


# ---------------------------------------------------------------------------
# State=RESUME_KEEP_TRES resumes only the node.
# ---------------------------------------------------------------------------


def test_resume_keep_tres_preserves_drained_tres(gpu_node):
    """RESUME_KEEP_TRES clears the node's DRAIN but keeps its drained TRES.

    Accounting has to follow: the whole-node event closes and the drained-TRES
    event opens again in its place, carrying the reason the GPU was drained
    with rather than the node's cleared one. Leaving no event open would stop
    charging for a GPU that is still gone.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        gpu_node, "DEGRADED"
    ), "gpu_node should reach DEGRADED"
    before_drained = _drained(gpu_node)

    assert (
        _set_node_state(gpu_node, "DRAIN Reason=maintenance")["exit_code"] == 0
    ), "could not drain the node itself"
    assert atf.wait_for_node_state(gpu_node, "DRAIN"), "gpu_node should reach DRAIN"
    # The whole-node event has to take over first, or finding "ecc" open at
    # the end would not show that the drained-TRES event opened again.
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(gpu_node)
        if reason == "maintenance":
            break

    assert (
        _set_node_state(gpu_node, "RESUME_KEEP_TRES")["exit_code"] == 0
    ), "state update should succeed"

    for _ in atf.timer(fatal=True):
        state = atf.get_node_parameter(gpu_node, "state") or ""
        if "DRAIN" not in state:
            break
    assert "DEGRADED" in (
        atf.get_node_parameter(gpu_node, "state") or ""
    ), f"RESUME_KEEP_TRES cleared DEGRADED: {atf.get_node_detail(gpu_node)}"
    assert (
        _drained(gpu_node) == before_drained
    ), f"RESUME_KEEP_TRES changed DrainedTRES: {_drained(gpu_node)!r}"
    for _ in atf.timer(fatal=True):
        reason = atf.get_open_node_event_reason(gpu_node)
        if reason == "ecc":
            break
    assert (
        atf.get_open_node_event_count(gpu_node) == 1
    ), f"expected a single open event: {atf.get_node_events(gpu_node)!r}"


def test_resume_after_timer_resumes_drained_tres(gpu_node):
    """A ResumeAfter timer resumes the node like State=RESUME.

    That includes the node's drained TRES, so a GPU drained separately is
    resumed along with the node's DRAIN.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        gpu_node, "DEGRADED"
    ), "gpu_node should reach DEGRADED"

    assert (
        _set_node_state(gpu_node, "DRAIN Reason=timed ResumeAfter=1")["exit_code"] == 0
    ), "state update should succeed"
    assert atf.wait_for_node_state(gpu_node, "DRAIN"), "gpu_node should reach DRAIN"

    for _ in atf.timer(timeout=RESUME_AFTER_TIMEOUT, fatal=True):
        state = atf.get_node_parameter(gpu_node, "state") or ""
        if "DRAIN" not in state:
            break
    assert not _drained(
        gpu_node
    ), f"ResumeAfter left the GPU drained: {_drained(gpu_node)!r}"
    assert "DEGRADED" not in atf.get_node_parameter(
        gpu_node, "state"
    ), atf.get_node_detail(gpu_node)


def test_resume_keep_tres_rejected_on_only_degraded_node(gpu_node):
    """RESUME_KEEP_TRES is refused on a node whose only fault is drained TRES.

    There is nothing for it to do there, and accepting it would report a state
    change that never happened. The admin wanted plain RESUME.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        gpu_node, "DEGRADED"
    ), "gpu_node should reach DEGRADED"

    result = _set_node_state(gpu_node, "RESUME_KEEP_TRES", quiet=True)
    assert (
        result["exit_code"] != 0
    ), f"RESUME_KEEP_TRES should be an invalid transition here: {result}"
    assert "Invalid node state transition" in result["stderr"], result
    assert (
        _drained(gpu_node) != ""
    ), "the refused request must leave the drained TRES alone"


# ---------------------------------------------------------------------------
# Parsing.
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    "state,resumes_tres",
    [
        ("RESU", True),
        ("RESUME", True),
        ("RESUME_K", False),
        ("RESUME_KEEP_TRES", False),
    ],
)
def test_resume_state_abbreviations(gpu_node, state, resumes_tres):
    """Each name keeps its own meaning however far it is abbreviated.

    scontrol matches State= values by prefix, and these two share one, so an
    abbreviation of either could silently land in the other's branch.
    """
    assert (
        atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="ecc")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        gpu_node, "DEGRADED"
    ), "gpu_node should reach DEGRADED"

    if not resumes_tres:
        # RESUME_KEEP_TRES is only a valid transition on a node that has
        # something besides drained TRES to resume.
        assert (
            _set_node_state(gpu_node, "DRAIN Reason=maintenance")["exit_code"] == 0
        ), "state update should succeed"
        assert atf.wait_for_node_state(gpu_node, "DRAIN"), "gpu_node should reach DRAIN"

    assert (
        _set_node_state(gpu_node, state)["exit_code"] == 0
    ), "state update should succeed"

    if resumes_tres:
        _wait_nothing_drained(gpu_node)
    else:
        assert (
            _drained(gpu_node) != ""
        ), f"State={state} should have left the GPU drained: {_drained(gpu_node)!r}"


def test_resume_underscore_alone_is_rejected(gpu_node):
    """ "RESUME_" matches neither name and is refused rather than guessed at."""
    result = _set_node_state(gpu_node, "RESUME_", quiet=True)
    assert result["exit_code"] != 0, f"'RESUME_' should be invalid input: {result}"
    assert "Invalid input: State=RESUME_" in result["stderr"], result


def test_state_degraded_is_rejected(gpu_node):
    """State=DEGRADED is not a state an admin can set."""
    result = _set_node_state(gpu_node, "DEGRADED", quiet=True)
    assert result["exit_code"] != 0, result
    assert "Invalid input: State=DEGRADED" in result["stderr"], result


def _scontrol(command):
    return atf.run_command(command, user=atf.properties["slurm-user"], quiet=True)


@pytest.mark.parametrize(
    ("command", "error"),
    [
        ("scontrol update TresName=cpu bare", "Invalid input: bare"),
        (
            "scontrol update TresName=cpu TresName=mem NodeName=node1 "
            "Index=0 Action=Drain Reason=twice",
            "TresName specified more than once",
        ),
        (
            "scontrol update TresName=cpu NodeName=node1 Count=abc "
            "Action=Drain Reason=badcount",
            "Invalid value Count=abc for Count",
        ),
        (
            "scontrol update TresName=cpu NodeName=node1 Index=0 "
            "Action=Nope Reason=badaction",
            "Invalid Action: Nope",
        ),
        (
            "scontrol update TresName=cpu NodeName=node1 Index=0 "
            "Action=Drain Reason=extra Color=blue",
            "Update of this parameter is not supported: Color=blue",
        ),
        (
            "scontrol update TresName= NodeName=node1 Index=0 "
            "Action=Drain Reason=empty",
            "TresName must be specified",
        ),
        (
            "scontrol update TresName=cpu NodeName=node1 Index=ALL Count=1 "
            "Action=Drain Reason=both",
            "ALL already selects everything",
        ),
        (
            "scontrol update TresName=gres/gpu NodeName=node1 Index=ALL Uuid=GPU-a Action=Drain Reason=both",
            "ALL already selects everything",
        ),
        (
            "scontrol update TresName=gres/gpu NodeName=node1 Uuid=ALL Index=0 Action=Drain Reason=both",
            "ALL already selects everything",
        ),
        ("scontrol show drainedtres bare", "Invalid input: bare"),
        ("scontrol show drainedtres Foo=1", "Invalid option: Foo=1"),
        ("scontrol show drainedtres 'NodeName=node['", "hostlist_create error"),
    ],
)
def test_tres_request_front_end_rejects_bad_input(command, error):
    """scontrol refuses a TRES request before it reaches the controller."""
    result = _scontrol(command)
    assert result["exit_code"] != 0, result
    assert error in result["stderr"], result


@pytest.mark.parametrize("flag", ["-a", "-F"])
def test_show_drainedtres_accepts_listing_flags(flag, gpu_node):
    """-a and -F widen the node query; the listing is the same."""
    atf.update_tres_drain(gpu_node, "gres/gpu", "Index=0", reason="flag", fatal=True)
    plain = _scontrol("scontrol show drainedtres")
    result = _scontrol(f"scontrol {flag} show drainedtres")
    assert result["exit_code"] == 0, result
    assert f"NodeName={gpu_node} " in result["stdout"], result
    assert result["stdout"] == plain["stdout"], result
