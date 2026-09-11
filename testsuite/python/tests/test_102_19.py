############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_102_19.py - sacctmgr declarative association and QOS load.

Exercises the limited declarative scope from ticket 23787. Listed account and
user associations reset omitted configurable association fields to defaults,
and listed standalone QOS rows reset omitted configurable QOS fields. QOS and
TRES values use replacement semantics. Unlisted entities are unchanged.

Account and user record metadata, Parent handling, and cluster metadata/root
association fields retain normal additive load behavior. Missing listed
entities are created through normal load behavior. clean and declarative are
mutually exclusive.

Additive load without the declarative flag is test_102_18.py.

Requires Slurm 26.11+ (sacctmgr declarative load).
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Entity names (same as test_102_18 for consistency)
test_cluster = "test_cluster1"
test_cluster_second = "test_cluster_second"
test_account1 = "test_acct1"
test_account2 = "test_acct2"
test_user1 = "test_user1"
test_user2 = "test_user2"
test_qos1 = "test_qos1"
test_qos2 = "test_qos2"
test_qos3 = "test_qos3"

CLASS_CAPACITY = "Capacity"

# Flag for declarative load (default + file overlay; omission = default)
DECLARATIVE_LOAD_FLAG = "declarative"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sacctmgr",
        reason="Ticket 23787: sacctmgr declarative load requires Slurm 26.11+",
    )
    atf.require_accounting(modify=True)
    # Needed by the user-record metadata test. Set here rather than in a
    # per-test fixture, since in auto-config mode this rewrites slurmdbd.conf
    # and restarts the daemon, and doing that partway through the module would
    # leave every later test running under a config the earlier ones did not
    # have.
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    """Grant the slurm user root access, then remove test entities."""
    atf.run_command(
        f"sacctmgr -i add user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
    )
    yield
    atf.run_command(
        f"sacctmgr -i remove user {test_user1},{test_user2}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove account {test_account1},{test_account2}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove cluster {test_cluster},{test_cluster_second}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove qos {test_qos1},{test_qos2},{test_qos3}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture
def live_cluster():
    """The configured cluster, snapshotted and restored around the test.

    A couple of tests have to load into the cluster slurmctld is running
    as, since that is the only way scontrol show assoc_mgr can be used to
    see what was packed. That means writing to accounting rows the site
    owns, so dump first and load the dump back afterwards, the way
    test_102_20.py does for its destructive cases.
    """
    atf.require_auto_config("declarative load writes to the configured cluster")
    cluster = atf.get_config_parameter("ClusterName")
    snap = "live_cluster_snapshot.cfg"
    atf.run_command(
        f"sacctmgr dump {cluster} file={snap}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield cluster
    atf.run_command(
        f"sacctmgr -i load file={snap}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def write_load_file(path, content):
    """Write a sacctmgr load file as the slurm user (remote-exec safe)."""
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def parse_tres(tres_str):
    """Parse a TRES string like 'cpu=2,mem=1024M' into {name: value}."""
    result = {}
    for item in tres_str.strip().split(","):
        item = item.strip()
        if "=" in item:
            name, value = item.split("=", 1)
            result[name.strip()] = value.strip()
    return result


def _load(cfg_path, declarative=False, file_equals=False, cluster=None):
    """Run sacctmgr load (optionally declarative) as the slurm user."""
    flag = f" {DECLARATIVE_LOAD_FLAG}" if declarative else ""
    cluster_arg = f" cluster={cluster}" if cluster else ""
    target = f"file={cfg_path}" if file_equals else cfg_path
    return atf.run_command(
        f"sacctmgr -i load {target}{flag}{cluster_arg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _declarative_load(cfg_path, file_equals=False, cluster=None):
    """Run sacctmgr load with the declarative flag (path then keyword)."""
    return _load(cfg_path, declarative=True, file_equals=file_equals, cluster=cluster)


def _qos_list_row(qos_name, fmt):
    """Return pipe-split fields for qos_name from sacctmgr list qos, or None."""
    out = atf.run_command_output(
        f"sacctmgr -n -P list qos format={fmt}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for ln in out.strip().split("\n"):
        parts = [p.strip() for p in ln.split("|")]
        if parts and parts[0] == qos_name:
            return parts
    return None


def _assoc_list_account_row_parts(out, num_fields, label="account association"):
    """Parse one pipe-delimited sacctmgr list assoc row (account-level only)."""
    lines = [ln for ln in out.strip().split("\n") if ln.strip()]
    assert len(lines) == 1, f"Expect one {label} row, got {len(lines)}"
    parts = [p.strip() for p in lines[0].split("|")]
    assert (
        len(parts) == num_fields
    ), f"Expect {num_fields} fields for {label}, got {len(parts)}"
    return parts


def _cfg(body):
    """Wrap load-file body lines in the cluster header every case shares."""
    return f"Cluster - '{test_cluster}'\n{body}"


def _assoc_row(scope, fmt):
    """Return the single assoc row matching scope, as stripped fields."""
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc {scope} cluster={test_cluster} format={fmt}",
        fatal=True,
    )
    lines = [ln for ln in out.strip().split("\n") if ln.strip()]
    assert len(lines) == 1, f"Expect one assoc row for '{scope}', got {len(lines)}"
    parts = [p.strip() for p in lines[0].split("|")]
    assert len(parts) == len(
        fmt.split(",")
    ), f"Expect a field per name in '{fmt}', got {parts}"
    return parts


def _expect_fields(parts, checks, label):
    """Assert a parsed row against a list of (field index, expectation).

    An expectation is a literal to compare equal, with "" meaning the field
    has to be empty, or one of ("has", s), ("hasn't", s) for a substring that
    must or must not appear, or ("set", None) for any non-empty value. List
    valued columns are checked by substring because a QOS column can also
    carry whatever the cluster grants by default.
    """
    for idx, want in checks:
        got = parts[idx]
        if isinstance(want, tuple):
            op, val = want
            if op == "has":
                assert (
                    val in got
                ), f"{label}: field {idx} should hold {val}, got {got!r}"
            elif op == "hasn't":
                assert (
                    val not in got
                ), f"{label}: field {idx} should not hold {val}, got {got!r}"
            else:
                assert got, f"{label}: field {idx} should have been set"
        elif want == "":
            assert not got, f"{label}: field {idx} should be empty, got {got!r}"
        else:
            assert got == want, f"{label}: field {idx} should be {want}, got {got!r}"


def _qos_cfg(qos_spec):
    """A load file holding one QOS line, plus the account a load needs."""
    return (
        f"QOS - '{test_qos1}':{qos_spec}\n"
        f"Cluster - '{test_cluster}'\n"
        f"Parent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50\n"
    )


def _assert_cleared_limits_print_as_minus_one(out):
    """A cleared limit is reported as -1, never as an unsigned sentinel.

    The load prints integer limits with %d, maps the INFINITE floats to -1
    and rewrites the INFINITE64 TRES counts, so none of the three sentinels
    should reach the reader. 4294967295 also catches the float spelling
    4294967295.000000.
    """
    for sentinel in ("4294967295", "18446744073709551615"):
        assert (
            sentinel not in out
        ), f"A cleared limit must print as -1, not {sentinel}: {out!r}"
    assert "-1" in out, f"The load should report the cleared limits as -1: {out!r}"


def _assoc_ids():
    """{(account, user): id} for the test cluster's associations."""
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc cluster={test_cluster} format=Account,User,ID",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    ids = {}
    for line in out.strip().split("\n"):
        if not line.strip():
            continue
        account, user, assoc_id = [p.strip() for p in line.split("|")]
        ids[(account, user)] = assoc_id
    return ids


def _qos_ids():
    """{name: id} for every stored QOS."""
    out = atf.run_command_output(
        "sacctmgr -n -P list qos format=Name,ID",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    ids = {}
    for line in out.strip().split("\n"):
        if not line.strip():
            continue
        name, qos_id = [p.strip() for p in line.split("|")]
        ids[name] = qos_id
    return ids


def _qos_row(fmt, label, name=test_qos1):
    """Return a QOS row, failing if the load did not leave one."""
    row = _qos_list_row(name, fmt)
    assert row is not None, f"Expected a QOS row for {name} {label}"
    return row


def _entity_row(list_args, fmt, fold_case=False):
    """Return the single sacctmgr list row for an entity, as stripped fields."""
    out = atf.run_command_output(
        f"sacctmgr -n -P list {list_args} format={fmt}",
        fatal=True,
    )
    lines = [ln for ln in out.strip().split("\n") if ln.strip()]
    assert len(lines) == 1, f"Expect one '{list_args}' row, got {len(lines)}"
    parts = [p.strip() for p in lines[0].split("|")]
    assert len(parts) == len(
        fmt.split(",")
    ), f"Expect a field per name in '{fmt}', got {parts}"
    return [p.lower() for p in parts] if fold_case else parts


def _read_row(kind, target, fmt, label):
    """Read one row to check: an association scope, or a QOS by name."""
    if kind == "qos":
        return _qos_row(fmt, label, name=target)
    return _assoc_row(target, fmt)


def _tres_replace_files(kind, field):
    """Return (full_cfg_text, partial_cfg_text) for a TRES replace case."""
    if kind == "qos":
        full = (
            f"QOS - '{test_qos1}':Priority=10:{field}=cpu=1,mem=1GB\n"
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
        )
        partial = (
            f"QOS - '{test_qos1}':Priority=10:{field}=cpu=2\n"
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
        )
    else:
        full = (
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50:{field}=cpu=1,mem=1GB\n"
        )
        partial = (
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50:{field}=cpu=2\n"
        )
    return full, partial


def _read_tres(kind, field):
    """Return the parsed TRES dict for the entity/field under test."""
    if kind == "qos":
        row = _qos_list_row(test_qos1, f"Name,{field}")
        assert row is not None, f"Expected QOS row for {field}"
        return parse_tres(row[1])
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format={field}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return parse_tres(out)


# ---------------------------------------------------------------------------
# Declarative tests
# ---------------------------------------------------------------------------


DECL_ASSOC_RESET_CASES = [
    pytest.param(
        {
            "add_qos": [test_qos1, test_qos2],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50:GrpJobs=10:MaxJobs=20"
            f":QOS='{test_qos1},{test_qos2}':DefaultQOS='{test_qos1}'\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':GrpJobs=3\n",
            "scope": f"account={test_account1} user=''",
            "fmt": "Account,Fairshare,GrpJobs,MaxJobs,QOS,DefaultQOS",
            "before": [
                (1, "50"),
                (2, "10"),
                (3, "20"),
                (4, ("has", test_qos1)),
                (4, ("has", test_qos2)),
                (5, test_qos1),
            ],
            "after": [
                (0, test_account1),
                (1, "1"),
                (2, "3"),
                (3, ""),
                (4, ("hasn't", test_qos1)),
                (4, ("hasn't", test_qos2)),
                (5, ""),
            ],
        },
        id="account-limits-and-qos",
    ),
    pytest.param(
        {
            "add_qos": [test_qos1],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"User - '{test_user1}':DefaultAccount='{test_account1}'"
            f":Fairshare=25:MaxJobs=5:QOS='{test_qos1}'\n",
            "partial": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"User - '{test_user1}':Fairshare=100\n",
            "scope": f"user={test_user1} account={test_account1}",
            "fmt": "User,Fairshare,MaxJobs,QOS",
            "before": [(1, "25"), (2, "5"), (3, ("has", test_qos1))],
            "after": [
                (0, test_user1),
                (1, "100"),
                (2, ""),
                (3, ("hasn't", test_qos1)),
            ],
        },
        id="user-limits-and-qos",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50:GrpTRES=cpu=1,mem=1GB\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=100\n",
            "scope": f"account={test_account1} user=''",
            # Account comes first so the row still prints once GrpTRES is
            # cleared; a lone empty column would come back as no row at all.
            "fmt": "Account,GrpTRES",
            "before": [(1, ("has", "cpu=1")), (1, ("has", "mem="))],
            "after": [
                (0, test_account1),
                (1, ("hasn't", "cpu")),
                (1, ("hasn't", "mem")),
            ],
        },
        id="account-grp-tres",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50"
            f":TresDecayHalfLife=cpu=3600,mem=0\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=100\n",
            "scope": f"account={test_account1} user=''",
            # Account first so the row still prints once the half-life is gone.
            "fmt": "Account,TresDecayHalfLife",
            # mem=0 is a setting, "that TRES never decays", not an unset field.
            # It has to be stored by the additive load and dropped by the
            # declarative one: clearing a half-life removes the entry rather
            # than writing a zero, which would mean something else entirely.
            "before": [(1, ("has", "cpu=3600")), (1, ("has", "mem=0"))],
            "after": [
                (0, test_account1),
                (1, ("hasn't", "cpu")),
                (1, ("hasn't", "mem")),
            ],
        },
        id="account-tres-decay-half-life",
    ),
    pytest.param(
        {
            "add_qos": [test_qos1, test_qos2],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50"
            f":QOS='{test_qos1},{test_qos2}'\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=100\n",
            "scope": f"account={test_account1} user=''",
            "fmt": "Fairshare,QOS",
            "before": [(0, "50"), (1, ("has", test_qos1)), (1, ("has", test_qos2))],
            "after": [
                (0, "100"),
                (1, ("hasn't", test_qos1)),
                (1, ("hasn't", test_qos2)),
            ],
        },
        id="account-qos-only",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50:GrpJobs=10\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=100\n",
            "scope": f"account={test_account1} user=''",
            "fmt": "Fairshare,GrpJobs",
            "before": [(0, "50"), (1, "10")],
            "after": [(0, "100"), (1, "")],
            "file_equals": True,
        },
        id="file-equals-syntax",
    ),
]


@pytest.mark.parametrize("case", DECL_ASSOC_RESET_CASES)
def test_declarative_assoc_resets_omitted_attributes(case):
    """A declarative load applies what the association line lists and resets
    what it leaves out.

    Each case loads a full file additively, pins the resulting state so the
    later assertions cannot pass on a value that was never set, then loads a
    partial file declaratively and checks the fields it named came from the
    file while the rest went back to their defaults. An omitted QOS is
    checked by substring, since the column can still show whatever the
    cluster grants by default.
    """
    if case["add_qos"]:
        atf.run_command(
            f"sacctmgr -i add qos {','.join(case['add_qos'])}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    cfg_full = "decl_reset_full.cfg"
    cfg_partial = "decl_reset_partial.cfg"
    write_load_file(cfg_full, _cfg(case["full"]))
    write_load_file(cfg_partial, _cfg(case["partial"]))

    _load(cfg_full)
    _expect_fields(
        _assoc_row(case["scope"], case["fmt"]),
        case["before"],
        "before the declarative load",
    )

    _declarative_load(cfg_partial, file_equals=case.get("file_equals", False))
    _expect_fields(
        _assoc_row(case["scope"], case["fmt"]),
        case["after"],
        "after the declarative load",
    )


DECL_QOS_RESET_CASES = [
    pytest.param(
        {
            "add_qos": [],
            "full": "Priority=10:GrpJobs=5",
            "partial": "Priority=77",
            "fmt": "Name,Priority,GrpJobs",
            "before": [(1, "10"), (2, "5")],
            "after": [(1, "77"), (2, "")],
        },
        id="grp-jobs",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": "Priority=10:PreemptMode='cancel'",
            "partial": "Priority=88",
            "fmt": "Name,Priority,PreemptMode",
            "before": [(1, "10"), (2, "cancel")],
            # An INFINITE init used to truncate to 0xffff here, which listed
            # as gang,unknown and corrupted the next dump. Omitted has to
            # come back as OFF, which lists as cluster.
            "after": [(1, "88"), (2, "cluster")],
        },
        id="preempt-mode",
    ),
    pytest.param(
        {
            # Preempt names a QOS, so the target has to exist before the load.
            "add_qos": [test_qos2],
            "full": f"Description='decl qos desc':GrpCPUs=2:Preempt='{test_qos2}'"
            ":Flags=DenyOnLimit:Priority=10",
            "partial": "Priority=77",
            "fmt": "Name,Description,GrpTRES,Preempt,Flags,Priority",
            "before": [
                (1, "decl qos desc"),
                (2, ("set", None)),
                (3, ("has", test_qos2)),
                (4, ("has", "DenyOnLimit")),
                (5, "10"),
            ],
            # Description and Flags are the two the file is not the authority
            # on, so they survive being left out; the limits do not.
            "after": [
                (1, "decl qos desc"),
                (2, ""),
                (3, ""),
                (4, ("has", "DenyOnLimit")),
                (5, "77"),
            ],
        },
        id="description-and-flags-kept",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": "Priority=10:MaxTRESPerJob=cpu=3",
            "partial": "Priority=77",
            "fmt": "Name,Priority,MaxTRESPerJob",
            "before": [(1, "10"), (2, ("set", None))],
            "after": [(1, "77"), (2, "")],
        },
        id="max-tres-per-job",
    ),
    pytest.param(
        {
            "add_qos": [],
            "full": "Priority=10:TresDecayHalfLife=cpu=3600,mem=0",
            "partial": "Priority=77",
            "fmt": "Name,Priority,TresDecayHalfLife",
            # mem=0 means that TRES never decays, which is a stored setting and
            # not an unset field, so the reset has to drop the entry outright.
            "before": [(1, "10"), (2, ("has", "cpu=3600")), (2, ("has", "mem=0"))],
            "after": [(1, "77"), (2, "")],
        },
        id="tres-decay-half-life",
    ),
]


@pytest.mark.parametrize("case", DECL_QOS_RESET_CASES)
def test_declarative_qos_resets_omitted_limits(case):
    """A declarative QOS line applies what it lists and resets what it omits.

    Each case loads a full QOS line additively and pins the result, so the
    later assertions cannot pass on a field that was never set, then loads a
    line carrying only Priority and checks which fields went back to their
    create-time defaults and which were left alone.
    """
    if case["add_qos"]:
        atf.run_command(
            f"sacctmgr -i add qos {','.join(case['add_qos'])}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    cfg_full = "decl_qos_full.cfg"
    cfg_partial = "decl_qos_partial.cfg"
    write_load_file(cfg_full, _qos_cfg(case["full"]))
    write_load_file(cfg_partial, _qos_cfg(case["partial"]))

    _load(cfg_full)
    _expect_fields(
        _qos_row(case["fmt"], "after the additive load"),
        case["before"],
        "before the declarative load",
    )

    _declarative_load(cfg_partial)
    _expect_fields(
        _qos_row(case["fmt"], "after the declarative load"),
        case["after"],
        "after the declarative load",
    )


def test_declarative_modify_preserves_assoc_and_qos_ids():
    """A declarative load modifies in place, keeping database identity.

    Every other assertion in this module reads values, so an implementation
    that deleted and recreated each listed row would satisfy all of them
    while orphaning the job and usage records that reference the old ids.
    Identity is what separates declarative from clean, so it is read
    directly.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_ids_full.cfg"
    cfg_partial = "decl_ids_partial.cfg"
    write_load_file(
        cfg_full,
        f"QOS - '{test_qos1}':Priority=42:GrpJobs=9\n"
        f"Cluster - '{test_cluster}'\n"
        f"Parent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50:GrpJobs=10:MaxJobs=20"
        f":QOS='{test_qos1},{test_qos2}'\n"
        f"Parent - '{test_account1}'\n"
        f"User - '{test_user1}':DefaultAccount='{test_account1}'"
        f":Fairshare=25:MaxJobs=5:QOS='{test_qos1}'\n",
    )
    write_load_file(
        cfg_partial,
        f"QOS - '{test_qos1}':Priority=43\n"
        f"Cluster - '{test_cluster}'\n"
        f"Parent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=99\n"
        f"Parent - '{test_account1}'\n"
        f"User - '{test_user1}':Fairshare=98\n",
    )

    _load(cfg_full)
    before_assoc = _assoc_ids()
    before_qos = _qos_ids()
    assert (test_account1, "") in before_assoc, "Setup: the account association exists"
    assert test_qos1 in before_qos, "Setup: the QOS exists"

    _declarative_load(cfg_partial)

    # Pin that the reset really happened, so the ids below are not unchanged
    # merely because the declarative load was a no-op.
    parts = _assoc_row(f"account={test_account1} user=''", "Fairshare,MaxJobs")
    assert parts[0] == "99", "the declarative account line must have applied"
    assert parts[1] == "", "an omitted MaxJobs must have been reset"
    assert (
        _qos_row("Name,Priority,GrpJobs", "after the declarative load")[2] == ""
    ), "an omitted GrpJobs on the QOS line must have been reset"

    assert (
        _assoc_ids() == before_assoc
    ), "a declarative load must modify associations in place, not recreate them"
    assert (
        _qos_ids() == before_qos
    ), "a declarative load must modify QOS in place, not recreate them"


# Association limits a declarative line resets, each with a value that
# differs from the built-in default. Fairshare, Priority and DefaultQOS read
# back as 1, 0 and a granted QOS rather than empty when unset, so a reset
# implementation that only checked for an empty stored value would leave them
# alone. The sacctmgr load option and the list assoc format field are not
# always spelled the same, so each entry carries both.
#
# {load option: (list assoc format field, value)}
ASSOC_RESET_ALL_FIELDS = {
    "Fairshare": ("Fairshare", "77"),
    "GrpJobs": ("GrpJobs", "120"),
    "GrpJobsAccrue": ("GrpJobsAccrue", "12"),
    "GrpSubmitJobs": ("GrpSubmit", "130"),
    "GrpTRES": ("GrpTRES", "cpu=1"),
    "GrpTRESMins": ("GrpTRESMins", "cpu=2"),
    "GrpTRESRunMins": ("GrpTRESRunMins", "cpu=3"),
    "GrpWall": ("GrpWall", "60"),
    "MaxJobs": ("MaxJobs", "160"),
    "MaxJobsAccrue": ("MaxJobsAccrue", "15"),
    "MaxSubmitJobs": ("MaxSubmit", "170"),
    "MaxTRESPerJob": ("MaxTRES", "cpu=4"),
    "MaxTRESPerNode": ("MaxTRESPerNode", "cpu=5"),
    "MaxWall": ("MaxWall", "70"),
    "MinPrioThresh": ("MinPrioThresh", "30"),
    "Priority": ("Priority", "18"),
    "TresDecayHalfLife": ("TresDecayHalfLife", "cpu=3600"),
    "DefaultQOS": ("DefaultQOS", test_qos1),
}


def test_declarative_assoc_resets_every_configurable_field():
    """A declarative association line resets every configurable limit, not
    just the ones whose unset value happens to read back empty.

    The association half of what
    test_declarative_qos_resets_every_configurable_field does for QOS, and
    built the same way: test_account2 is listed in the additive file carrying
    no options and is never named declaratively, so its stored values are
    exactly what an omitted field on test_account1 must reset to.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    load_options = list(ASSOC_RESET_ALL_FIELDS)
    fmt_fields = [ASSOC_RESET_ALL_FIELDS[opt][0] for opt in load_options]
    fmt = f"Account,{','.join(fmt_fields)}"
    control_scope = f"account={test_account2} user=''"
    target_scope = f"account={test_account1} user=''"

    full_spec = ":".join(
        f"{opt}={ASSOC_RESET_ALL_FIELDS[opt][1]}" for opt in load_options
    )
    cfg_full = "decl_assoc_reset_all_full.cfg"
    cfg_partial = "decl_assoc_reset_all_partial.cfg"
    write_load_file(
        cfg_full,
        _cfg(
            f"Parent - 'root'\n"
            f"Account - '{test_account1}':{full_spec}\n"
            f"Account - '{test_account2}'\n"
        ),
    )
    write_load_file(
        cfg_partial,
        _cfg(f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=99\n"),
    )

    _load(cfg_full)
    control = dict(zip(fmt_fields, _assoc_row(control_scope, fmt)[1:]))
    loaded = dict(zip(fmt_fields, _assoc_row(target_scope, fmt)[1:]))
    for field in fmt_fields:
        assert loaded[field] != control[field], (
            f"Setup: {field} must differ from the default so the later reset "
            "assertion cannot pass by finding it already untouched"
        )

    result = _declarative_load(cfg_partial)
    _assert_cleared_limits_print_as_minus_one(result["stdout"])
    after = dict(zip(fmt_fields, _assoc_row(target_scope, fmt)[1:]))
    assert after["Fairshare"] == "99", "Fairshare from the file must apply"
    for field in fmt_fields:
        if field == "Fairshare":
            continue
        assert after[field] == control[field], (
            f"{field} left out of the declarative line must reset to its "
            f"default {control[field]!r}, got {after[field]!r}"
        )


# Every field DECL_QOS_RESET_CASES above does not already cover. GraceTime,
# Priority and UsageFactor read back as 0, 0 and 1 (not empty) when unset,
# so a reset implementation that only checks for an empty stored value would
# leave them alone; the rest round out the set of configurable QOS limits.
QOS_RESET_ALL_FIELDS = {
    "GraceTime": "70",
    "GrpJobsAccrue": "2",
    "GrpJobs": "120",
    "GrpSubmitJobs": "130",
    "GrpTres": "cpu=1",
    "GrpTresMins": "cpu=2",
    "GrpTresRunMins": "cpu=3",
    "GrpWall": "60",
    "LimitFactor": "2.0",
    "MaxJobsPerAccount": "160",
    "MaxJobsPerUser": "250",
    "MaxJobsAccruePerAccount": "160",
    "MaxJobsAccruePerUser": "250",
    "MaxSubmitJobsPerAccount": "180",
    "MaxSubmitJobsPerUser": "4",
    "MaxTresMinsPerJob": "cpu=1",
    "MaxTresPerAccount": "cpu=2",
    "MaxTresPerJob": "cpu=3",
    "MaxTresPerNode": "cpu=4",
    "MaxTresPerUser": "cpu=5",
    "MaxTRESRunMinsPerAccount": "cpu=1",
    "MaxTRESRunMinsPerUser": "cpu=3",
    "MaxWallDurationPerJob": "30",
    "MinPrioThresh": "30",
    "MinTRESPerJob": "cpu=10",
    "PreemptExemptTime": "60",
    "Priority": "10010",
    "TresDecayHalfLife": "cpu=3600",
    "UsageFactor": "0.5",
    "UsageThreshold": "2.5",
}


def test_declarative_qos_resets_every_configurable_field():
    """A declarative QOS line resets every configurable limit, not just the
    ones whose unset value happens to read back empty.

    Rather than hard-code the built-in defaults, this reads them off a QOS
    created fresh in the same test: test_qos3 is never loaded into, so its
    stored values are exactly what an omitted field must reset to. Every
    field in QOS_RESET_ALL_FIELDS is loaded additively onto test_qos1, pinned
    to prove it actually left the default, then a declarative line naming
    only Priority is loaded and every other field is checked against the
    fresh QOS's value instead of a separately maintained expectation.
    """
    control_name = test_qos3
    atf.run_command(
        f"sacctmgr -i add qos {control_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    field_names = list(QOS_RESET_ALL_FIELDS.keys())
    fmt = f"Name,{','.join(field_names)}"

    control_row = _qos_row(fmt, "control QOS defaults", name=control_name)
    control_values = dict(zip(field_names, control_row[1:]))

    full_spec = ":".join(f"{k}={v}" for k, v in QOS_RESET_ALL_FIELDS.items())
    cfg_full = "decl_qos_reset_all_full.cfg"
    cfg_partial = "decl_qos_reset_all_partial.cfg"
    write_load_file(cfg_full, _qos_cfg(full_spec))
    write_load_file(cfg_partial, _qos_cfg("Priority=77"))

    _load(cfg_full)
    loaded_row = _qos_row(fmt, "after the additive load")
    loaded_values = dict(zip(field_names, loaded_row[1:]))
    for field in field_names:
        assert loaded_values[field] != control_values[field], (
            f"Setup: {field} must differ from the default so the later "
            "reset assertion cannot pass by finding it already untouched"
        )

    result = _declarative_load(cfg_partial)
    _assert_cleared_limits_print_as_minus_one(result["stdout"])
    after_row = _qos_row(fmt, "after the declarative load")
    after_values = dict(zip(field_names, after_row[1:]))
    assert after_values["Priority"] == "77", "Priority from the file must apply"
    for field in field_names:
        if field == "Priority":
            continue
        assert after_values[field] == control_values[field], (
            f"{field} left out of the declarative line must reset to its "
            f"default {control_values[field]!r}, got {after_values[field]!r}"
        )


def test_declarative_user_record_metadata_stays_additive():
    """User-record metadata keeps normal additive behavior.

    Covers every field the man page lists as preserved when omitted:
    AdminLevel, DefaultAccount, DefaultWCKey, Coordinator and WCKey (the
    user-record fields), plus association Comment. Coordinator and WCKey
    are list-valued, which is the shape most at risk from a reset-omitted
    -fields change, and losing a coordinator is a privilege change rather
    than a limit change.

    The man page promises both halves of that sentence - left alone when
    omitted, and still updated normally when given - so a final load carries
    the fields and checks the new values land. An implementation that kept
    the first half by dropping these fields from the declarative modify
    would break the second.
    """
    test_wckey = f"{test_user1}_wckey"
    other_wckey = f"{test_user1}_other_wckey"
    acct1_comment = "acct1 initial comment"
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:Comment='{acct1_comment}'
Account - '{test_account2}':Fairshare=30
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account2}':DefaultWCKey='{test_wckey}':WCKeys='{other_wckey}':AdminLevel=Operator:Coordinator='{test_account2}':Fairshare=25:MaxJobs=15:QOS='{test_qos1}'
Parent - '{test_account2}'
User - '{test_user1}':Fairshare=10
"""
    cfg_full = "decl_user_rec_full.cfg"
    write_load_file(cfg_full, config_full)
    atf.run_command(
        f"sacctmgr -i load {cfg_full}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out_before = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,AdminLevel,DefaultA,DefaultW",
        fatal=True,
    )
    parts_before = [p.strip() for p in out_before.strip().split("|")]
    assert len(parts_before) == 4, "Expect User|AdminLevel|DefaultA|DefaultW"
    assert parts_before[1] == "Operator", "Initial AdminLevel should be Operator"
    assert parts_before[2] == test_account2, "Initial DefaultAccount from file"
    assert parts_before[3] == test_wckey, "Initial DefaultWCKey from file"

    coord_before = atf.run_command_output(
        "sacctmgr -n -P list user format=User,Coordinator withcoordinator",
        fatal=True,
    )
    assert (
        f"{test_user1}|{test_account2}" in coord_before
    ), f"Initial Coordinator should be {test_account2}, got: {coord_before!r}"

    wckeys_before = atf.run_command_output(
        f"sacctmgr -n -P list wckeys user={test_user1} cluster={test_cluster} "
        "format=WCKey",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert test_wckey in wckeys_before, "Initial default WCKey should be stored"
    assert other_wckey in wckeys_before, "Initial non-default WCKey should be stored"

    comment_before = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Comment",
        fatal=True,
    )
    assert (
        comment_before.strip() == acct1_comment
    ), "Initial association Comment from file"

    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Account - '{test_account2}':Fairshare=30
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=100
"""
    cfg_partial = "decl_user_rec_partial.cfg"
    write_load_file(cfg_partial, config_partial)
    _declarative_load(cfg_partial)
    out_after = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,AdminLevel,DefaultA,DefaultW",
        fatal=True,
    )
    parts = [p.strip() for p in out_after.strip().split("|")]
    assert len(parts) == 4, "Expect User|AdminLevel|DefaultA|DefaultW"
    assert parts[0] == test_user1, "row should be for the loaded user"
    assert parts[1] == "Operator", "Omitted AdminLevel should remain Operator"
    assert parts[2] == test_account2, "Omitted DefaultAccount should remain unchanged"
    assert parts[3] == test_wckey, "Omitted DefaultWCKey should remain unchanged"
    other_assoc = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} "
        f"account={test_account2} cluster={test_cluster} format=Fairshare",
        fatal=True,
    )
    assert (
        other_assoc.strip() == "10"
    ), "Unlisted association for a listed user should remain unchanged"

    coord_after = atf.run_command_output(
        "sacctmgr -n -P list user format=User,Coordinator withcoordinator",
        fatal=True,
    )
    assert (
        f"{test_user1}|{test_account2}" in coord_after
    ), f"Omitted Coordinator should remain {test_account2}, got: {coord_after!r}"

    wckeys_after = atf.run_command_output(
        f"sacctmgr -n -P list wckeys user={test_user1} cluster={test_cluster} "
        "format=WCKey",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert test_wckey in wckeys_after, "Omitted default WCKey should remain stored"
    assert other_wckey in wckeys_after, "Omitted non-default WCKey should remain stored"

    comment_after = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Comment",
        fatal=True,
    )
    assert (
        comment_after.strip() == acct1_comment
    ), "Omitted association Comment should remain unchanged"

    new_comment = "acct1 updated comment"
    config_given = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:Comment='{new_comment}'
