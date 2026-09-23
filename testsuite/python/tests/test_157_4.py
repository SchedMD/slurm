############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Regression guardrails for bug 25211 job_submit/lua field clearing.

These exercise behavior that the fix must leave unchanged, so they are not
version gated and run on every supported version:

Group C - submit-path behavior is unchanged by the patch: assigning "" on
    submit leaves the field NULL, discarding the user-supplied value.

Group D - non-whitelist guardrails (must NOT regress).  Assigning "" to a
    field outside the whitelist must leave that field alone.  Every field
    here reaches a controller branch that would act on "": partition and
    qos revert to a default, mail_user reverts to the job owner, and
    std_out, std_err and std_in are cleared outright.  The plugin's
    whitelist is the only thing keeping "" away from those branches, and
    job_submit_plugins.shtml documents the fields as deliberately
    excluded.  Each case is seeded away from the value "" would produce,
    so a regression is observable rather than silently identical.  The
    Lua hook and the test are both generated from _GROUP_D_CASES.

Group E - assigning a non-empty value must still take effect.  job_desc is
    documented as input/output and the hook as able to modify the job
    parameters supplied by the user, so narrowing which fields "" clears
    must not disturb ordinary assignment:
    E1 features      set on modify
    E2 tres_per_node set on modify
    E3 comment       overwritten on modify
    E4 features      set on submit
"""

import os

import pytest

import atf

QOS_NAME = "test157_qos"
ACCOUNT_NAME = "test157_acct"
PARTITION_NAME = "test157_part"

# ---------------------------------------------------------------------------
# Group D - fields outside the whitelist.  Both the Lua hook below and the
# Group D test are generated from this table, so a field cannot gain a hook
# branch without also gaining coverage.
#
# (lua_field, sentinel, scontrol_field, sbatch args, expected seeded value,
#  non-empty value the hook assigns to prove the setter is live)
#
# "{path}" in either the seed or the expected value is substituted with a
# per-test temporary path.  The assigned value cannot use it: the Lua hook
# is generated once at import, before any test has a working directory.
# ---------------------------------------------------------------------------
_GROUP_D_CASES = [
    # "" would revert these to a default, so each is seeded off it.
    (
        "partition",
        "TEST_PARTITION_CLEAR",
        "Partition",
        f"--partition={PARTITION_NAME}",
        PARTITION_NAME,
        "primary",
    ),
    (
        "qos",
        "TEST_QOS_CLEAR",
        "QOS",
        f"--account={ACCOUNT_NAME} --qos={QOS_NAME}",
        QOS_NAME,
        "normal",
    ),
    (
        "mail_user",
        "TEST_MAILUSER_CLEAR",
        "MailUser",
        "--mail-user=before@example.com --mail-type=END",
        "before@example.com",
        "after@example.com",
    ),
    # "" would clear these outright.
    (
        "std_out",
        "TEST_STDOUT_CLEAR",
        "StdOut",
        "--output={path}",
        "{path}",
        "test157_stdout.set",
    ),
    (
        "std_err",
        "TEST_STDERR_CLEAR",
        "StdErr",
        "--error={path}",
        "{path}",
        "test157_stderr.set",
    ),
    (
        "std_in",
        "TEST_STDIN_CLEAR",
        "StdIn",
        "--input={path}",
        "{path}",
        "test157_stdin.set",
    ),
]


# The same rows keyed by the sentinel that assigns a value instead of "".
_GROUP_D_SET_CASES = [
    (field, sentinel.replace("_CLEAR", "_SET"), show_field, seed, set_value)
    for field, sentinel, show_field, seed, _, set_value in _GROUP_D_CASES
]

# Each field gets a clearing branch and an assigning one, so the guardrail
# is always paired with proof that the setter it guards is reachable.
_GROUP_D_BRANCHES = [
    (field, sentinel, "") for field, sentinel, _, _, _, _ in _GROUP_D_CASES
] + [
    (field, sentinel, set_value)
    for field, sentinel, _, _, set_value in _GROUP_D_SET_CASES
]

_GROUP_D_LUA = "\n\n".join(
    f'    {"if" if index == 0 else "elseif"} c == "{sentinel}" then\n'
    f'        job_desc.{field} = "{value}"'
    for index, (field, sentinel, value) in enumerate(_GROUP_D_BRANCHES)
)

# ---------------------------------------------------------------------------
# Lua hook content - kept inline so the test is self-contained and
# require_config_file() can install it as the slurm-user.
# ---------------------------------------------------------------------------
_LUA_CONTENT = f"""--[[
 job_submit.lua for bug 25211 guardrail regression tests.

 Sentinel values (passed via --comment / scontrol update Comment=<sentinel>):

   Submit-path verification (Group C):
   TEST_SUBMIT_FEATURES_EMPTY    -- set features      = "" in slurm_job_submit
   TEST_SUBMIT_TRESPERNODE_EMPTY -- set tres_per_node = "" in slurm_job_submit
   TEST_SUBMIT_GRES_EMPTY        -- set gres          = "" in slurm_job_submit
   TEST_SUBMIT_COMMENT_EMPTY     -- set comment       = "" in slurm_job_submit

   The Group D branches are generated from _GROUP_D_CASES: per field, a
   _CLEAR sentinel assigning "" and a _SET sentinel assigning a value.

   Non-empty assignment (Group E):
   TEST_SET_FEATURES        -- set features      = "testfeat157"   (E1)
   TEST_SET_TRESPERNODE     -- set tres_per_node = "gres/gpu:1"    (E2)
   TEST_SET_COMMENT         -- set comment       = <new value>     (E3)
   TEST_SUBMIT_SET_FEATURES -- set features on the submit path     (E4)
--]]

