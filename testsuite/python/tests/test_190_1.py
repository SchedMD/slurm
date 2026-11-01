############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""What a drained TRES reports, and what it actually costs the node.

Two halves, each under its own banner below. First "scontrol show drainedtres",
the cluster-wide listing of what is drained. Then the device's own availability,
which the listing can only describe -- a device the listing calls drained must
cost the node exactly that device and no more.

The listing reads the structured drained-TRES records carried on the node RPC
rather than the DrainedTRES display string, which lets it report things the
string cannot express:

  - a drained device names its Index *and* its Uuid on the same line, so the
    line selects the same devices whichever selector is used
  - a Reason containing a comma or a parenthesis survives, because nothing has
    to be split on commas to recover the fields

With --json or --yaml the same listing is dumped as "scontrol show node" does:
the nodes that have a drained TRES, each with only the records TresName= selects.
The records are not grouped: a device or a memory drain is a record of its own,
and CPUs drained for the same reason share one.

TRES that share a name and a drain reason are reported on one line: an Index
range list, plus the UUID of every one that line covers. --exact reports each
TRES on a line of its own instead.

Every line uses the field names of "scontrol update TresName=...", so a reported
line is usable as an update selector unchanged: a device is named by Uuid and by
Index at once, and an update accepts the pair because they agree. Selectors that
name different devices are asserted to be refused, since a stale index must not
quietly resolve to whatever device now sits there.

What each half guards against:

  - reporting: a TresName= filter must select for the listing what it would act
    on for an update. The records carry the typed name (gres/gpu:a100) while an
    update accepts the untyped one, so filtering by the untyped name must still
    report a drained GPU.
  - enforcement: a device that is drained *and* allocated must be subtracted
    from the node's free count once, not once as drained and again as
    allocated, or the GPUs that are really free cannot be allocated and jobs
    pend forever.

The two halves share one node on purpose. The enforcement tests need a device in
both states at once, which is the state the listing exists to show, so they are
asserting against the same node the listing is read from.
"""

import json
import re

import pytest
import yaml

import atf

pytestmark = pytest.mark.slow

NODE = "node1"  # require_nodes(1, ...) yields node1
GPU_TRES = "gres/gpu:a100"  # a device record names the type it belongs to
GPU_UNTYPED = "gres/gpu"  # what "scontrol update" accepts for the same devices

# Fake GPU device UUIDs, in fake_gpus.conf device order.
GPU_A = "GPU-aaaaaaaa-0000-0000-0000-000000000000"
GPU_B = "GPU-bbbbbbbb-1111-1111-1111-111111111111"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file(
        "fake_gpus.conf", atf.fake_gpus_conf((0, GPU_A), (1, GPU_B))
    )
    atf.require_nodes(1, [("Gres", "gpu:a100:2"), ("CPUs", 4), ("RealMemory", 1024)])
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Ticket 25180: TRES drain added in 26.11"
    )
    atf.require_version(
        (26, 11),
        component="bin/sinfo",
        reason="Ticket 25180: sinfo -R degraded nodes added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: GRES device slots keyed by UUID added in 26.11",
    )
    atf.require_slurm_running()


def _show(*args, opts=""):
    """Run "scontrol show drainedtres".

    opts holds scontrol's own options (e.g. "--exact"), which have to precede
    the command; args are the listing's own TresName=/NodeName= filters.
    """
    return atf.run_command_output(
        f"scontrol {opts} show drainedtres {' '.join(args)}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _parse(output):
    """Parse "scontrol show drainedtres" output into a list of field dicts.

    Reason is free text and may contain spaces, so it is taken as the rest of
    the line; every field before it is a plain key=value. A reason containing
    whitespace is printed in double quotes so that the line can be pasted back
    into "scontrol update", so one layer of quotes is removed here.
    """
    recs = []
    for line in output.splitlines():
        if not line.startswith("NodeName="):
            continue
        rec, head = {}, line
        if " Reason=" in line:
            head, rec["Reason"] = line.split(" Reason=", 1)
            if len(rec["Reason"]) > 1 and rec["Reason"][0] == rec["Reason"][-1] == '"':
                rec["Reason"] = rec["Reason"][1:-1]
        for tok in head.split():
            key, _, val = tok.partition("=")
            rec[key] = val
        recs.append(rec)
    return recs


def _recs(tres_name, output=None):
    output = _show() if output is None else output
    return [r for r in _parse(output) if r.get("TresName") == tres_name]


@pytest.fixture(autouse=True)
def resume_everything():
    """Leave the node with nothing drained, so each test starts from empty."""
    yield
    for tres, selector in (
        ("gres/gpu", "Index=ALL"),
        ("cpu", "Index=ALL"),
        ("mem", "Count=ALL"),
    ):
        atf.update_tres_drain(NODE, tres, selector, action="Resume", quiet=True)
    assert atf.get_node_tres_drained(NODE) is None, atf.get_node_detail(NODE)


# ---------------------------------------------------------------------------
# Reporting: "scontrol show drainedtres".
#
# What the listing prints, and that every line it prints is usable as an update
# selector unchanged.
# ---------------------------------------------------------------------------


def test_nothing_drained_reports_nothing():
    """With a clean node the listing has no records."""
    assert not _parse(_show()), _show()


def test_reports_index_and_uuid_together():
    """A drained device names both its Index and its Uuid.

    DrainedTRES groups devices that share a reason, so an entry there carries an
    index or a UUID but never both; the records are per device and carry both.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="ecc_errors", fatal=True)

    recs = _recs(GPU_TRES)
    assert len(recs) == 1, _show()
    assert recs[0]["NodeName"] == NODE, recs
    assert recs[0]["Index"] == "0", recs
    assert recs[0]["Uuid"] == GPU_A, recs
    assert recs[0]["Reason"] == "ecc_errors", recs