Account - '{test_account2}':Fairshare=30
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account1}':DefaultWCKey='{other_wckey}':AdminLevel=Administrator:Coordinator='{test_account1}':Fairshare=100
"""
    cfg_given = "decl_user_rec_given.cfg"
    write_load_file(cfg_given, config_given)
    _declarative_load(cfg_given)

    out_given = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,AdminLevel,DefaultA,DefaultW",
        fatal=True,
    )
    parts_given = [p.strip() for p in out_given.strip().split("|")]
    assert len(parts_given) == 4, "Expect User|AdminLevel|DefaultA|DefaultW"
    assert parts_given[1] == "Administrator", "AdminLevel on the line must apply"
    assert parts_given[2] == test_account1, "DefaultAccount on the line must apply"
    assert parts_given[3] == other_wckey, "DefaultWCKey on the line must apply"

    coord_given = atf.run_command_output(
        "sacctmgr -n -P list user format=User,Coordinator withcoordinator",
        fatal=True,
    )
    coord_rows = [
        ln for ln in coord_given.strip().split("\n") if ln.startswith(f"{test_user1}|")
    ]
    assert coord_rows, f"Expect a coordinator row for {test_user1}: {coord_given!r}"
    assert (
        test_account1 in coord_rows[0]
    ), f"Coordinator on the line must apply, got: {coord_rows[0]!r}"

    comment_given = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Comment",
        fatal=True,
    )
    assert (
        comment_given.strip() == new_comment
    ), "Association Comment on the line must apply"


def test_declarative_cluster_root_assoc_stays_additive():
    """Cluster root-association fields retain additive load behavior."""
    config_full = f"""Cluster - '{test_cluster}':Fairshare=100:QOS='{test_qos1}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg1 = "decl_cluster_full.cfg"
    write_load_file(cfg1, config_full)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    config_partial = f"""Cluster - '{test_cluster}':Fairshare=200
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    cfg2 = "decl_cluster_partial.cfg"
    write_load_file(cfg2, config_partial)
    atf.run_command(
        f"sacctmgr -i load {cfg2} {DECLARATIVE_LOAD_FLAG}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out_root = atf.run_command_output(
        f"sacctmgr -n -P list assoc account=root user='' "
        f"cluster={test_cluster} format=Fairshare,QOS",
        fatal=True,
    )
    root_parts = _assoc_list_account_row_parts(out_root, 2, "root association")
    assert root_parts[0] == "200", "Cluster line Fairshare on root association"
    assert test_qos1 in root_parts[1], "Omitted root QOS should remain unchanged"
    out_acct = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} cluster={test_cluster} format=Fairshare",
        fatal=True,
    )
    assert (
        out_acct.strip() == "50"
    ), "Account line in file: Fairshare 50 on account assoc"


DECL_METADATA_ADDITIVE_CASES = [
    pytest.param(
        {
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Description='Custom Desc'"
            f":Organization='CustomOrg':Fairshare=50\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=100\n",
            "list_args": f"account {test_account1}",
            "fmt": "Account,Description,Organization",
            "fold_case": True,
            "before": [(1, "custom desc"), (2, "customorg")],
            "after": [(0, test_account1), (1, "custom desc"), (2, "customorg")],
        },
        id="account-description-and-organization",
    ),
    pytest.param(
        {
            "full": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"Account - '{test_account2}':Organization='CustomOrg':Fairshare=30\n",
            "partial": f"Parent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"Account - '{test_account2}':Fairshare=40\n",
            "list_args": f"account {test_account2}",
            "fmt": "Account,Organization",
            "fold_case": True,
            "before": [(1, "customorg")],
            "after": [(0, test_account2), (1, "customorg")],
        },
        id="child-account-organization",
    ),
    pytest.param(
        {
            "full": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=50\n",
            "partial": f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=50\n",
            "cluster_spec": f":Classification={CLASS_CAPACITY}:Fairshare=100",
            "partial_cluster_spec": ":Fairshare=100",
            "list_args": f"cluster {test_cluster}",
            "fmt": "Cluster,Classification",
            "fold_case": False,
            "before": [(1, CLASS_CAPACITY)],
            "after": [(0, test_cluster), (1, CLASS_CAPACITY)],
        },
        id="cluster-classification",
    ),
]


@pytest.mark.parametrize("case", DECL_METADATA_ADDITIVE_CASES)
def test_declarative_leaves_entity_metadata_additive(case):
    """Descriptive metadata is not something declarative resets.

    Declarative is about the association and QOS limits. The record fields
    beside them, an account's Description and Organization and a cluster's
    Classification, keep normal additive behavior: leaving them out of the
    file changes nothing. Each case sets one additively, confirms it landed,
    then reloads declaratively without it.
    """
    cluster_spec = case.get("cluster_spec", "")
    partial_cluster_spec = case.get("partial_cluster_spec", "")
    cfg_full = "decl_meta_full.cfg"
    cfg_partial = "decl_meta_partial.cfg"
    write_load_file(
        cfg_full, f"Cluster - '{test_cluster}'{cluster_spec}\n{case['full']}"
    )
    write_load_file(
        cfg_partial,
        f"Cluster - '{test_cluster}'{partial_cluster_spec}\n{case['partial']}",
    )

    _load(cfg_full)
    _expect_fields(
        _entity_row(case["list_args"], case["fmt"], case["fold_case"]),
        case["before"],
        "before the declarative load",
    )

    _declarative_load(cfg_partial)
    _expect_fields(
        _entity_row(case["list_args"], case["fmt"], case["fold_case"]),
        case["after"],
        "after the declarative load",
    )


DECL_TRES_REPLACE_CASES = [
    pytest.param("assoc", "GrpTRES", id="assoc-grptres"),
    pytest.param("assoc", "MaxTRES", id="assoc-maxtres"),
    pytest.param("assoc", "MaxTRESPerNode", id="assoc-maxtres-per-node"),
    pytest.param("qos", "GrpTRES", id="qos-grptres"),
    pytest.param("qos", "MaxTRESPerJob", id="qos-maxtres-per-job"),
]


@pytest.mark.parametrize("kind, field", DECL_TRES_REPLACE_CASES)
def test_declarative_tres_replaces_not_merges(kind, field):
    """Declarative TRES limit replaces the stored value; does not merge extras.

    Additive load sets the field to cpu=1,mem=1GB; declarative load with only
    cpu=2 must drop mem, not retain it from the prior database row.
    """
    full, partial = _tres_replace_files(kind, field)
    cfg_full = f"decl_tres_{kind}_{field}_full.cfg"
    cfg_partial = f"decl_tres_{kind}_{field}_partial.cfg"
    write_load_file(cfg_full, full)
    _load(cfg_full)
    before = _read_tres(kind, field)
    assert before.get("cpu") == "1", f"Initial {field} should include cpu=1"
    assert "mem" in before, f"Initial {field} should include mem"

    write_load_file(cfg_partial, partial)
    result = _load(cfg_partial, declarative=True)
    after = _read_tres(kind, field)
    assert after.get("cpu") == "2", f"Declarative {field} should be cpu=2"
    assert "mem" not in after, f"Declarative {field} must replace, not merge mem"

    # Dropping mem is reported by naming it with a count of INFINITE64, which
    # is what makes the modify remove it. That is a wire value; printed as it
    # stands it reaches the reader as 18446744073709551615.
    out = result["stdout"]
    assert (
        "18446744073709551615" not in out
    ), f"The clear sentinel must not be printed as a count, got: {out!r}"
    assert "=-1" in out, f"A cleared {field} should read as -1, got: {out!r}"


def test_declarative_assoc_qos_replaces_not_merges():
    """Declarative account QOS replaces the list; does not merge with prior QOS."""
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_assoc_qos_full.cfg"
    cfg_partial = "decl_assoc_qos_partial.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='{test_qos1},{test_qos2}'
"""
    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='{test_qos2}'
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_partial, config_partial)
    atf.run_command(
        f"sacctmgr -i load {cfg_full}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    qos_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert (
        test_qos1 in qos_out and test_qos2 in qos_out
    ), "both QOS present after additive load"
    _declarative_load(cfg_partial)
    qos_out2 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert test_qos2 in qos_out2, "Declarative QOS line should keep qos2"
    assert (
        test_qos1 not in qos_out2
    ), "Declarative QOS must replace, not merge with qos1 from prior load"