function slurm_job_submit(job_desc, part_list, submit_uid)
    -- Group C: on the submit path "" leaves the field NULL, so each of
    -- these assignments must discard the value the user submitted.
    if job_desc.comment == "TEST_SUBMIT_FEATURES_EMPTY" then
        job_desc.features = ""
    elseif job_desc.comment == "TEST_SUBMIT_TRESPERNODE_EMPTY" then
        job_desc.tres_per_node = ""
    elseif job_desc.comment == "TEST_SUBMIT_GRES_EMPTY" then
        job_desc.gres = ""
    elseif job_desc.comment == "TEST_SUBMIT_COMMENT_EMPTY" then
        job_desc.comment = ""

    elseif job_desc.comment == "TEST_SUBMIT_SET_FEATURES" then
        job_desc.features = "testfeat157"
    end
    return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    local c = job_desc.comment or ""

    -- Group D: these fields are not whitelisted, so "" drops to NULL inside
    -- the plugin, while a non-empty value still reaches the controller.
{_GROUP_D_LUA}

    -- Group E: a non-empty assignment must reach the controller.
    elseif c == "TEST_SET_FEATURES" then
        job_desc.features = "testfeat157"

    elseif c == "TEST_SET_TRESPERNODE" then
        job_desc.tres_per_node = "gres/gpu:1"

    elseif c == "TEST_SET_COMMENT" then
        job_desc.comment = "TEST_SET_COMMENT_DONE"

    end

    return slurm.SUCCESS
end