def test_devices_sharing_a_reason_are_one_record():
    """Two devices drained for the same reason are reported on one line.

    The line names both by Index, as a range list, and carries the UUID of each
    -- one update can then act on both at once.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="same_reason", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="same_reason", fatal=True)

    recs = _recs(GPU_TRES)
    assert len(recs) == 1, _show()
    assert recs[0]["Index"] == "0-1", recs
    assert set(recs[0]["Uuid"].split(",")) == {GPU_A, GPU_B}, recs
    assert recs[0]["Reason"] == "same_reason", recs


def test_devices_with_different_reasons_stay_separate():
    """Only a shared reason merges TRES; different reasons stay on their
    own lines, so no reason is ever attributed to a device that did not get
    it."""
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="first", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="second", fatal=True)

    recs = _recs(GPU_TRES)
    assert len(recs) == 2, _show()
    assert {r["Index"] for r in recs} == {"0", "1"}, recs
    assert {r["Reason"] for r in recs} == {"first", "second"}, recs


@pytest.mark.parametrize("exact", ["--exact", "-e", "interactive"])
def test_exact_reports_each_device_on_its_own_line(exact):
    """--exact undoes the grouping: one line per drained TRES, each naming
    the single device it covers. -e is the same option, and "exact" sets it
    in interactive mode."""
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="same_reason", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="same_reason", fatal=True)

    if exact == "interactive":
        output = atf.run_command_output(
            "scontrol",
            input="exact\nshow drainedtres\n",
            user=atf.properties["slurm-user"],
            fatal=True,
            quiet=True,
        )
        # Drop the prompts interactive mode prints ahead of the output.
        output = "\n".join(
            line[line.find("NodeName=") :] if "NodeName=" in line else line
            for line in output.splitlines()
        )
    else:
        output = _show(opts=exact)
    recs = _recs(GPU_TRES, output)
    assert len(recs) == 2, output
    assert {r["Index"] for r in recs} == {"0", "1"}, recs
    assert {r["Uuid"] for r in recs} == {GPU_A, GPU_B}, recs
    assert all(r["Reason"] == "same_reason" for r in recs), recs


def test_reason_with_comma_and_parenthesis_survives():
    """A reason containing the characters the display string uses as structure
    is reported intact, since the fields never have to be recovered by
    splitting on them."""
    reason = "bad hbm, (rma pending)"
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason=reason, fatal=True)

    recs = _recs(GPU_TRES)
    assert len(recs) == 1, _show()
    assert recs[0]["Reason"] == reason, recs
    assert recs[0]["Index"] == "0", recs
    # A reason with spaces is quoted, so the line can be pasted back into an update
    assert f'Reason="{reason}"' in _show(), _show()


def test_cpu_and_mem_records():
    """CPUs report an index range and no UUID; memory reports a count."""
    atf.update_tres_drain(NODE, "cpu", "Index=0,2", reason="flaky_cores", fatal=True)
    atf.update_tres_drain(NODE, "mem", "Count=256", reason="bad_dimm", fatal=True)

    cpu = _recs("cpu")
    mem = _recs("mem")
    assert len(cpu) == 1, _show()
    assert len(mem) == 1, _show()
    assert "Uuid" not in cpu[0], cpu
    assert cpu[0]["Index"] == "0,2", cpu
    assert cpu[0]["Reason"] == "flaky_cores", cpu
    assert mem[0]["Count"] == "256", mem
    assert mem[0]["Reason"] == "bad_dimm", mem


def test_filters_narrow_the_listing():
    """TresName= and NodeName= restrict which records are printed."""
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="gpu_reason", fatal=True)
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="cpu_reason", fatal=True)

    assert len(_parse(_show())) == 2, _show()

    only_cpu = _parse(_show("TresName=cpu"))
    assert len(only_cpu) == 1, only_cpu
    assert only_cpu[0]["TresName"] == "cpu", only_cpu

    only_gpu = _parse(_show(f"TresName={GPU_TRES}"))
    assert len(only_gpu) == 1, only_gpu
    assert only_gpu[0]["TresName"] == GPU_TRES, only_gpu

    assert len(_parse(_show(f"NodeName={NODE}"))) == 2, _show(f"NodeName={NODE}")
    assert not _parse(_show("NodeName=nosuchnode")), "unrelated node matched"


def test_untyped_filter_matches_a_typed_record():
    """TresName= selects for the listing what it would act on for an update.

    The record carries "gres/gpu:a100" but an update accepts "gres/gpu" for the
    same devices, so filtering by the untyped name must still report a drained
    GPU.
    """
    atf.update_tres_drain(
        NODE, GPU_UNTYPED, "Index=0", reason="filter_test", fatal=True
    )

    unfiltered = _show()
    assert _recs(GPU_TRES, unfiltered), unfiltered

    filtered = _show(f"TresName={GPU_UNTYPED}")
    assert _recs(
        GPU_TRES, filtered
    ), f"the untyped filter should match the typed record: {filtered!r}"


def test_typed_filter_still_matches_exactly():
    """The untyped match must not cost the exact one."""
    atf.update_tres_drain(NODE, GPU_UNTYPED, "Index=0", reason="typed_test", fatal=True)

    filtered = _show(f"TresName={GPU_TRES}")
    assert _recs(GPU_TRES, filtered), filtered


def test_filter_does_not_match_a_partial_name():
    """An untyped filter matches whole names only, not any shared prefix."""
    atf.update_tres_drain(
        NODE, GPU_UNTYPED, "Index=0", reason="prefix_test", fatal=True
    )

    assert not _parse(_show("TresName=gres/gp")), "a truncated TRES name must not match"


@pytest.mark.parametrize("keep", ["Index", "Uuid"])
def test_reported_fields_resume_the_tres(keep):
    """Either selector printed for a device resumes exactly that device.

    A device line names both Index and Uuid, and either alone is enough to name
    the device, so dropping Reason and one selector still leaves a valid update.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="round_trip", fatal=True)

    lines = [ln for ln in _show().splitlines() if ln.startswith("NodeName=")]
    assert len(lines) == 1, _show()

    fields = re.sub(r"\s+Reason=.*$", "", lines[0]).split()
    drop = "Uuid" if keep == "Index" else "Index"
    selector = " ".join(f for f in fields if not f.startswith(f"{drop}="))
    assert f"{keep}=" in selector, selector

    result = atf.run_command(
        f"scontrol update {selector} Action=Resume",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, result
    assert not _recs(GPU_TRES), f"still drained after resume: {_show()}"


def test_whole_reported_line_is_accepted_unedited():
    """A reported line, minus its Reason, is a working update.

    This is the point of the two commands sharing field names: the device is
    named by Uuid and by Index at once, and since both name the same device the
    update is accepted rather than refused for naming it twice.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="round_trip", fatal=True)

    lines = [ln for ln in _show().splitlines() if ln.startswith("NodeName=")]
    assert len(lines) == 1, _show()
    whole_line = re.sub(r"\s+Reason=.*$", "", lines[0])
    assert "Index=" in whole_line and "Uuid=" in whole_line, whole_line

    result = atf.run_command(
        f"scontrol update {whole_line} Action=Resume",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, result
    assert not _recs(GPU_TRES), f"still drained after resume: {_show()}"


def test_whole_consolidated_line_is_accepted_unedited():
    """A line covering several devices round-trips as one update.

    The consolidated line pairs an Index range list with the UUID of every
    TRES it covers. The controller checks a paired Index against every
    device the UUIDs resolve to, so the two selectors have to describe the same
    set for this to be accepted -- which is why a line only carries Uuid when
    every one of its TRES reports one.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="round_trip", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="round_trip", fatal=True)

    lines = [ln for ln in _show().splitlines() if ln.startswith("NodeName=")]
    assert len(lines) == 1, _show()
    whole_line = re.sub(r"\s+Reason=.*$", "", lines[0])
    assert "Index=0-1" in whole_line, whole_line
    assert whole_line.count(",") >= 1, f"expected a UUID list: {whole_line}"

    result = atf.run_command(
        f"scontrol update {whole_line} Action=Resume",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, result
    assert not _recs(GPU_TRES), f"still drained after resume: {_show()}"


def test_disagreeing_selectors_are_rejected():
    """Index and Uuid naming different devices is refused.

    A stale index -- the UUID is not at that index, or the line came from another node
    -- must not quietly resolve to whichever device now sits at that index.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="mismatch", fatal=True)

    # GPU_B is at index 1, so pairing it with Index=0 names two devices.
    result = atf.update_tres_drain(
        NODE, "gres/gpu", f"Index=0 Uuid={GPU_B}", action="Resume"
    )
    assert result["exit_code"] != 0, result
    assert "Index and Uuid name different devices" in result["stderr"], result

    # The drain that was actually in place is untouched by the refused request.
    recs = _recs(GPU_TRES)
    assert len(recs) == 1, _show()
    assert recs[0]["Index"] == "0", recs


def test_sinfo_list_reasons_reports_the_degraded_node():
    """ "sinfo -R" includes a DEGRADED node and says what is drained on it.

    Draining a TRES records the reason against that TRES rather than against
    the node, so a node that is only degraded has no node reason for -R to
    print. It is reported under the drained TRES instead, which is the thing
    the admin is asking -R about.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="ecc_errors", fatal=True)
    assert atf.wait_for_node_state(
        NODE, "DEGRADED", timeout=60
    ), f"{NODE} should be DEGRADED: {atf.get_node_parameter(NODE, 'state')!r}"

    # The default -R format pads the reason to 20 columns, which would cut the
    # drained-TRES string off; ask for it unpadded so the whole value is seen.
    output = atf.run_command_output(
        'sinfo -R -h -o "%E|%U|%H|%N"',
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    lines = [ln for ln in output.splitlines() if ln.endswith(f"|{NODE}")]
    assert len(lines) == 1, f"sinfo -R omitted the degraded node: {output!r}"

    reason, user, timestamp, _node = lines[0].rsplit("|", 3)
    assert (
        reason == "gpu(IDX:0)(ecc_errors)"
    ), f"sinfo -R should report the drained TRES as the reason: {reason!r}"
    # No user or time is recorded for a reason the node does not have.
    assert (user, timestamp) == ("Unknown", "Unknown"), lines[0]


def _sinfo_state(node, fmt):
    """The node's state as sinfo renders it, for "%t" (compact) or "%T" (long).

    sinfo prints one line per partition holding the node, all carrying the same
    state, so collapse them; a genuine disagreement stays visible and fails the
    caller's comparison.
    """
    output = atf.run_command_output(
        f"sinfo -h -n {node} -o '{fmt}'",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    return ",".join(sorted(set(output.split())))


@pytest.fixture
def resume_node():
    """Resume the node after a test that drains it as a whole."""
    yield
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME Reason=cleanup",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


@pytest.fixture
def degraded_resv():
    """Name a reservation the test creates, and delete it afterwards."""
    resv = "degraded_resv"
    yield resv
    atf.run_command(
        f"scontrol delete reservation {resv}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_gpu_drain_sinfo_degraded_surface():
    """A DEGRADED node appears under 'degraded' and 'deg' sinfo state filters.

    The sinfo state filter is TRES independent, so a GPU drain covers the
    surface for every TRES drain.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="sinfo-drain", fatal=True)
    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), f"{NODE} should be DEGRADED after a GPU drain"

    for state_filter in ("degraded", "deg"):
        output = atf.run_command_output(
            f"sinfo -h -n {NODE} -t {state_filter} -o '%N'",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        assert output.strip(), f"{NODE} not found in sinfo -t {state_filter} output"


def test_gpu_drain_marks_state_degraded():
    """An idle degraded node reports as degraded rather than as idle.

    Nothing else is claiming the state column on an idle node, so the degraded
    marker takes the column outright instead of annotating "idle" with a
    suffix.
    """
    atf.update_tres_drain(
        NODE, "gres/gpu", "Index=0", reason="degraded-drain", fatal=True
    )
    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), f"{NODE} should be DEGRADED after a GPU drain"

    for _ in atf.timer(fatal=True):
        state = _sinfo_state(NODE, "%t")
        if state == "deg":
            break
    # %T renders a different word than %t here, so this covers the long form
    # of the state string as well as the compact one.
    assert (
        _sinfo_state(NODE, "%T") == "degraded"
    ), f"sinfo %T should report 'degraded', got {_sinfo_state(NODE, '%T')!r}"

    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", action="Resume")
    for _ in atf.timer(fatal=True):
        state = _sinfo_state(NODE, "%t")
        if state == "idle":
            break


def test_gpu_drain_suffix_survives_node_drain(resume_node):
    """The degraded suffix stays visible after the node itself is drained.

    A TRES drain is normally followed by draining the whole node to service the
    hardware, so the marker has to survive that. DRAIN is reported ahead of
    every base state, and must not swallow the degraded marker.
    """
    atf.update_tres_drain(
        NODE, "gres/gpu", "Index=0", reason="drain-suffix", fatal=True
    )
    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), f"{NODE} should be DEGRADED after a GPU drain"

    assert (
        atf.run_command(
            f"scontrol update NodeName={NODE} State=DRAIN Reason=drain-suffix",
            user=atf.properties["slurm-user"],
            quiet=True,
        )["exit_code"]
        == 0
    ), "scontrol update should succeed"
    for _ in atf.timer(fatal=True):
        state = _sinfo_state(NODE, "%t")
        if state == "drain?":
            break
    # %T renders a different word than %t here, so this covers the long form
    # of the state string as well as the compact one.
    assert (
        _sinfo_state(NODE, "%T") == "drained?"
    ), f"sinfo %T should report 'drained?', got {_sinfo_state(NODE, '%T')!r}"


def test_sinfo_states_rejects_degraded_suffix():
    """A state string carrying the suffix is rejected rather than reinterpreted.

    "IDLE?" is not a state, so sinfo --states refuses it instead of matching
    every idle node.
    """
    result = atf.run_command(
        "sinfo -h -t 'IDLE?' -o '%N'",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] != 0, f"sinfo should reject 'IDLE?': {result}"
    assert (
        "bad state string" in (result["stderr"] or "").lower()
    ), f"expected a 'Bad state string' error: {result}"


def test_degraded_marker_yields_state_word_to_other_flags(degraded_resv):
    """A degraded node that is also reserved reports "resv?", not plain "deg".

    Another state flag takes the state word -- the reservation has to stay
    visible, so draining a TRES never hides a scheduling state -- but the
    degraded marker is kept as the "?" suffix on it. The node is still
    degraded either way, and --states=degraded still finds it.
    """
    resv = degraded_resv
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="resv-suffix", fatal=True)
    assert atf.wait_for_node_state(
        NODE, "DEGRADED"
    ), f"{NODE} should be DEGRADED after a GPU drain"
    for _ in atf.timer(fatal=True):
        state = _sinfo_state(NODE, "%t")
        if state == "deg":
            break

    assert (
        atf.run_command(
            f"scontrol create reservation ReservationName={resv} "
            f"starttime=now duration=120 user=root nodes={NODE}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )["exit_code"]
        == 0
    ), "reservation should be created"
    for _ in atf.timer(fatal=True):
        state = _sinfo_state(NODE, "%t")
        if state == "resv?":
            break

    output = atf.run_command_output(
        f"sinfo -h -n {NODE} -t degraded -o '%N'",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert (
        output.strip()
    ), "-t degraded must still match a degraded node that is also reserved"


def _dump(fmt, *args):
    """Run "scontrol --json|--yaml show drainedtres" and parse its output.

    Returns the node list. Anything the dump reports under "errors" fails the
    test, since a clean listing must not carry any.
    """
    output = atf.run_command_output(
        f"scontrol --{fmt} show drainedtres {' '.join(args)}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    data = json.loads(output) if fmt == "json" else yaml.safe_load(output)
    assert not data.get("errors"), data["errors"]
    return data["nodes"]


def _dumped_recs(nodes, node_name=NODE):
    """Every drained_tres record dumped for node_name."""
    return [
        rec
        for node in nodes
        if node["name"] == node_name
        for rec in node["drained_tres"]
    ]


@pytest.mark.parametrize("fmt", ["json", "yaml"])
def test_dump_reports_the_drained_records(fmt):
    """--json and --yaml dump the drained TRES records of each node.

    The records carry the same facts as the text listing: a device's Index and
    Uuid together, a CPU's index with no UUID, and a memory drain's count.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="ecc_errors", fatal=True)
    atf.update_tres_drain(NODE, "cpu", "Index=0,2", reason="flaky_cores", fatal=True)
    atf.update_tres_drain(NODE, "mem", "Count=256", reason="bad_dimm", fatal=True)

    nodes = _dump(fmt)
    assert [n["name"] for n in nodes] == [NODE], nodes

    by_tres = {rec["tres_name"]: rec for rec in _dumped_recs(nodes)}
    assert set(by_tres) == {GPU_TRES, "cpu", "mem"}, by_tres

    gpu = by_tres[GPU_TRES]
    assert gpu["index"] == "0", gpu
    assert gpu["uuid"] == GPU_A, gpu
    assert gpu["reason"] == "ecc_errors", gpu

    cpu = by_tres["cpu"]
    assert cpu["index"] == "0,2", cpu
    assert not cpu.get("uuid"), cpu
    assert cpu["reason"] == "flaky_cores", cpu

    mem = by_tres["mem"]
    assert mem["count"]["number"] == 256, mem
    assert mem["reason"] == "bad_dimm", mem


def test_dump_reports_each_device_as_its_own_record():
    """The dump never groups devices, so --exact has nothing left to undo.

    Two devices drained for the same reason are one line in the text listing but
    two records here, each naming the single device it covers.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="same_reason", fatal=True)
    atf.update_tres_drain(NODE, "gres/gpu", "Index=1", reason="same_reason", fatal=True)

    recs = _dumped_recs(_dump("json"))
    assert len(recs) == 2, recs
    assert {r["index"] for r in recs} == {"0", "1"}, recs
    assert {r["uuid"] for r in recs} == {GPU_A, GPU_B}, recs
    assert (
        _dumped_recs(_dump("json", "TresName=cpu")) == []
    ), "dump should be empty for this request"


def test_dump_applies_the_filters():
    """TresName= narrows each node's records, and NodeName= narrows the nodes.

    A node with nothing left after filtering is omitted, so the dump holds what
    the text listing would have printed. The untyped GPU name selects the typed
    record, as it does in the text listing.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="gpu_reason", fatal=True)
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="cpu_reason", fatal=True)

    assert len(_dumped_recs(_dump("json"))) == 2, "dump should list the drained records"

    cpu = _dumped_recs(_dump("json", "TresName=cpu"))
    assert [r["tres_name"] for r in cpu] == ["cpu"], cpu

    gpu = _dumped_recs(_dump("json", f"TresName={GPU_UNTYPED}"))
    assert [r["tres_name"] for r in gpu] == [GPU_TRES], gpu

    assert _dump("json", "TresName=mem") == [], "no node has drained memory"
    assert _dump("json", "NodeName=nosuchnode") == [], "unrelated node matched"
    assert (
        len(_dumped_recs(_dump("json", f"NodeName={NODE}"))) == 2
    ), "dump should list the drained records"