def test_declarative_child_clears_max_jobs_to_inherit_parent():
    """Declarative child omit MaxJobs clears the child override.

    Parent MaxJobs remains. After clear, list shows the inherited parent
    MaxJobs for the child (not the prior child-only value).
    """
    cfg_full = "decl_parent_maxjobs_full.cfg"
    cfg_partial = "decl_parent_maxjobs_partial.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:MaxJobs=5
Parent - '{test_account1}'
Account - '{test_account2}':Fairshare=25:MaxJobs=20
"""
    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:MaxJobs=5
Parent - '{test_account1}'
Account - '{test_account2}':Fairshare=40
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_partial, config_partial)
    _load(cfg_full)
    _declarative_load(cfg_partial)
    parent = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=MaxJobs",
        fatal=True,
    )
    child = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} user='' "
        f"cluster={test_cluster} format=Fairshare,MaxJobs",
        fatal=True,
    )
    assert parent.strip() == "5", "Parent MaxJobs unchanged"
    parts = child.strip().split("|")
    assert parts[0] == "40", "Child Fairshare from declarative file"
    assert parts[1] == "5", "Cleared child MaxJobs should inherit parent limit"


DECL_UNLISTED_CASES = [
    pytest.param(
        {
            "full": f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Account - '{test_account2}':Fairshare=30\n",
            "partial": f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=100\n",
            "checks": [
                (
                    "assoc",
                    f"account={test_account1} user=''",
                    "Fairshare",
                    [(0, "100")],
                ),
                ("assoc", f"account={test_account2} user=''", "Fairshare", [(0, "30")]),
            ],
        },
        id="accounts",
    ),
    pytest.param(
        {
            "full": f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"User - '{test_user1}':DefaultAccount='{test_account1}':Fairshare=25\n"
            f"User - '{test_user2}':DefaultAccount='{test_account1}':Fairshare=15\n",
            "partial": f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n"
            f"Parent - '{test_account1}'\n"
            f"User - '{test_user1}':Fairshare=100\n",
            "checks": [
                (
                    "assoc",
                    f"user={test_user1} account={test_account1}",
                    "Fairshare",
                    [(0, "100")],
                ),
                (
                    "assoc",
                    f"user={test_user2} account={test_account1}",
                    "Fairshare",
                    [(0, "15")],
                ),
            ],
        },
        id="users",
    ),
    pytest.param(
        {
            "full": f"QOS - '{test_qos1}':Priority=10:GrpJobs=5\n"
            f"QOS - '{test_qos2}':Priority=20:GrpJobs=8\n"
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n",
            "partial": f"QOS - '{test_qos1}':Priority=99\n"
            f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare=50\n",
            "checks": [
                ("qos", test_qos1, "Name,Priority,GrpJobs", [(1, "99")]),
                ("qos", test_qos2, "Name,Priority,GrpJobs", [(1, "20"), (2, "8")]),
            ],
        },
        id="qos",
    ),
]


@pytest.mark.parametrize("case", DECL_UNLISTED_CASES)
def test_declarative_leaves_unlisted_entities_alone(case):
    """Declarative resets what the file lists and nothing else.

    Two entities are created additively and only one is named in the
    declarative file. The listed one takes its new value; the unlisted one
    keeps everything it had and is not removed, since declarative never
    deletes. The first check in each case is the listed entity, so a load
    that quietly did nothing at all cannot pass.
    """
    cfg_full = "decl_unlisted_full.cfg"
    cfg_partial = "decl_unlisted_partial.cfg"
    write_load_file(cfg_full, case["full"])
    write_load_file(cfg_partial, case["partial"])

    _load(cfg_full)
    _declarative_load(cfg_partial)

    for kind, target, fmt, checks in case["checks"]:
        _expect_fields(
            _read_row(kind, target, fmt, "after the declarative load"),
            checks,
            f"{kind} {target} after the declarative load",
        )


def test_declarative_load_unscoped_leaves_other_cluster_alone():
    """A declarative load with no cluster= override only resets the cluster
    named in the file, not an identically-named account/user on another one.

    The man page promises that anything not listed in the file, including
    whole clusters and associations, is left alone. An account/user pair is
    created on both test_cluster and test_cluster_second with distinct
    Fairshare/GrpJobs, then a declarative file whose Cluster line names only
    test_cluster is loaded with no cluster= override. The second cluster's
    row must be untouched, not reset.
    """
    cfg_full = "decl_two_cluster_full.cfg"
    cfg_c2 = "decl_two_cluster_c2.cfg"
    cfg_partial = "decl_two_cluster_partial.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=25
"""
    config_c2 = f"""Cluster - '{test_cluster_second}'
Parent - 'root'
Account - '{test_account1}':Fairshare=70:GrpJobs=20
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=45
"""
    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=100
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=100
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_c2, config_c2)
    write_load_file(cfg_partial, config_partial)
    _load(cfg_full)
    _load(cfg_c2)

    _declarative_load(cfg_partial)

    row1 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs",
        fatal=True,
    )
    parts1 = [p.strip() for p in row1.strip().split("|")]
    assert parts1[0] == "100", "Declarative line for its own cluster must apply"
    assert not parts1[1], "Omitted GrpJobs on the named cluster must reset to default"

    row2 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster_second} format=Fairshare,GrpJobs",
        fatal=True,
    )
    parts2 = [p.strip() for p in row2.strip().split("|")]
    assert parts2[0] == "70", "Other cluster's Fairshare must be untouched"
    assert parts2[1] == "20", "Other cluster's GrpJobs must be untouched, not reset"

    user2 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user={test_user1} "
        f"cluster={test_cluster_second} format=Fairshare",
        fatal=True,
    )
    assert user2.strip() == "45", "Other cluster's user Fairshare must be untouched"


