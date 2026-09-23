############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression test for bug 25211 clearing job_desc fields via job_submit/lua.

job_submit/lua could not clear job string fields on slurm_job_modify by
assigning "" (empty string).  The fix whitelists the fields whose
controller modify path has a clearing branch; assignment to other string
fields on modify remains a no-op.

Group A - whitelist (clearing must work via Lua hook setting "" on modify):
  admin_comment, comment, cpus_per_tres, dependency, exc_nodes, extra,
  features, licenses, mem_per_tres, prefer, req_nodes, reservation,
  tres_bind, tres_freq, tres_per_job, tres_per_node, tres_per_socket,
  tres_per_task, wckey.  Several whitelisted fields can also be cleared
  in a single slurm_job_modify() call, and a clear overrides a value the
  user supplies in that same request.

Group B - gres legacy alias (= tres_per_node).
"""

import pytest

import atf

pytestmark = pytest.mark.slow

# ---------------------------------------------------------------------------
# Whitelisted fields.  Both the Lua hook below and the Group A test are
# generated from this table, so a field cannot gain a hook branch without
# also gaining coverage.
#
# (lua_field, scontrol_field, seed_kind, seed)
#
# seed_kind "sbatch" seeds the field via submit arguments, "update" via a
# scontrol update.  A None seed_kind means the seed or the trigger is
# irregular and the field has a hand-written test below.
# ---------------------------------------------------------------------------
_CLEAR_FIELDS = [
    ("admin_comment", "AdminComment", None, None),
    ("comment", "Comment", None, None),
    ("cpus_per_tres", "CpusPerTres", "update", "CpusPerTres=cpu:2"),
    ("dependency", "Dependency", None, None),
    # On a single-node cluster this excludes the only node, so
    # _release_and_verify() cannot pass unless the clear reached the
    # scheduler rather than just the displayed field.
    ("exc_nodes", "ExcNodeList", "sbatch", "--exclude={node}"),
    ("extra", "Extra", "update", "Extra=extra_val"),
    ("features", "Features", "sbatch", "--constraint=testfeat157"),
    ("gres", "TresPerNode", None, None),
    ("licenses", "Licenses", "sbatch", "--licenses=test157_lic:1"),
    ("mem_per_tres", "MemPerTres", "update", "MemPerTres=cpu:100"),
    ("prefer", "Prefer", "sbatch", "--prefer=testpref157"),
    ("req_nodes", "ReqNodeList", "sbatch", "--nodelist={node}"),
    ("reservation", "Reservation", None, None),
    ("tres_bind", "TresBind", "update", "TresBind=gres/gpu:closest"),
    ("tres_freq", "TresFreq", "update", "TresFreq=gpu:1000"),
    ("tres_per_job", "TresPerJob", "update", "TresPerJob=cpu:2"),
    ("tres_per_node", "TresPerNode", "update", "TresPerNode=cpu:2"),
    ("tres_per_socket", "TresPerSocket", "update", "TresPerSocket=cpu:1"),
    ("tres_per_task", "TresPerTask", "update", "TresPerTask=cpu:1"),
    ("wckey", "WCKey", "sbatch", "--wckey=test157_wckey"),
]

# The hand-written test covering each None seed_kind row above, enforced in
# setup() so such a row cannot land without coverage.
_HAND_WRITTEN_TESTS = {
    "admin_comment": "test_A_admin_comment_clear",
    "comment": "test_A_comment_clear",
    "dependency": "test_A_dependency_clear",
    "gres": "test_B_gres_alias_clear",
    "reservation": "test_A_reservation_clear",
}

# admin_comment and comment are triggered via job_desc.extra so the sentinel
# does not occupy the field being cleared.
_EXTRA_TRIGGERED = ("admin_comment", "comment")

# Fields the multi-field case clears in a single slurm_job_modify() call.
# Every other case clears exactly one field per call, so nothing else would
# catch a clear that only works in isolation.
#
# (lua_field, scontrol_field, sbatch seed)
_MULTI_FIELDS = [
    ("features", "Features", "--constraint=testfeat157"),
    ("licenses", "Licenses", "--licenses=test157_lic:1"),
    ("prefer", "Prefer", "--prefer=testpref157"),
]
_MULTI_SENTINEL = "TEST_MULTI_CLEAR"

# job_submit_plugins.shtml documents these three as resetting to an empty
# string, and every other clearable field as resetting to NULL.
_CLEARS_TO_EMPTY = ("admin_comment", "comment", "extra")

_SENTINEL = {
    field: f"TEST_{field.upper().replace('_', '')}_CLEAR"
    for field, _, _, _ in _CLEAR_FIELDS
}
_GROUP_A_CASES = [
    (field, _SENTINEL[field], show_field, seed_kind, seed)
    for field, show_field, seed_kind, seed in _CLEAR_FIELDS
    if seed_kind is not None
]

_LUA_CLEARS = "\n\n".join(
    f'    {"if" if index == 0 else "elseif"} '
    f'{"e" if field in _EXTRA_TRIGGERED else "c"} == '
    f'"{_SENTINEL[field]}" then\n        job_desc.{field} = ""'
    for index, (field, _, _, _) in enumerate(_CLEAR_FIELDS)
)

_LUA_CLEARS += f'\n\n    elseif c == "{_MULTI_SENTINEL}" then\n' + "\n".join(
    f'        job_desc.{field} = ""' for field, _, _ in _MULTI_FIELDS
)

_LUA_CONTENT = f"""--[[
 job_submit.lua for bug 25211 regression tests.

 slurm_job_modify clears a whitelisted field by assigning "" (empty
 string).  A single Lua file drives every subtest; the sentinel arrives
 via scontrol update Comment=<sentinel>, except for admin_comment and
 comment, which are triggered via Extra=<sentinel>.
--]]