@pytest.mark.parametrize("fmt", ["json", "yaml"])
def test_dump_of_a_clean_cluster_has_no_nodes(fmt):
    """Nothing drained dumps an empty, valid document rather than text."""
    assert _dump(fmt) == [], "dump should be empty for this request"


# ---------------------------------------------------------------------------
# Enforcement: what the drain costs the node.
#
# A device the listing calls drained has to cost exactly that device. These
# submit real jobs, so they check the free count the scheduler actually uses
# rather than the one the listing prints.
# ---------------------------------------------------------------------------


def test_drained_allocated_gpu_leaves_the_other_gpu_schedulable():
    """Draining a GPU a job holds must not also cost the free one.

    A GPU that is both drained and allocated is taken off the node's free
    count once, so the idle GPU stays schedulable.
    """
    holder = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:1 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(holder, "RUNNING", fatal=True, timeout=60)

    # Drain whichever device the running job was given, so the drained set
    # and the allocated set overlap on exactly one GPU.
    job = str(atf.get_job(holder))
    match = re.search(r"IDX:(\d+)", job)
    assert match, f"could not tell which GPU the job holds: {job}"
    held_idx = match.group(1)

    atf.update_tres_drain(
        NODE, GPU_UNTYPED, f"Index={held_idx}", reason="drain_held", fatal=True
    )
    atf.wait_for_job_state(holder, "RUNNING", fatal=True)

    # One GPU is drained-and-allocated; the other is untouched and free.
    second = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:1 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        second, "RUNNING", timeout=60
    ), "the undrained GPU should still be allocatable"

    job2 = str(atf.get_job(second))
    assert not re.search(
        rf"IDX:{held_idx}\b", job2
    ), f"the second job must not land on the drained GPU: {job2}"