def test_declarative_load_cluster_override_scopes_reset_to_named_cluster():
    """A declarative load's cluster= override scopes the reset to that
    cluster, replacing whatever the file's own Cluster line names.

    Load the same file body twice, additively, once per cluster. Then
    declaratively load a file whose Cluster line names test_cluster, but
    pass cluster={test_cluster_second} on the command line: the reset must
    land on test_cluster_second's row, and test_cluster's row -- despite
    being the name the file itself carries -- must be left alone.
    """
    cfg_full = "decl_cluster_override_full.cfg"
    cfg_c2 = "decl_cluster_override_c2.cfg"
    cfg_partial = "decl_cluster_override_partial.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10
"""
    config_c2 = f"""Cluster - '{test_cluster_second}'
Parent - 'root'
Account - '{test_account1}':Fairshare=70:GrpJobs=20
"""
    # The Cluster line names test_cluster; the command-line cluster= below
    # is what actually decides which cluster gets reset.
    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=100
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_c2, config_c2)
    write_load_file(cfg_partial, config_partial)
    _load(cfg_full)
    _load(cfg_c2)

    _declarative_load(cfg_partial, cluster=test_cluster_second)

    row1 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs",
        fatal=True,
    )
    parts1 = [p.strip() for p in row1.strip().split("|")]
    assert (
        parts1[0] == "50"
    ), "The cluster named in the file must be untouched when cluster= overrides it"
    assert parts1[1] == "10", "That cluster's GrpJobs must not be reset"

    row2 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster_second} format=Fairshare,GrpJobs",
        fatal=True,
    )
    parts2 = [p.strip() for p in row2.strip().split("|")]
    assert (
        parts2[0] == "100"
    ), "cluster= override should land the declarative Fairshare on that cluster"
    assert not parts2[
        1
    ], "Omitted GrpJobs on the overridden cluster must reset to default"


def test_declarative_create_path_adds_account_with_defaults():
    """Declarative load still adds a missing account, with omitted limits
    at their defaults.

    A new entity gets defaults under an additive load too, so this guards
    the create path against the declarative changes rather than proving
    declarative reset semantics.
    """
    cfg1 = "decl_new_acct1.cfg"
    cfg2 = "decl_new_acct2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Account - '{test_account2}':Fairshare=40
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _declarative_load(cfg2)
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} user='' "
        f"cluster={test_cluster} format=Account,Fairshare,GrpJobs,MaxJobs",
        fatal=True,
    )
    parts = _assoc_list_account_row_parts(out, 4)
    assert parts[0] == test_account2, "row should be for the new account"
    assert parts[1] == "40", "Fairshare from declarative create"
    assert not parts[2], "GrpJobs omitted => default on new account"
    assert not parts[3], "MaxJobs omitted => default on new account"


def test_declarative_create_path_adds_user_with_defaults():
    """Declarative load still adds a missing user, with omitted limits at
    their defaults.

    A new entity gets defaults under an additive load too, so this guards
    the create path against the declarative changes rather than proving
    declarative reset semantics.
    """
    cfg1 = "decl_new_user1.cfg"
    cfg2 = "decl_new_user2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Parent - '{test_account1}'
User - '{test_user2}':Fairshare=40
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _declarative_load(cfg2)
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user2} account={test_account1} "
        f"cluster={test_cluster} format=User,Fairshare,MaxJobs,QOS",
        fatal=True,
    )
    parts = [p.strip() for p in out.strip().split("|")]
    assert len(parts) == 4, "Expect User|Fairshare|MaxJobs|QOS"
    assert parts[0] == test_user2, "row should be for the new user"
    assert parts[1] == "40", "Fairshare from declarative create"
    assert not parts[2], "MaxJobs omitted => default on new user"
    parent_qos = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    ).strip()
    assert (
        parts[3] == parent_qos
    ), "Omitted QOS => whatever the parent account grants, nothing of its own"


def test_declarative_dump_reload_preserves_listed_qos_fields():
    """Dump, then declarative-reload of that dump, is a no-op: dump again and
    the two files match exactly.

    This is the feature's central safety property, so the state has to be
    rich enough that a vacuous field (one dump never writes and declarative
    never resets) cannot hide behind it: a full QOS option set plus an
    association that sets DefaultQOS, Priority and a TRES-run-mins limit on
    top of the plain limits, none of which the earlier tests in this module
    cover together.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_dump_src.cfg"
    dump_cfg_a = "decl_dump_a.cfg"
    dump_cfg_b = "decl_dump_b.cfg"
    qos_opts = {
        "Description": "decl_dump_qos",
        "Flags": "DenyOnLimit,NoDecay",
        "GraceTime": "70",
        "GrpJobs": "120",
        "GrpJobsAccrue": "2",
        "GrpSubmitJobs": "130",
        "GrpTres": "cpu=1",
        "GrpTresMins": "cpu=2",
        "GrpTresRunMins": "cpu=3",
        "GrpWall": "60",
        "MaxJobsPerAccount": "160",
        "MaxJobsPerUser": "250",
        "MaxSubmitJobsPerUser": "4",
        "MaxTresPerJob": "cpu=3",
        "MaxWallDurationPerJob": "30",
        "MinPrioThresh": "30",
        "Preempt": f"'{test_qos2}'",
        "PreemptMode": "cancel",
        "Priority": "10010",
        "UsageFactor": "0.500000",
    }
    qos_line_opts = ":".join(f"{key}={value}" for key, value in qos_opts.items())
    config = f"""QOS - '{test_qos1}':{qos_line_opts}
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=6:MaxJobs=7:Priority=15\
:DefaultQOS='{test_qos1}':GrpTRESRunMins=cpu=4:QOS='{test_qos1}'
"""
    write_load_file(cfg, config)
    atf.run_command(
        f"sacctmgr -i load {cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr dump {test_cluster} file={dump_cfg_a}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.assert_file_contents(
        dump_cfg_a,
        f"QOS - '{test_qos1}'",
        contains=True,
        message="Dump should contain the QOS being reloaded",
    )
    atf.assert_file_contents(
        dump_cfg_a,
        f"Account - '{test_account1}'",
        contains=True,
        message="Dump should contain the association being reloaded",
    )
    _declarative_load(dump_cfg_a)
    atf.run_command(
        f"sacctmgr dump {test_cluster} file={dump_cfg_b}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    dump_a = atf.run_command_output(f"cat {dump_cfg_a}", fatal=True)
    dump_b = atf.run_command_output(f"cat {dump_cfg_b}", fatal=True)
    assert dump_a == dump_b, (
        "Declarative reload of a dump should be a no-op: re-dumping should "
        "produce an identical file"
    )


@pytest.mark.parametrize(
    "flags",
    [
        "clean declarative",
        "declarative clean",
        "clean=account declarative",
    ],
)
def test_declarative_clean_and_declarative_mutually_exclusive(flags):
    """clean and declarative cannot be combined on one load command."""
    config_file = "clean_decl_error.cfg"
    config_text = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(config_file, config_text)

    result = atf.run_command(
        f"sacctmgr -i load file={config_file} {flags}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    stderr = result["stderr"].lower()
    assert (
        "clean" in stderr and "declarative" in stderr
    ), f"Should reject clean+declarative for {flags}"


def test_modify_qos_flags_clear_all_does_not_reset_limits():
    """sacctmgr Flags=-1 clears flag bits only; it must not wipe QOS limits.

    Regression: declarative used an ephemeral bit in qos->flags that Flags=-1
    (INFINITE) also set, so modify qos set Flags=-1 reset every limit.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1} set Priority=99 GrpJobs=5 "
        f"Description='keepme' Flags=DenyOnLimit",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    fmt = "Name,Priority,GrpJobs,Description,Flags"
    row = _qos_list_row(test_qos1, fmt)
    assert row is not None, f"QOS {test_qos1} should exist after the modify"
    assert row[1] == "99", "Priority should be set by the modify"
    assert row[2] == "5", "GrpJobs should be set by the modify"

    atf.run_command(
        f"sacctmgr -i modify qos {test_qos1} set Flags=-1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    row2 = _qos_list_row(test_qos1, fmt)
    assert row2 is not None, "QOS should still exist after Flags=-1"
    assert row2[1] == "99", "Flags=-1 must not reset Priority"
    assert row2[2] == "5", "Flags=-1 must not reset GrpJobs"
    assert row2[3] == "keepme", "Flags=-1 must not clear Description"
    assert not row2[4].strip(), "Flags=-1 should clear named QOS flags"


# The two spellings of a relative association QOS the flat file accepts: a
# sign on the value, and the QOS(=,+=,-=) operator the SPECIFICATIONS FOR
# FLAT FILE section documents. They are tokenized differently, so covering
# one does not cover the other.
#
# (load option, value, whether the named QOS ends up added or removed)
RELATIVE_QOS_SPELLINGS = [
    pytest.param(f"QOS='-{test_qos1}'", "remove", id="signed-value-remove"),
    pytest.param(f"QOS='+{test_qos2}'", "add", id="signed-value-add"),
    pytest.param(f"QOS-={test_qos1}", "remove", id="operator-remove"),
    pytest.param(f"QOS+={test_qos2}", "add", id="operator-add"),
]


@pytest.mark.parametrize(
    "entity",
    ("account", "user"),
)
@pytest.mark.parametrize("qos_spec,effect", RELATIVE_QOS_SPELLINGS)
def test_declarative_relative_assoc_qos_applies(entity, qos_spec, effect):
    """Declarative load applies relative association QOS tokens.

    Relative tokens keep their usual meaning under declarative: they are a
    delta against what is stored rather than a replacement. Pre-state is
    Fairshare=50 with QOS exactly {test_qos1}; the file carries a
    distinguishing Fairshare (77) so we can also tell the rest of the line
    still took effect.

    Run for both spellings the man page documents, and for both an Account
    line and a User line, since they reach _mod_assoc() from different points
    in the load loop.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_rel_qos_full.cfg"
    cfg_rel = "decl_rel_qos.cfg"
    header = f"Cluster - '{test_cluster}'\nParent - 'root'\n"
    if entity == "account":
        config_full = (
            f"{header}Account - '{test_account1}':Fairshare=50:QOS='{test_qos1}'\n"
        )
        config_rel = f"{header}Account - '{test_account1}':Fairshare=77:{qos_spec}\n"
        who = "user=''"
    else:
        child = (
            f"Account - '{test_account1}':Fairshare=50\nParent - '{test_account1}'\n"
        )
        config_full = (
            f"{header}{child}" f"User - '{test_user1}':Fairshare=50:QOS='{test_qos1}'\n"
        )
        config_rel = (
            f"{header}{child}" f"User - '{test_user1}':Fairshare=77:{qos_spec}\n"
        )
        who = f"user={test_user1}"
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_rel, config_rel)
    _load(cfg_full)
    _declarative_load(cfg_rel)

    row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} {who} "
        f"cluster={test_cluster} format=Fairshare,QOS",
        fatal=True,
    )
    parts = [p.strip() for p in row.strip().split("|")]
    assert parts[0] == "77", "Rest of the line must still apply"
    if effect == "remove":
        assert (
            test_qos1 not in parts[1]
        ), f"{qos_spec} must drop that QOS from the stored list"
    else:
        assert test_qos1 in parts[1], f"{qos_spec} must leave the stored QOS in place"
        assert test_qos2 in parts[1], f"{qos_spec} must add the named QOS"