slurm.log_info("bug25211 guardrail test hook loaded")
return slurm.SUCCESS
"""


# ---------------------------------------------------------------------------
# Module-level fixture: install Lua plugin config and start Slurm.
# ---------------------------------------------------------------------------
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", _LUA_CONTENT)
    # Accounting is required so a job's QOS is recorded and visible to
    # scontrol for the D2 guardrail.
    atf.require_accounting(modify=True)
    # A node feature and a fake GPU gres let the Group C jobs submit with
    # Features/TresPerNode already set, so the hook has a value to destroy.
    atf.require_tty(0)
    atf.require_config_parameter("Name", {"gpu": {"File": "/dev/tty0"}}, source="gres")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # An empty partition resolves to the cluster default, so the partition
    # job must sit in a non-default partition for that revert to be
    # observable.
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL", "Default": "YES"},
            PARTITION_NAME: {"Nodes": "ALL"},
        },
    )
    atf.require_nodes(1, [("Gres", "gpu:1"), ("Features", "testfeat157")])
    atf.require_slurm_running()

    # The controller maps an empty qos to "revert to the user's default
    # QOS", so the qos job must hold a non-default QOS for that revert to
    # be observable.
    slurm_user = atf.properties["slurm-user"]
    test_user = atf.properties["test-user"]
    atf.run_command(f"sacctmgr -i add qos {QOS_NAME}", user=slurm_user, fatal=True)
    atf.run_command(
        f"sacctmgr -i add account {ACCOUNT_NAME}", user=slurm_user, fatal=True
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user} account={ACCOUNT_NAME} "
        f"qos={QOS_NAME},normal",
        user=slurm_user,
        fatal=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove user {test_user} account={ACCOUNT_NAME}",
        user=slurm_user,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {ACCOUNT_NAME}", user=slurm_user, quiet=True
    )
    atf.run_command(f"sacctmgr -i remove qos {QOS_NAME}", user=slurm_user, quiet=True)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _submit_held(*, comment=None, extra_args=None):
    """Submit a held batch job and return its job id.

    extra_args is appended verbatim, so a case needing its own --output
    can pass one and it wins over the default.
    """
    args = "--hold -n1 --output=/dev/null --wrap='true'"
    if comment is not None:
        args += f" --comment={comment}"
    if extra_args:
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


# ===========================================================================
# Group C - Submit-path behavior unchanged by the patch
# ===========================================================================
# job_submit_plugins.shtml documents that on the submit path assigning ""
# to a string field leaves it NULL, discarding whatever the user supplied,
# and that the modify path's per-field clear semantics do not apply there.


# The gres alias is sampled alongside tres_per_node because it is
# documented as clearable in its own right on the modify path.
#
# The comment case is the asymmetry at its sharpest: the same assignment
# that clears Comment on modify instead destroys the user's own --comment
# value here, which is the sentinel itself.
_GROUP_C_CASES = [
    ("features", "--constraint=testfeat157", "TEST_SUBMIT_FEATURES_EMPTY", "Features"),
    ("tres_per_node", "--gres=gpu:1", "TEST_SUBMIT_TRESPERNODE_EMPTY", "TresPerNode"),
    ("gres", "--gres=gpu:1", "TEST_SUBMIT_GRES_EMPTY", "TresPerNode"),
    ("comment", "", "TEST_SUBMIT_COMMENT_EMPTY", "Comment"),
]


@pytest.mark.parametrize(
    "seed, sentinel, show_field",
    [case[1:] for case in _GROUP_C_CASES],
    ids=[case[0] for case in _GROUP_C_CASES],
)
def test_C_submit_becomes_null(seed, sentinel, show_field):
    """On submit, assigning "" leaves the field NULL, dropping the user value."""
    job_id = atf.submit_job_sbatch(
        f"--hold -n1 --output=/dev/null {seed} --comment={sentinel} --wrap='true'",
        fatal=True,
    )
    after = _field(job_id, show_field)
    assert after is None, f"{show_field} must be null on submit path, got {after!r}"


# ===========================================================================
# Group D - Non-whitelist guardrails
# ===========================================================================


@pytest.mark.parametrize(
    "sentinel, show_field, seed, expected",
    [case[1:5] for case in _GROUP_D_CASES],
    ids=[case[0] for case in _GROUP_D_CASES],
)
def test_D_non_whitelist_empty_ignored(sentinel, show_field, seed, expected):
    """Lua assigns "" to a non-whitelisted field on modify -> field unchanged."""
    path = f"{os.getcwd()}/{show_field}.path"
    job_id = _submit_held(extra_args=seed.format(path=path))
    before = _field(job_id, show_field)
    want = expected.format(path=path)
    assert (
        before == want
    ), f"pre-condition: {show_field} must be {want!r}, got {before!r}"
    result = _modify(job_id, Comment=sentinel)
    assert result["exit_code"] == 0, (
        f'scontrol update must succeed when Lua sets {show_field} to "", '
        f"stderr: {result['stderr']}"
    )
    after = _field(job_id, show_field)
    assert (
        after == before
    ), f"{show_field} must be unchanged: expected {before!r}, got {after!r}"


# Without this, test_D_non_whitelist_empty_ignored would keep passing if the
# field's Lua setter were renamed or removed, since an assignment that never
# happens also leaves the field unchanged.
@pytest.mark.parametrize(
    "sentinel, show_field, seed, set_value",
    [case[1:] for case in _GROUP_D_SET_CASES],
    ids=[case[0] for case in _GROUP_D_SET_CASES],
)
def test_D_non_whitelist_set_works(sentinel, show_field, seed, set_value):
    """Lua assigns a non-empty value to a non-whitelisted field -> it applies."""
    path = f"{os.getcwd()}/{show_field}.path"
    job_id = _submit_held(extra_args=seed.format(path=path))
    result = _modify(job_id, Comment=sentinel)
    assert result["exit_code"] == 0, (
        f"scontrol update must succeed when Lua sets {show_field} to "
        f"{set_value!r}, stderr: {result['stderr']}"
    )
    # scontrol resolves a relative stdio path against the job's working
    # directory, which is where the job was submitted from.
    want = f"{os.getcwd()}/{set_value}" if "{path}" in seed else set_value
    after = _field(job_id, show_field)
    assert after == want, f"{show_field} must be {want!r}, got {after!r}"


# ===========================================================================
# Group E - Non-empty assignment still takes effect
# ===========================================================================
# job_submit_plugins.shtml documents job_desc as input/output and the hook
# as able to modify the job parameters supplied by the user.  Narrowing
# which fields "" clears must leave ordinary assignment working.

_GROUP_E_CASES = [
    ("features", "TEST_SET_FEATURES", "Features", "testfeat157"),
    ("tres_per_node", "TEST_SET_TRESPERNODE", "TresPerNode", "gres/gpu:1"),
    ("comment", "TEST_SET_COMMENT", "Comment", "TEST_SET_COMMENT_DONE"),
]


@pytest.mark.parametrize(
    "sentinel, show_field, expected",
    [case[1:] for case in _GROUP_E_CASES],
    ids=[case[0] for case in _GROUP_E_CASES],
)
def test_E_set_on_modify(sentinel, show_field, expected):
    """Lua assigns a non-empty value on modify -> the field takes that value."""
    job_id = _submit_held()
    before = _field(job_id, show_field)
    assert (
        before != expected
    ), f"pre-condition: {show_field} must not already be {expected!r}"
    result = _modify(job_id, Comment=sentinel)
    assert result["exit_code"] == 0, f"scontrol update failed: {result['stderr']}"
    after = _field(job_id, show_field)
    assert after == expected, f"{show_field} must be {expected!r}, got {after!r}"


def test_E_set_on_submit():
    """Lua assigns a non-empty value on submit -> the field takes that value."""
    job_id = atf.submit_job_sbatch(
        "--hold -n1 --output=/dev/null --comment=TEST_SUBMIT_SET_FEATURES "
        "--wrap='true'",
        fatal=True,
    )
    features_after = _field(job_id, "Features")
    assert (
        features_after == "testfeat157"
    ), f"Features must be set on submit path, got {features_after!r}"