def test_both_gpus_drained_blocks_a_new_job():
    """A drained GPU that no job holds is not given to a new job."""
    holder = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:1 --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(holder, "RUNNING", fatal=True, timeout=60)

    atf.update_tres_drain(
        NODE, GPU_UNTYPED, "Index=ALL", reason="drain_all", fatal=True
    )
    atf.wait_for_job_state(holder, "RUNNING", fatal=True)

    blocked = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:1 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.job_stays_pending(
        blocked
    ), "no GPU is free, so the job must stay pending"


def test_set_drain_by_index_leaves_other_tres():
    """SetDrain of one GPU index resumes the other GPU and leaves CPU alone.

    SetDrain is scoped to the TRES TresName names. Naming gpu index 1 drains
    that device and resumes gpu 0, which was drained and is no longer listed.
    The CPU drain is a different TRES, so it stays.
    """
    atf.update_tres_drain(NODE, "gres/gpu", "Index=0", reason="gpu0", fatal=True)
    atf.update_tres_drain(NODE, "cpu", "Index=0", reason="cpu0", fatal=True)

    result = atf.update_tres_drain(
        NODE, "gres/gpu", "Index=1", action="SetDrain", reason="gpu1"
    )
    assert result["exit_code"] == 0, result

    drain = atf.get_node_tres_drained(NODE) or ""
    assert re.search(r"gpu\(IDX:1\)\(gpu1\)", drain), drain
    assert not re.search(r"gpu\(IDX:0\)", drain), drain
    assert re.search(r"cpu\(IDX:0\)\(cpu0\)", drain), drain