@pytest.mark.parametrize(
    "preempt_expr",
    (
        f"-{test_qos2}",
        f"+{test_qos3}",
    ),
)
def test_declarative_relative_qos_preempt_applies(preempt_expr):
    """Declarative load applies relative standalone QOS Preempt tokens.

    Pre-state has Priority=10 and Preempt exactly {test_qos2}. The file uses
    Priority=88 so we can tell the rest of the line applied too. test_qos3 is
    the add target rather than test_qos1, since a QOS preempting itself is a
    preemption loop and is rejected for reasons unrelated to this check.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2},{test_qos3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_qos_preempt_full.cfg"
    cfg_rel = "decl_qos_preempt_rel.cfg"
    config_full = f"""QOS - '{test_qos2}':Priority=5
QOS - '{test_qos1}':Priority=10:Preempt='{test_qos2}'
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    config_rel = f"""QOS - '{test_qos2}':Priority=5
QOS - '{test_qos1}':Priority=88:Preempt='{preempt_expr}'
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_rel, config_rel)
    _load(cfg_full)
    _declarative_load(cfg_rel)

    row = _qos_list_row(test_qos1, "Name,Priority,Preempt")
    assert row is not None, "QOS should still exist"
    assert row[1] == "88", "Rest of the QOS line must still apply"
    if preempt_expr.startswith("-"):
        assert test_qos2 not in row[2], "-Preempt must drop that target"
    else:
        assert test_qos2 in row[2], "+Preempt must leave the stored target"
        assert test_qos3 in row[2], "+Preempt must add the named target"


def test_declarative_create_path_relative_assoc_qos_applies():
    """Create-path relative Account QOS is applied; later lines still apply.

    The Account does not exist yet, so this goes through the add path rather
    than _mod_assoc(). A relative token is allowed there too, and the rest of
    the file is still processed.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_create_rel_qos.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=77:QOS='+{test_qos1}'