function slurm_job_submit(job_desc, part_list, submit_uid)
    return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    local c = job_desc.comment or ""
    local e = job_desc.extra  or ""

{_LUA_CLEARS}

    end

    return slurm.SUCCESS
end

slurm.log_info("bug25211 test hook loaded")
return slurm.SUCCESS
"""


# ---------------------------------------------------------------------------
# Module-level fixture: install Lua plugin config and start Slurm.
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 25211: job_submit/lua clear-on-empty modify lands in 26.11",
    )
    # _GROUP_A_CASES drops each None seed_kind row, so without this a new row
    # would generate a Lua branch that nothing exercises.
    uncovered = [
        field
        for field, _, seed_kind, _ in _CLEAR_FIELDS
        if seed_kind is None and _HAND_WRITTEN_TESTS.get(field) not in globals()
    ]
    assert not uncovered, f"clearable fields with no test: {uncovered}"
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", _LUA_CONTENT)
    atf.require_config_parameter_includes("Licenses", "test157_lic:10")
    # Accounting + TrackWCKey are needed so a job's wckey is recorded and
    # visible to scontrol; without it the wckey case cannot observe the
    # field being cleared.  wckeys must not be enforced, or the controller
    # rejects clearing the wckey to "" with ESLURM_INVALID_WCKEY.
    atf.require_accounting(modify=True)
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "wckeys")
    # A fake GPU gres lets the tres_bind / tres_freq cases populate
    # TresBind/TresFreq without real accelerator hardware.
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_tty(0)
    atf.require_config_parameter("Name", {"gpu": {"File": "/dev/tty0"}}, source="gres")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(1, [("Gres", "gpu:1"), ("Features", "testfeat157,testpref157")])
    atf.require_slurm_running()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _submit_held(*, extra_args=None, comment=None):
    """Submit a held batch job and return its job id.

    The payload is a trivial command so the job completes quickly once
    released (see _release_and_verify).
    """
    args = "--hold -n1 --output=/dev/null --wrap='true'"
    if comment is not None:
        args += f" --comment={comment}"
    if extra_args is not None:
        args += f" {extra_args}"
    return atf.submit_job_sbatch(args, fatal=True)


def _modify(job_id, **kwargs):
    """Run scontrol update on job_id with arbitrary key=value pairs."""
    parts = [f"{k}={v}" for k, v in kwargs.items()]
    return atf.run_command(
        f"scontrol update JobId={job_id} " + " ".join(parts),
        quiet=True,
    )


def _field(job_id, param):
    """Return get_job_parameter result, or None if absent or (null)."""
    return atf.get_job_parameter(job_id, param, default=None, quiet=True)


def _expected_cleared(field):
    """The value a cleared whitelisted field reads back as.

    job_submit_plugins.shtml documents admin_comment, comment and extra as
    resetting to an empty string, and the rest as resetting to NULL.
    """
    return "" if field in _CLEARS_TO_EMPTY else None


def _release_and_verify(job_id):
    """Release a held job and confirm it launches and completes cleanly.

    Clearing a whitelisted field must never leave the job unable to start
    (e.g. a slurmstepd failure on a now-NULL field), so each clear test
    runs the job to completion and requires it to reach COMPLETED.
    """
    atf.run_command(f"scontrol release {job_id}", fatal=True, quiet=True)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)


def _get_usable_node():
    """Return the name of a node that can still run jobs."""
    for name, info in atf.get_nodes(quiet=True).items():
        if "IDLE" in info["state"]:
            return name
    pytest.fail("No usable node available")


# ===========================================================================
# Group A - Whitelist: clearing must work
# ===========================================================================


@pytest.mark.parametrize(
    "field, sentinel, show_field, seed_kind, seed",
    _GROUP_A_CASES,
    ids=[row[0] for row in _GROUP_A_CASES],
)
def test_A_clear(field, sentinel, show_field, seed_kind, seed):
    """Lua sets a whitelisted field to "" on modify -> the field is cleared."""
    if "{node}" in seed:
        seed = seed.format(node=_get_usable_node())
    if seed_kind == "sbatch":
        job_id = _submit_held(extra_args=seed)
    else:
        job_id = _submit_held()
        atf.run_command(
            f"scontrol update JobId={job_id} {seed}", fatal=True, quiet=True
        )
    before = _field(job_id, show_field)
    assert (
        before is not None and before != ""
    ), f"pre-condition: {show_field} must be set, got {before!r}"
    result = _modify(job_id, Comment=sentinel)
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    after = _field(job_id, show_field)
    expected = _expected_cleared(field)
    assert (
        after == expected
    ), f"{show_field} must be {expected!r} once cleared, got {after!r}"
    comment_after = _field(job_id, "Comment")
    assert (
        comment_after == sentinel
    ), f"clearing {field} must not disturb Comment, got {comment_after!r}"
    _release_and_verify(job_id)


def test_A_multi_field_clear():
    """Lua clears several whitelisted fields in one modify -> all are cleared."""
    job_id = _submit_held(extra_args=" ".join(seed for _, _, seed in _MULTI_FIELDS))
    for _, show_field, _ in _MULTI_FIELDS:
        before = _field(job_id, show_field)
        assert (
            before is not None and before != ""
        ), f"pre-condition: {show_field} must be set, got {before!r}"
    result = _modify(job_id, Comment=_MULTI_SENTINEL)
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    for field, show_field, _ in _MULTI_FIELDS:
        after = _field(job_id, show_field)
        expected = _expected_cleared(field)
        assert (
            after == expected
        ), f"{show_field} must be {expected!r} once cleared, got {after!r}"
    comment_after = _field(job_id, "Comment")
    assert (
        comment_after == _MULTI_SENTINEL
    ), f"a multi-field clear must not disturb Comment, got {comment_after!r}"
    _release_and_verify(job_id)


def test_A_clear_overrides_same_request_value():
    """Lua clears a field the user sets in the same modify -> the clear wins."""
    control_id = _submit_held()
    result = _modify(control_id, Features="testfeat157", Comment="TEST_SAMERPC_CONTROL")
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    control = _field(control_id, "Features")
    assert (
        control == "testfeat157"
    ), f"pre-condition: Features must be set in the same request, got {control!r}"
    _release_and_verify(control_id)

    job_id = _submit_held()
    result = _modify(job_id, Features="testfeat157", Comment=_SENTINEL["features"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    after = _field(job_id, "Features")
    assert after is None, f"Features must be cleared, got {after!r}"
    _release_and_verify(job_id)


def test_A_admin_comment_clear():
    """Lua sets admin_comment="" on modify -> AdminComment must be cleared."""
    job_id = _submit_held()
    # AdminComment requires admin (slurm-user) privileges to set.
    atf.run_command(
        f"scontrol update JobId={job_id} AdminComment=admin_val",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    assert _field(job_id, "AdminComment") == "admin_val", "pre-condition"
    # admin_comment is privileged, so the modify must also run as
    # slurm-user for the controller to accept the resulting assignment.
    result = atf.run_command(
        f"scontrol update JobId={job_id} Extra={_SENTINEL['admin_comment']}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    ac_after = _field(job_id, "AdminComment")
    assert ac_after == _expected_cleared(
        "admin_comment"
    ), f"AdminComment must be cleared, got {ac_after!r}"
    extra_after = _field(job_id, "Extra")
    assert (
        extra_after == _SENTINEL["admin_comment"]
    ), f"clearing admin_comment must not disturb Extra, got {extra_after!r}"
    _release_and_verify(job_id)


def test_A_comment_clear():
    """Lua sets comment="" on modify (triggered via Extra) -> Comment cleared."""
    job_id = _submit_held(comment="some_comment")
    assert _field(job_id, "Comment") == "some_comment", "pre-condition"
    result = _modify(job_id, Extra=_SENTINEL["comment"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    comment_after = _field(job_id, "Comment")
    assert comment_after == _expected_cleared(
        "comment"
    ), f"Comment must be cleared, got {comment_after!r}"
    extra_after = _field(job_id, "Extra")
    assert (
        extra_after == _SENTINEL["comment"]
    ), f"clearing comment must not disturb Extra, got {extra_after!r}"
    _release_and_verify(job_id)


def test_A_clear_on_running_job():
    """Clearing a whitelisted field must also work on a RUNNING job.

    Group A otherwise only modifies held jobs, but the documented
    behavior is not qualified by job state and scontrol accepts Comment
    on a running job.
    """
    job_id = atf.submit_job_sbatch(
        "-n1 --output=/dev/null --comment=some_comment --wrap='sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    result = _modify(job_id, Extra=_SENTINEL["comment"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    comment_after = _field(job_id, "Comment")
    assert comment_after == _expected_cleared(
        "comment"
    ), f"Comment must be cleared on a running job, got {comment_after!r}"
    extra_after = _field(job_id, "Extra")
    assert (
        extra_after == _SENTINEL["comment"]
    ), f"clearing comment must not disturb Extra, got {extra_after!r}"


def test_A_dependency_clear():
    """Lua sets dependency="" on modify -> Dependency must be cleared."""
    # Depend on a real held job so the dependency is valid but stays
    # unsatisfied, rather than relying on a non-existent job id.
    dep_job_id = _submit_held()
    job_id = _submit_held(extra_args=f"--dependency=afterany:{dep_job_id}")
    dep_before = _field(job_id, "Dependency")
    assert (
        dep_before is not None and dep_before != ""
    ), f"pre-condition: Dependency must be set, got {dep_before!r}"
    result = _modify(job_id, Comment=_SENTINEL["dependency"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    dep_after = _field(job_id, "Dependency")
    assert dep_after == _expected_cleared(
        "dependency"
    ), f"Dependency must be cleared, got {dep_after!r}"
    comment_after = _field(job_id, "Comment")
    assert (
        comment_after == _SENTINEL["dependency"]
    ), f"clearing dependency must not disturb Comment, got {comment_after!r}"
    _release_and_verify(job_id)


@pytest.fixture
def reservation():
    """Create a reservation for the test and delete it on teardown."""
    resv_name = "test157_resv"
    node = _get_usable_node()
    atf.run_command(
        f"scontrol create reservation reservationname={resv_name} "
        f"user={atf.properties['test-user']} start=now duration=60 "
        f"nodes={node} flags=ignore_jobs",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    yield resv_name
    atf.run_command(
        f"scontrol delete reservation {resv_name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_A_reservation_clear(reservation):
    """Lua sets reservation="" on modify -> Reservation must become (null)."""
    job_id = _submit_held(extra_args=f"--reservation={reservation}")
    resv_before = _field(job_id, "Reservation")
    assert (
        resv_before == reservation
    ), f"pre-condition: Reservation must be {reservation}, got {resv_before!r}"
    result = _modify(job_id, Comment=_SENTINEL["reservation"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    resv_after = _field(job_id, "Reservation")
    assert resv_after == _expected_cleared(
        "reservation"
    ), f"Reservation must be cleared, got {resv_after!r}"
    comment_after = _field(job_id, "Comment")
    assert (
        comment_after == _SENTINEL["reservation"]
    ), f"clearing reservation must not disturb Comment, got {comment_after!r}"
    # The job is not run to completion here: the reservation still holds the
    # cluster's only node, so a job no longer in the reservation would stay
    # pending. Clearing a scheduler-side field cannot break job launch.


# ===========================================================================
# Group B - gres legacy alias
# ===========================================================================


def test_B_gres_alias_clear():
    """Lua sets gres="" (legacy alias) on modify -> TresPerNode must be cleared."""
    job_id = _submit_held(extra_args="--gres=gpu:1")
    tpn_before = _field(job_id, "TresPerNode")
    assert (
        tpn_before is not None and tpn_before != ""
    ), f"pre-condition: TresPerNode must be set, got {tpn_before!r}"
    result = _modify(job_id, Comment=_SENTINEL["gres"])
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    tpn_after = _field(job_id, "TresPerNode")
    assert tpn_after == _expected_cleared(
        "gres"
    ), f"TresPerNode must be cleared via gres alias, got {tpn_after!r}"
    comment_after = _field(job_id, "Comment")
    assert (
        comment_after == _SENTINEL["gres"]
    ), f"clearing gres must not disturb Comment, got {comment_after!r}"
    _release_and_verify(job_id)