Account - '{test_account2}':Fairshare=60
"""
    write_load_file(cfg, config)
    _declarative_load(cfg)

    row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Account,Fairshare,QOS",
        fatal=True,
    )
    parts = [p.strip() for p in row.strip().split("|")]
    assert parts[0] == test_account1, "Account with a relative QOS must be added"
    assert parts[1] == "77", "Rest of the line must still apply"
    assert test_qos1 in parts[2], "Relative QOS must resolve to that QOS"

    row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} user='' "
        f"cluster={test_cluster} format=Account,Fairshare",
        fatal=True,
    )
    parts = [p.strip() for p in row.strip().split("|")]
    assert parts[0] == test_account2, "Later Account line must still be created"
    assert parts[1] == "60", "Later Account Fairshare must apply"


def test_declarative_create_path_relative_qos_preempt_applies():
    """Create-path relative Preempt is applied; later lines still apply.

    Pre-create only the Preempt target; the subject QOS is new, so this goes
    through the QOS add path rather than the modify path.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_create_rel_preempt.cfg"
    config = f"""QOS - '{test_qos1}':Priority=88:Preempt='+{test_qos2}'
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=55
"""
    write_load_file(cfg, config)
    _declarative_load(cfg)

    row = _qos_list_row(test_qos1, "Name,Priority,Preempt")
    assert row is not None, "QOS with a relative Preempt must be added"
    assert row[1] == "88", "Rest of the QOS line must still apply"
    assert test_qos2 in row[2], "Relative Preempt must resolve to that QOS"

    row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Account,Fairshare",
        fatal=True,
    )
    parts = [p.strip() for p in row.strip().split("|")]
    assert parts[0] == test_account1, "Later Account line must still be created"
    assert parts[1] == "55", "Later Account Fairshare must apply"


def test_declarative_partition_user_line_scopes_to_id():
    """Partition-less declarative User line must not reset partition assocs.

    Creates a default (no Partition) user assoc and a partition-specific one.
    Declarative load of Partition-less User=Fairshare must update only the
    default row; the partition row keeps its MaxJobs.
    """
    # test_cluster is not the live cluster, so sacctmgr never resolves this
    # name against a real partition. A literal keeps the test independent of
    # whether the host config happens to define a default partition.
    part = "decl_part_23787"
    cfg_full = "decl_part_full.cfg"
    cfg_partial = "decl_part_partial.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=50
User - '{test_user1}':Fairshare=50:Partition='{part}':MaxJobs=3
"""
    config_partial = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=100
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_partial, config_partial)
    _load(cfg_full)
    _declarative_load(cfg_partial)

    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user={test_user1} "
        f"cluster={test_cluster} format=Fairshare,MaxJobs,Partition",
        fatal=True,
    )
    by_part = {}
    for ln in out.strip().split("\n"):
        if not ln.strip():
            continue
        parts = [p.strip() for p in ln.split("|")]
        assert len(parts) == 3, f"Unexpected assoc row: {ln!r}"
        by_part[parts[2]] = parts

    assert "" in by_part, "Default (no Partition) user assoc must exist"
    assert by_part[""][0] == "100", "Default (no Partition) Fairshare should update"

    assert part in by_part, "Partition-specific user assoc must still exist"
    assert by_part[part][0] == "50", "Partition assoc Fairshare must stay untouched"
    assert by_part[part][1] == "3", "Partition assoc MaxJobs must stay untouched"


def test_declarative_parent_max_tres_fill_in_controller(live_cluster):
    """Controller MaxTRESPJ keeps parent TRES ids after declarative replace.

    Uses the live ClusterName so scontrol show assoc_mgr can observe the
    packed max_tres_pj. Declarative MaxTRES=cpu=2 replaces the child's prior
    mem=512; list/assoc_mgr may still show parent mem (per-id fill from
    parent MaxTRES=mem=2048), not the old child mem value.
    """
    cluster = live_cluster
    cfg_full = "decl_ctld_tres_full.cfg"
    cfg_partial = "decl_ctld_tres_partial.cfg"
    config_full = f"""Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=1:MaxTRES=cpu=10,mem=2048
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=1:MaxTRES=cpu=5,mem=512
"""
    config_partial = f"""Cluster - '{cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=1:MaxTRES=cpu=10,mem=2048
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=1:MaxTRES=cpu=2
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_partial, config_partial)
    _load(cfg_full)
    _declarative_load(cfg_partial)

    db_row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user={test_user1} "
        f"cluster={cluster} format=MaxTRES",
        fatal=True,
    )
    db_tres = parse_tres(db_row)
    assert db_tres.get("cpu") == "2", "MaxTRES should be declarative cpu=2"
    # Parent fill may show mem=2G; must not keep the prior child mem=512.
    assert (
        db_tres.get("mem") != "512"
    ), "MaxTRES must not retain child mem=512 after declarative replace"
    if "mem" in db_tres:
        assert db_tres["mem"] in (
            "2G",
            "2048",
        ), "Listed mem should be parent fill, not an unexpected value"

    def _has_parent_fill(out):
        # scontrol prints the account assoc first (MaxTRESPJ=cpu=10,...), then
        # the user assoc (MaxTRESPJ=cpu=2,mem=...). Scan all MaxTRESPJ lines.
        if test_user1 not in out:
            return False
        for m in re.finditer(r"MaxTRESPJ=([^\n]+)", out):
            tres = parse_tres(m.group(1))
            if tres.get("cpu") == "2" and tres.get("mem") in ("2048", "2G"):
                return True
        return False

    for _ in atf.timer():
        if _has_parent_fill(
            atf.run_command_output(
                f"scontrol show assoc_mgr accounts={test_account1} "
                f"users={test_user1} flags=assoc",
                user=atf.properties["slurm-user"],
                quiet=True,
            )
        ):
            break
    else:
        pytest.fail("Ticket 23787: controller never showed the parent-filled MaxTRESPJ")


@pytest.mark.parametrize(
    "bad_spec,expected_error",
    [
        ("QOS='no_such_qos_23787'", "bad qos"),
        ("DefaultQOS='no_such_qos_23787'", "bad default qos"),
        ("Fairshare+=10", "invalid operator"),
    ],
    ids=["qos", "default_qos", "relative_fairshare"],
)
def test_declarative_rejects_invalid_field(bad_spec, expected_error):
    """An invalid field must fail the line; the association stays untouched.

    These callees set exit_code and still report success (leaving an empty
    qos_list for a bad QOS name), so the parser has to treat a changed
    exit_code as a parse failure. Otherwise declarative load applies the
    record and resets the omitted fields.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_bad_field_full.cfg"
    cfg_bad = "decl_bad_field_bad.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='{test_qos1}'
"""
    config_bad = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':{bad_spec}
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_bad, config_bad)
    _load(cfg_full)

    result = atf.run_command(
        f"sacctmgr -i load {cfg_bad} {DECLARATIVE_LOAD_FLAG}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert (
        expected_error in result["stderr"].lower()
    ), f"stderr should report {expected_error!r} for {bad_spec}"

    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,QOS",
        fatal=True,
    )
    parts = [p.strip() for p in out.strip().split("|")]
    assert len(parts) == 2, f"Expect Fairshare|QOS, got {out!r}"
    assert parts[0] == "50", "Rejected line must not change Fairshare"
    assert test_qos1 in parts[1], "Rejected line must not clear association QOS"


def test_declarative_rejects_invalid_field_on_a_later_line():
    """A bad QOS on a later line rejects that record, not an earlier one.

    The first account line carries a relative QOS that applies cleanly, so
    the diagnostic naming the second line's QOS is what proves the parse
    reached it. That record must keep the QOS and Fairshare it already had
    rather than having its list wiped.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_two_error_full.cfg"
    cfg_bad = "decl_two_error_bad.cfg"
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='{test_qos1}'
Account - '{test_account2}':Fairshare=50:QOS='{test_qos1}'
"""
    config_bad = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='+{test_qos1}'
Account - '{test_account2}':QOS='no_such_qos_23787'
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_bad, config_bad)
    _load(cfg_full)

    result = atf.run_command(
        f"sacctmgr -i load {cfg_bad} {DECLARATIVE_LOAD_FLAG}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert (
        "bad qos" in result["stderr"].lower()
    ), "stderr should report the bad qos on the second line"
    assert "no_such_qos_23787" in result["stderr"], (
        "the diagnostic must name the QOS from the second account line, "
        f"which is what proves the parse reached it: {result['stderr']!r}"
    )

    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} user='' "
        f"cluster={test_cluster} format=Fairshare,QOS",
        fatal=True,
    )
    parts = [p.strip() for p in out.strip().split("|")]
    assert len(parts) == 2, f"Expect Fairshare|QOS, got {out!r}"
    assert parts[0] == "50", "Second line must not change Fairshare"
    assert test_qos1 in parts[1], "Second line must keep its QOS after the earlier skip"


def test_declarative_cluster_relative_qos_applies():
    """Relative QOS on a Cluster line is applied; later lines still apply.

    Cluster lines keep additive behavior under declarative, so a relative
    token there is simply merged into the root association.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_cluster_rel_qos.cfg"
    config = f"""Cluster - '{test_cluster}':QOS='+{test_qos1}'
Parent - 'root'
Account - '{test_account1}':Fairshare=60
"""
    write_load_file(cfg, config)
    _declarative_load(cfg)

    root_qos = atf.run_command_output(
        f"sacctmgr -n -P list assoc account=root user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert test_qos1 in root_qos, "Relative QOS on Cluster - must reach the root assoc"

    row = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Account,Fairshare",
        fatal=True,
    )
    parts = [p.strip() for p in row.strip().split("|")]
    assert parts[0] == test_account1, "Account after the Cluster line must be added"
    assert parts[1] == "60", "Account Fairshare must apply"


def test_declarative_removing_qos_a_child_defaults_to_stops_the_load():
    """A rejected declarative modify must stop the load, not report success.

    Dropping a QOS from an account while a user under it still has that
    QOS as their DefaultQOS makes slurmdbd refuse the modify and discard
    the whole open transaction. It signals that by returning a one-entry
    list holding the offending associations with an errno set, not by
    returning an empty list, so a caller that only checks the list for
    emptiness reads it as success. Carrying on there would commit every
    later line on top of a transaction the server already threw away.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "decl_defqos_full.cfg"
    cfg_drop = "decl_defqos_drop.cfg"
    # test_user1 gets no QOS of its own, so its DefaultQOS is only reachable
    # through what test_account1 grants.
    config_full = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account2}':Fairshare=30
Account - '{test_account1}':Fairshare=50:QOS='{test_qos1},{test_qos2}'
Parent - '{test_account1}'
User - '{test_user1}':Fairshare=25:DefaultQOS='{test_qos1}'
"""
    # test_account2 is listed first, so its modify is staged before the one
    # that gets rejected. It is the witness for the rollback. test_user1 is
    # left out, so the load does not change its DefaultQOS on the way past.
    config_drop = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account2}':Fairshare=77
Account - '{test_account1}':Fairshare=50:QOS='{test_qos2}'
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_drop, config_drop)
    _load(cfg_full)

    result = atf.run_command(
        f"sacctmgr -i load {cfg_drop} {DECLARATIVE_LOAD_FLAG}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    stderr = result["stderr"].lower()
    assert (
        "default qos" in stderr
    ), f"stderr should name the default-QOS problem, got: {result['stderr']}"

    fs2 = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} user='' "
        f"cluster={test_cluster} format=Fairshare",
        fatal=True,
    )
    assert (
        fs2.strip() == "30"
    ), "Change staged before the rejected line must be rolled back, not kept"

    qos_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert test_qos1 in qos_out, "Rejected modify must leave the QOS list alone"


def test_declarative_idempotent_reload():
    """Reloading the same file leaves the stored hierarchy unchanged.

    Covers the advertised dump/edit/reload workflow, including a child that
    inherits MaxJobs from its parent. The controller-side suppression of
    redundant descendant updates is not observable here: load prints one
    summary line per listed entity and never names the descendants it
    refreshed, so this asserts stored state only.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_idempotent.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:MaxJobs=7:QOS='{test_qos1},{test_qos2}'
Parent - '{test_account1}'
Account - '{test_account2}':Fairshare=30
"""
    write_load_file(cfg, config)
    _declarative_load(cfg)

    def _snapshot():
        return atf.run_command_output(
            f"sacctmgr -n -P list assoc user='' cluster={test_cluster} "
            f"format=Account,Fairshare,MaxJobs,QOS",
            fatal=True,
        )

    before = _snapshot()
    assert test_account1 in before, "Setup: first load must apply the hierarchy"
    assert test_account2 in before, "Setup: child account must be created"

    _declarative_load(cfg)
    after = _snapshot()
    assert after == before, "Idempotent declarative reload must not change assocs"


@pytest.mark.parametrize(
    "bad_arg",
    ("no_such_option_23787", "no_such_option_23787=5"),
    ids=["bare_word", "key_value"],
)
def test_declarative_bad_load_option_applies_nothing(bad_arg):
    """An argument the load cannot use must stop it before the file is read.

    The options say what the load is meant to do, so one that cannot be used
    has to stop it. Both forms are covered: a bare word is taken as a second
    file name, while a key=value that matches nothing is an unknown option.

    The tell that the file was never read is the absence of a line-level
    complaint. exit_code is global and sticky, so without a check on the
    arguments the load goes on to open the file and rejects its first line,
    blaming "Problem with line(1)" for a line that is perfectly valid.
    """
    cfg = "decl_bad_option.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=42
"""
    write_load_file(cfg, config)

    result = atf.run_command(
        f"sacctmgr -i load {cfg} {DECLARATIVE_LOAD_FLAG} {bad_arg}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert result["stderr"].strip(), "A rejected argument should be reported"
    assert (
        "problem with line" not in result["stderr"].lower()
    ), "The load must stop on its arguments, before any line is parsed"

    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Account",
        fatal=True,
    )
    assert not out.strip(), "A load with a bad argument must not apply the file"


def test_declarative_qos_line_bad_option_is_diagnosed_on_its_own_line():
    """A malformed QOS line is diagnosed on its own line, not an earlier one.

    The first QOS line is well formed, so the diagnostic naming the second
    line's option is what proves the parse reached it rather than stopping
    earlier. The abort then has to discard the first line's QOS along with
    the rest of the file.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1} set Priority=3",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Preempt target, so the relative token below resolves by name.
    atf.run_command(
        f"sacctmgr -i add qos {test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg = "decl_qos_parse_bad.cfg"
    config = f"""QOS - '{test_qos3}':Priority=88:Preempt='+{test_qos2}'
QOS - '{test_qos1}':Priority=7:NoSuchQosOption23787=1
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(cfg, config)

    result = atf.run_command(
        f"sacctmgr -i load {cfg} {DECLARATIVE_LOAD_FLAG}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert (
        "unknown option" in result["stderr"].lower()
    ), "stderr should name the unknown QOS option"
    assert "NoSuchQosOption23787" in result["stderr"], (
        "the diagnostic must name the option from the second QOS line, which "
        f"is what proves the parse reached it: {result['stderr']!r}"
    )

    assert (
        _qos_list_row(test_qos3, "Name") is None
    ), "the aborted load must not leave the first line's QOS behind"

    row = _qos_list_row(test_qos1, "Name,Priority")
    assert row is not None, "QOS should still exist after a rejected load"
    assert row[1] == "3", "Rejected QOS line must not change Priority"
