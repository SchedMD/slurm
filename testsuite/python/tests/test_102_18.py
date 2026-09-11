############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_102_18.py - sacctmgr load/dump.

Tests for sacctmgr load and dump: file format semantics, additive vs clean
behavior, and error handling. Covers basic load/dump, additive load, load
with clean, comprehensive modify, and error/edge cases (no cluster, QOS
after cluster, missing parent, no parent warning, multiple clusters,
duplicate QOS, bad option format, no name, unknown option, no filename,
unreadable file, nothing after object, readonly mode, clean with multiple
clusters, duplicate file=, empty file, unbalanced quotes, nonexistent QOS).
Also covers a three-phase sacctmgr load workflow, additive preserve-omitted,
parent-unchanged modify, DefaultAccount on a separate Parent line,
DefaultWCKey when the WCKey row already exists, user-record modify scoped to
the named cluster, dump round-trip, and clean clearing association QOS.
Destructive clean=account/qos tests live in test_102_20.py.

Everything here is additive-load behavior. The tests gated on 26.11+ cover
sacctmgr load fixes that are already on master (association QOS merge,
DefaultAccount marking the default association, per-cluster scoping of the
load user modify); none of them exercises declarative load, which is
test_102_19.py. This file is the baseline that keeps the additive path from
regressing while declarative is built on the same code.
"""

import logging
import os
import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables for entity names
test_cluster = "test_cluster1"
test_cluster_second = "test_cluster_second"
test_account1 = "test_acct1"
test_account2 = "test_acct2"
test_user1 = "test_user1"
test_user2 = "test_user2"
test_qos1 = "test_qos1"
test_qos2 = "test_qos2"
test_qos3 = "test_qos3"

test_name = os.path.splitext(os.path.basename(__file__))[0]

# Names for the three-phase integration workflow (test_name-prefixed)
tc1 = f"{test_name}-cluster-1"
ta1 = f"{test_name}-account.1"
ta2 = f"{test_name}-account.2"
ta3 = f"{test_name}-account.3"
tu1 = f"{test_name}-user.1"
tu2 = f"{test_name}-user.2"
tu3 = f"{test_name}-user.3"
qs1 = "normal"
qs2 = f"{test_name}.qos"
qs3 = f"{test_name}.qos2"

CLASS_CAPACITY = "Capacity"
CLASS_CAPABILITY = "Capability"
CLASS_CAPAPACITY = "Capapacity"

# Association limits for the three-phase integration workflow: one entry per
# option a file line sets, holding the six values the workflow hands out to
# the cluster, the accounts and the users. The values only have to differ per
# index, which is what proves a value landed on the row it was meant for.
#
# The set is wide on purpose and the options are not interchangeable: the
# *CPUs and *CPUMins options go through the TRES conversion and the *Wall
# options through time parsing, so each one covers a different parse path.
#
# The option order here is the order they are written into the file.
ASSOC_LIMITS = {
    "Fairshare": (1000, 2375, 3240, 4321, 5678, 6789),
    "GrpCPUMins": (1100, 2000, 3300, 4000, 5500, 6600),
    "GrpCPUs": (10, 20, 30, 40, 50, 60),
    "GrpJobs": (120, 210, 310, 410, 510, 610),
    "GrpNodes": (140, 230, 330, 430, 530, 630),
    "GrpSubmitJobs": (130, 220, 320, 420, 520, 620),
    "GrpWall": (60, 120, 180, 240, 300, 1440),
    "MaxCPUMins": (110000, 220000, 330000, 420000, 550000, 660000),
    "MaxCPUs": (150, 240, 340, 440, 540, 640),
    "MaxJobs": (160, 250, 350, 450, 550, 650),
    "MaxNodes": (180, 270, 370, 470, 570, 670),
    "MaxSubmitJobs": (170, 260, 360, 460, 560, 660),
    "MaxWall": (70, 140, 210, 280, 350, 2880),
}

# What a User line carries; the Grp* limits are account level only.
USER_LIMITS = (
    "MaxCPUMins",
    "MaxCPUs",
    "MaxJobs",
    "MaxNodes",
    "MaxSubmitJobs",
    "MaxWall",
)

QOS2_INFO1 = {
    "Description": "qos_temp",
    "Flags": "DenyOnLimit,EnforceUsageThreshold,NoDecay",
    "GraceTime": "70",
    "GrpJobsAccrue": "2",
    "GrpJobs": "120",
    "GrpSubmitJobs": "130",
    "GrpTres": "cpu=1",
    "GrpTresMins": "cpu=2",
    "GrpTresRunMins": "cpu=3",
    "GrpWall": "60",
    "LimitFactor": "2.000000",
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
    "Preempt": "normal",
    "PreemptMode": "cancel",
    "PreemptExemptTime": "60",
    "Priority": "10010",
    "UsageFactor": "0.500000",
    "UsageThreshold": "2.500000",
}

QOS2_INFO2 = {
    "Description": "qos_temp2",
    "Flags": "DenyOnLimit",
    "GraceTime": "80",
    "GrpJobsAccrue": "3",
    "GrpJobs": "110",
    "GrpSubmitJobs": "110",
    "GrpTres": "cpu=2",
    "GrpTresMins": "cpu=1",
    "GrpTresRunMins": "cpu=4",
    "GrpWall": "70",
    "LimitFactor": "2.400000",
    "MaxJobsPerAccount": "120",
    "MaxJobsPerUser": "210",
    "MaxJobsAccruePerAccount": "120",
    "MaxJobsAccruePerUser": "230",
    "MaxSubmitJobsPerAccount": "190",
    "MaxSubmitJobsPerUser": "2",
    "MaxTresMinsPerJob": "cpu=4",
    "MaxTresPerAccount": "cpu=1",
    "MaxTresPerJob": "cpu=4",
    "MaxTresPerNode": "cpu=5",
    "MaxTresPerUser": "cpu=6",
    "MaxTRESRunMinsPerAccount": "cpu=2",
    "MaxTRESRunMinsPerUser": "cpu=4",
    "MaxWallDurationPerJob": "70",
    "MinPrioThresh": "10",
    "MinTRESPerJob": "cpu=60",
    "Preempt": qs3,
    "PreemptMode": "requeue",
    "PreemptExemptTime": "10",
    "Priority": "10110",
    "UsageFactor": "0.250000",
    "UsageThreshold": "2.300000",
}

ASSOC_FMT_WITH_PART = (
    "Cluster,Account,User,Partition,Fairshare,GrpCPUMins,GrpCPUs,GrpJobs,"
    "GrpNodes,GrpSubmitJobs,GrpWall,MaxCPUs,MaxCPUMins,MaxJobs,MaxNodes,"
    "MaxSubmitJobs,MaxWall,QOS"
)
ASSOC_FMT = (
    "Cluster,Account,User,Fairshare,GrpCPUMins,GrpCPUs,GrpJobs,GrpNodes,"
    "GrpSubmitJobs,GrpWall,MaxCPUs,MaxCPUMins,MaxJobs,MaxNodes,"
    "MaxSubmitJobs,MaxWall,QOS"
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    # Needed by the duplicate-WCKey test. Set here rather than in that test,
    # since in auto-config mode this rewrites slurmdbd.conf and restarts the
    # daemon, and doing that partway through the module would leave every
    # later test running under a config the earlier ones did not have.
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db():
    """Add slurm user on root before each test; remove test entities after."""
    atf.run_command(
        f"sacctmgr -i add user {atf.properties['slurm-user']} account=root",
        user=atf.properties["slurm-user"],
    )
    yield
    atf.run_command(
        f"sacctmgr -i remove user {test_user1},{test_user2},{tu1},{tu2},{tu3}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove account {test_account1},{test_account2},{ta1},{ta2},{ta3}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove cluster {test_cluster},{test_cluster_second},{tc1}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove qos {test_qos1},{test_qos2},{test_qos3},{qs2},{qs3}",
        user=atf.properties["slurm-user"],
    )


def _limit(name, n):
    """The value of one association limit at index n (1-6)."""
    return ASSOC_LIMITS[name][n - 1]


def _limits(n):
    """Association limit option string for index n (1-6)."""
    return ":".join(f"{name}={_limit(name, n)}" for name in ASSOC_LIMITS)


def _user_limits(fs_n, max_n):
    """User line limit options: Fairshare from fs_n, the Max* set from max_n."""
    opts = [f"Fairshare={_limit('Fairshare', fs_n)}"]
    opts += [f"{name}={_limit(name, max_n)}" for name in USER_LIMITS]
    return ":".join(opts)


def _qos_line(name, options):
    opts = ":".join(f"{key}={value}" for key, value in options.items())
    return f"QOS - '{name}':{opts}"


def write_load_file(path, content):
    """Write a sacctmgr load file as the slurm user (remote-exec safe)."""
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _write_config(path, lines):
    write_load_file(path, "\n".join(lines) + "\n")


def _convert_time_str(time_str, units):
    """Match expect globals convert_time_str for QOS limit checks."""
    if not time_str or time_str.strip() == "":
        return ""
    match = re.match(r"(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+)", time_str.strip())
    if not match:
        return time_str
    days, hours, mins, secs = match.groups()
    hours = int(hours or 0)
    mins = int(mins or 0)
    secs = int(secs or 0)
    if days:
        hours += int(days) * 24
    value = hours * 3600 + mins * 60 + secs
    if units == "hours":
        return str(value // 3600)
    if units == "mins":
        return str(value // 60)
    return str(value)


def _check_qos_limits(qos_name, expected):
    """Port of expect globals_accounting check_qos_limits."""
    fmt = ",".join(expected.keys())
    output = atf.run_command_output(
        f"sacctmgr -n -P list qos {qos_name} format={fmt}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    values = output.strip().split("|")
    assert len(values) == len(expected), (
        f"QOS {qos_name} field count mismatch: got {len(values)} fields, "
        f"expected {len(expected)}: {values!r}"
    )
    mismatches = []
    for option, exp_val, in_val in zip(expected.keys(), expected.values(), values):
        if exp_val == "-1" and in_val == "":
            in_val = "-1"
        elif ":" in str(exp_val):
            pass
        elif option == "GraceTime":
            in_val = _convert_time_str(in_val, "secs")
        elif (
            "MaxWall" in option or "GrpWall" in option or option == "PreemptExemptTime"
        ):
            in_val = _convert_time_str(in_val, "mins")
        if str(exp_val) != str(in_val):
            mismatches.append(f"{option} expected {exp_val!r} got {in_val!r}")
    assert not mismatches, f"QOS {qos_name} limits mismatch: " + "; ".join(mismatches)


def get_assoc_rows(cluster, fmt_fields):
    """Return {(account, user, partition): {field: value}} for list assoc.

    Parses -n -P output by field name so callers assert exact per-field
    values instead of regex-bridged column matches. Partition is part of the
    key so a partition-scoped association cannot overwrite the row for the
    same account and user without one; it reads as "" when the format does
    not request it.
    """
    fields = fmt_fields.split(",")
    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc cluster={cluster} format={fmt_fields}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    rows = {}
    for line in output.strip().split("\n"):
        if not line.strip():
            continue
        values = [p.strip() for p in line.split("|")]
        row = dict(zip(fields, values))
        rows[(row["Account"], row["User"], row.get("Partition", ""))] = row
    return rows


def _assert_assoc_rows(rows, expected, label):
    """Assert the associations are exactly the expected set, field by field.

    Expected keys are (account, user) for the association with no partition,
    or (account, user, partition). The set is compared exactly, so an
    association the load should have removed fails here rather than going
    unnoticed because every expected row still happens to be present.
    """
    normalized = {}
    for key, fields in expected.items():
        if len(key) == 2:
            key = (key[0], key[1], "")
        normalized[key] = fields

    assert set(rows) == set(normalized), (
        f"{label}: association set mismatch; "
        f"unexpected {sorted(set(rows) - set(normalized))!r}, "
        f"missing {sorted(set(normalized) - set(rows))!r}"
    )

    for key, fields in normalized.items():
        account, user, _partition = key
        got = rows[key]
        for field, value in fields.items():
            got_val = got[field]
            # sacctmgr renders wall limits as [D-]HH:MM:SS, but _limits()
            # writes them into the file as minutes. Compare in minutes.
            if field in ("GrpWall", "MaxWall"):
                got_val = _convert_time_str(got_val, "mins")
            assert got_val == str(value), (
                f"{label}: account={account} user={user} field {field} "
                f"expected {value!r}, got {got[field]!r}"
            )


def _acct_assoc(n):
    """Expected account-level assoc fields when the line uses _limits(n)."""
    return dict(
        {name: _limit(name, n) for name in ASSOC_LIMITS},
        QOS=qs1,
    )


def _user_assoc(fs_n, max_n):
    """Expected user assoc fields: Fairshare from fs_n, Max* from max_n."""
    return dict(
        {name: _limit(name, max_n) for name in USER_LIMITS},
        Fairshare=_limit("Fairshare", fs_n),
        QOS=qs1,
    )


def _root_user_assoc(n):
    """Expected (root, root) user assoc: Max* from n, QOS only."""
    return dict(
        {name: _limit(name, n) for name in USER_LIMITS},
        QOS=qs1,
    )


def _parse_parsable_row(output):
    """Return the field list from one sacctmgr -n -P row.

    -P emits no trailing delimiter, so a line ending in '|' means the last
    requested field is empty and the split has to keep it.
    """
    line = output.strip()
    if not line:
        return []
    return line.split("|")


def _assert_parsable_row(output, field_names, expected, label=""):
    """Assert sacctmgr -P output fields match expected values by name."""
    parts = _parse_parsable_row(output)
    assert len(parts) == len(field_names), (
        f"{label}: expected {len(field_names)} fields, got {len(parts)}: " f"{parts!r}"
    )
    got = dict(zip(field_names, parts))
    for key, val in expected.items():
        assert (
            got[key] == val
        ), f"{label}: field {key} expected {val!r}, got {got[key]!r}"
    return got


def _assert_coordinators(expected_rows, label):
    output = atf.run_command_output(
        "sacctmgr -n -P list user format=User,Coordinator,AdminLevel withcoordinator",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for user, coord, admin in expected_rows:
        pattern = rf"{re.escape(user)}\|{re.escape(coord)}\|{re.escape(admin)}"
        assert re.search(
            pattern, output
        ), f"{label}: expected coordinator row {user}/{coord}/{admin} not in:\n{output}"


def _assert_acct_associations_consistent():
    """Asserts the stored associations have no duplicate (cluster, lineage).

    Ports the modern half of testsuite/expect/globals's
    check_acct_associations: lists every association's lineage path and
    fails if the same (cluster, lineage) pair shows up twice, which would
    mean two associations collapsed onto the same spot in the hierarchy.
    The legacy lft/rgt hole check that function also does only matters for
    clusters at or before Slurm 23.02, which is out of scope for the
    current + 3 prior versions this testsuite runs against, so it is not
    ported.
    """
    output = atf.run_command_output(
        "sacctmgr -n -P list assoc wopi wopl withd format=lineage,cluster",
        fatal=True,
    )
    seen = set()
    for line in output.strip().split("\n"):
        if not line.strip():
            continue
        lineage, cluster = [p.strip() for p in line.split("|")]
        key = (cluster, lineage)
        assert (
            key not in seen
        ), f"Cluster {cluster} has more than one association at lineage {lineage}"
        seen.add(key)


def _load_file(path):
    result = atf.run_command(
        f"sacctmgr -i load {path}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    return result["stdout"] + result["stderr"]


def _remove_integration_entities():
    atf.run_command(
        f"sacctmgr -i delete user {tu1},{tu2},{tu3}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i delete account {ta1},{ta2},{ta3}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i delete cluster {tc1}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i delete qos {qs2},{qs3}",
        user=atf.properties["slurm-user"],
    )


def _build_file_in():
    lines = [
        f"QOS - '{qs1}'",
        f"QOS - '{qs3}'",
        _qos_line(qs2, QOS2_INFO1),
    ]
    lines.extend(
        [
            f"Cluster - '{tc1}':Classification={CLASS_CAPACITY}:{_limits(6)}:QOS='{qs1}'",
            "Parent - 'root'",
            (
                f"Account - '{ta1}':Description='scienceacct':Organization='scienceorg':"
                f"{_limits(5)}:QOS='{qs1}'"
            ),
            (
                f"Account - '{ta2}':Description='physicsacct':Organization='physicsorg':"
                f"{_limits(4)}:QOS='{qs1}'"
            ),
            f"Parent - '{ta1}'",
            (
                f"Account - '{ta3}':Description='theoryacct':Organization='theoryorg':"
                f"{_limits(3)}:QOS='{qs1}'"
            ),
            f"Parent - '{ta2}'",
            (
                f"User - '{tu1}':Coordinator='{ta2}':DefaultAccount='{ta2}':"
                f"{_user_limits(1, 2)}:QOS='{qs1}':"
                f"AdminLevel=Operator"
            ),
            f"Parent - '{ta3}'",
            (
                f"User - '{tu2}':Coordinator='{ta3}':DefaultAccount='{ta3}':"
                f"{_user_limits(2, 1)}:QOS='{qs1}':"
                f"AdminLevel=Administrator"
            ),
        ]
    )
    return lines


def _build_file_in2():
    lines = [_qos_line(qs2, QOS2_INFO2)]
    lines.extend(
        [
            f"Cluster - '{tc1}':Classification={CLASS_CAPABILITY}",
            "Parent - 'root'",
            f"Account - '{ta1}'",
            (
                f"Account - '{ta3}':Description='scienceacct':Organization='scienceorg':"
                f"{_limits(5)}:QOS='{qs1}'"
            ),
            f"Parent - '{ta1}'",
            f"Account - '{ta2}'",
            f"Parent - '{ta2}'",
            (
                f"User - '{tu3}':Coordinator='{ta1},{ta2},{ta3}':DefaultAccount='{ta2}':"
                f"{_user_limits(2, 2)}:QOS='{qs1}':"
                f"AdminLevel=Administrator"
            ),
            f"Parent - '{ta3}'",
            (
                f"User - '{tu2}':DefaultAccount='{ta3}':{_user_limits(3, 3)}:QOS='{qs1}':AdminLevel=Operator"
            ),
            (f"User - '{tu3}':DefaultAccount='{ta3}':{_user_limits(3, 3)}:QOS='{qs1}'"),
            f"Parent - '{ta1}'",
            (f"User - '{tu3}':DefaultAccount='{ta1}':{_user_limits(2, 1)}:QOS='{qs1}'"),
        ]
    )
    return lines


def _build_file_in3():
    return [
        f"Cluster - '{tc1}':Classification={CLASS_CAPAPACITY}:{_limits(6)}:QOS='{qs1}'",
        "Parent - 'root'",
        (
            f"Account - '{ta1}':Description='scienceacct':Organization='scienceorg':"
            f"{_limits(5)}:QOS='{qs1}'"
        ),
        (
            f"Account - '{ta3}':Description='theoryacct':Organization='theoryorg':"
            f"{_limits(5)}:QOS='{qs1}'"
        ),
        f"Parent - '{ta1}'",
        (
            f"Account - '{ta3}':Description='scienceacct':Organization='scienceorg':"
            f"{_limits(5)}:QOS='{qs1}'"
        ),
        (
            f"Account - '{ta2}':Description='physicsacct':Organization='physicsorg':"
            f"{_limits(4)}:QOS='{qs1}':AdminLevel=Operator"
        ),
        (
            f"User - '{tu3}':Coordinator='{ta1},{ta2},{ta3}':DefaultAccount='{ta1}':"
            f"{_user_limits(2, 1)}:QOS='{qs1}':"
            f"AdminLevel=Administrator"
        ),
        f"Parent - '{ta2}'",
        (
            f"User - '{tu1}':Coordinator='{ta2}':DefaultAccount='{ta2}':"
            f"{_user_limits(1, 2)}:QOS='{qs1}':"
            f"AdminLevel=Operator"
        ),
        (f"User - '{tu3}':DefaultAccount='{ta2}':{_user_limits(2, 2)}:QOS='{qs1}'"),
        f"Parent - '{ta3}'",
        (
            f"User - '{tu2}':Coordinator='{ta3}':DefaultAccount='{ta3}':"
            f"{_user_limits(3, 3)}:QOS='{qs1}'"
        ),
        (f"User - '{tu3}':DefaultAccount='{ta3}':{_user_limits(3, 3)}:QOS='{qs1}'"),
    ]


def test_load_file_three_phase_workflow():
    """Three-phase sacctmgr load: initial, additive modify, clean replace."""
    file_in = "input"
    file_in2 = "input2"
    file_in3 = "input3"

    _remove_integration_entities()

    _write_config(file_in, _build_file_in())
    _write_config(file_in2, _build_file_in2())
    _write_config(file_in3, _build_file_in3())

    out1 = _load_file(file_in)
    assert f"For cluster {tc1}" in out1, "Load 1 should report cluster"
    assert (
        f"Classification: {CLASS_CAPACITY}" in out1
    ), "Load 1 should set Classification"

    rows1 = get_assoc_rows(tc1, ASSOC_FMT_WITH_PART)
    _assert_assoc_rows(
        rows1,
        {
            ("root", ""): _acct_assoc(6),
            ("root", "root"): _root_user_assoc(6),
            (ta1, ""): _acct_assoc(5),
            (ta3, ""): _acct_assoc(3),
            (ta3, tu2): _user_assoc(2, 1),
            (ta2, ""): _acct_assoc(4),
            (ta2, tu1): _user_assoc(1, 2),
        },
        "Associations after load 1",
    )

    _assert_coordinators(
        [
            (tu1, ta2, "Operator"),
            (tu2, ta3, "Administrator"),
        ],
        "Coordinators after load 1",
    )

    _check_qos_limits(qs2, QOS2_INFO1)

    out2 = _load_file(file_in2)
    assert f"For cluster {tc1}" in out2, "Load 2 should report cluster"
    assert (
        f"{CLASS_CAPACITY} -> {CLASS_CAPABILITY}" in out2
    ), "Load 2 should report Classification change"

    _check_qos_limits(qs2, QOS2_INFO2)

    rows2 = get_assoc_rows(tc1, ASSOC_FMT)
    _assert_assoc_rows(
        rows2,
        {
            ("root", ""): _acct_assoc(6),
            ("root", "root"): _root_user_assoc(6),
            (ta1, ""): _acct_assoc(5),
            (ta1, tu3): _user_assoc(2, 1),
            (ta2, ""): _acct_assoc(4),
            (ta2, tu1): _user_assoc(1, 2),
            (ta2, tu3): _user_assoc(2, 2),
            (ta3, ""): _acct_assoc(5),
            (ta3, tu2): _user_assoc(3, 3),
            (ta3, tu3): _user_assoc(3, 3),
        },
        "Associations after load 2",
    )

    _assert_coordinators(
        [
            (tu1, ta2, "Operator"),
            (tu2, ta3, "Operator"),
            (tu3, f"{ta1},{ta2},{ta3}", "Administrator"),
        ],
        "Coordinators after load 2",
    )

    result3 = atf.run_command(
        f"sacctmgr -i load {file_in3} clean",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out3 = result3["stdout"] + result3["stderr"]
    assert f"For cluster {tc1}" in out3, "Load 3 should report cluster"
    assert (
        f"Classification: {CLASS_CAPAPACITY}" in out3
    ), "Load 3 should set Classification"

    rows3 = get_assoc_rows(tc1, ASSOC_FMT_WITH_PART)
    _assert_assoc_rows(
        rows3,
        {
            ("root", ""): _acct_assoc(6),
            ("root", "root"): _root_user_assoc(6),
            (ta1, ""): _acct_assoc(5),
            (ta1, tu3): _user_assoc(2, 1),
            (ta2, ""): _acct_assoc(4),
            (ta2, tu1): _user_assoc(1, 2),
            (ta2, tu3): _user_assoc(2, 2),
            (ta3, ""): _acct_assoc(5),
            (ta3, tu2): _user_assoc(3, 3),
            (ta3, tu3): _user_assoc(3, 3),
        },
        "Associations after clean load",
    )

    _assert_coordinators(
        [
            (tu1, ta2, "Operator"),
            (tu2, ta3, "Operator"),
            (tu3, f"{ta1},{ta2},{ta3}", "Administrator"),
        ],
        "Coordinators after clean load",
    )

    _assert_acct_associations_consistent()


def test_basic_load_and_dump():
    """A dump names every entity a load put in the database.

    Covers the file format end to end, Cluster then QOS then Parent then
    Account then User, and the Parent lines setting the association context
    that the accounts and the user hang off. What the reloaded dump
    preserves is test_dump_reload_round_trip_preserves_assoc.
    """

    config_file = "basic_test.cfg"
    dump_file = "dump_test1.cfg"

    # Pre-create QOS entities (non-fatal: QOS persists across database restores)
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
    )

    # Create a config file with cluster, accounts, and user
    config_text = f"""Cluster - '{test_cluster}':Fairshare=100:QOS='{test_qos1}'
Parent - 'root'
Account - '{test_account1}':Description='Test Account 1':Organization='TestOrg':Fairshare=50:GrpJobs=10:MaxJobs=20:QOS='{test_qos1},{test_qos2}'
Account - '{test_account2}':Description='Test Account 2':Fairshare=30:GrpJobs=5:QOS='{test_qos1}'
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account1}':Fairshare=25:MaxJobs=15:QOS='{test_qos1},{test_qos2}'
"""
    write_load_file(config_file, config_text)

    # Load the configuration
    atf.run_command(
        f"sacctmgr -i load {config_file}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Dump the configuration
    atf.run_command(
        f"sacctmgr dump {test_cluster} file={dump_file}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Verify dump file was created and contains expected content
    for expected in (test_account1, test_account2, test_user1):
        atf.assert_file_contents(dump_file, expected, contains=True)


def test_load_with_clean():
    """Bare clean removes the cluster's associations, not the records.

    clean with no options removes the cluster along with its associations,
    while the account, user and QOS records themselves survive. Reading only
    list assoc cannot tell that apart from clean having deleted the records,
    so the association and the record are asserted separately.
    """

    config_file1 = "clean_test1.cfg"
    config_file2 = "clean_test2.cfg"

    # Pre-create QOS entities (non-fatal: QOS persists across database restores)
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
    )

    # Initial configuration with two accounts and a user under the second, so
    # the clean load below leaves all three unlisted.
    config_text1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10
Account - '{test_account2}':Fairshare=30:GrpJobs=5
Parent - '{test_account2}'
User - '{test_user1}':DefaultAccount='{test_account2}':Fairshare=25
"""
    write_load_file(config_file1, config_text1)

    atf.run_command(
        f"sacctmgr -i load {config_file1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Verify both accounts exist
    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1},{test_account2} cluster={test_cluster} format=Account",
        fatal=True,
    )
    assert test_account1 in output, "Account 1 should exist after first load"
    assert test_account2 in output, "Account 2 should exist after first load"

    # Second load with clean option - only account1
    config_text2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=60
"""
    write_load_file(config_file2, config_text2)

    atf.run_command(
        f"sacctmgr -i load {config_file2} clean",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Verify account2 was removed
    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account2} cluster={test_cluster} format=Account",
        fatal=True,
    )
    assert test_account2 not in output, "Account 2 should be removed with clean option"

    # The association is gone, but the records it referenced are not. Without
    # these an accidental escalation of clean to deleting account, user or QOS
    # records would satisfy the assertion above.
    output = atf.run_command_output(
        f"sacctmgr -n -P list account {test_account2} format=Account",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        test_account2 in output
    ), "Bare clean should keep the account record, removing only its association"

    output = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} format=User",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert test_user1 in output, "Bare clean should keep the user record"

    output = atf.run_command_output(
        f"sacctmgr -n -P list qos {test_qos1} format=Name",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert test_qos1 in output, "Bare clean should not remove QOS definitions"

    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account=root cluster={test_cluster} "
        f"format=Account",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert "root" in output, "Bare clean should leave the root association"

    # Verify account1 still exists with updated values
    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} cluster={test_cluster} format=Account,Fairshare",
        fatal=True,
    )
    _assert_parsable_row(
        output,
        ["Account", "Fairshare"],
        {"Account": test_account1, "Fairshare": "60"},
        "Account 1 fairshare after clean",
    )

    _assert_acct_associations_consistent()


def test_load_clean_clears_assoc_qos_when_omitted():
    """Load with clean and no QOS on the account line drops file-specific QOS.

    After cluster flush, the account may inherit the cluster default QOS (normal);
    the test checks that QOS from the prior load file (test_qos1) is not kept.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
    )
    cfg1 = "clean_qos_clear1.cfg"
    cfg2 = "clean_qos_clear2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:QOS='{test_qos1}'
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out_before = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert test_qos1 in out_before, "QOS should be set before clean load"
    atf.run_command(
        f"sacctmgr -i load {cfg2} clean",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out_after = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=QOS",
        fatal=True,
    )
    assert (
        test_qos1 not in out_after
    ), "clean load without QOS must not keep QOS from the prior file"


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: additive QOS merge (file QOS appended, not replaced) requires 26.11+",
)
def test_comprehensive_modify_behavior():
    """Test comprehensive modification covering account metadata, account associations, and user associations

    Validates:
    - Complete integration: Metadata, account associations, and user associations all modified
    - Field-level modifications: All three categories updated together
    - Additive QOS: Second load adds test_qos2 to existing test_qos1 (not replace)
    - Parent - line context: User association created under test_account1 (from Parent line)
    - DefaultAccount metadata: Independent from association account context
    - Case sensitivity: Metadata assertions use .lower()
    """
    config_file1 = "comprehensive_test1.cfg"
    config_file2 = "comprehensive_test2.cfg"

    # Pre-create QOS entities (non-fatal: QOS persists across database restores)
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
    )

    # Initial configuration with many fields set
    config_text1 = f"""Cluster - '{test_cluster}':Fairshare=100
Parent - 'root'
Account - '{test_account1}':Description='Initial Desc':Organization='InitOrg':Fairshare=50:GrpJobs=10:MaxJobs=20:Priority=100:QOS='{test_qos1}'
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account1}':Fairshare=25:MaxJobs=15:Priority=50:QOS='{test_qos1}'
"""
    write_load_file(config_file1, config_text1)

    atf.run_command(
        f"sacctmgr -i load {config_file1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Verify initial state - all three areas
    metadata_output = atf.run_command_output(
        f"sacctmgr -n -P list account {test_account1} format=Account,Description,Organization",
        fatal=True,
    )
    assert (
        "initial desc" in metadata_output.lower()
    ), "Initial description should be set"
    assert "initorg" in metadata_output.lower(), "Initial organization should be set"

    acct_assoc_output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' cluster={test_cluster} format=Account,Fairshare,GrpJobs,MaxJobs,Priority,QOS",
        fatal=True,
    )
    _assert_parsable_row(
        acct_assoc_output,
        ["Account", "Fairshare", "GrpJobs", "MaxJobs", "Priority", "QOS"],
        {
            "Account": test_account1,
            "Fairshare": "50",
            "GrpJobs": "10",
            "MaxJobs": "20",
            "Priority": "100",
            "QOS": test_qos1,
        },
        "Initial account association",
    )

    user_assoc_output = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} account={test_account1} cluster={test_cluster} format=User,Account,Fairshare,MaxJobs,Priority,QOS",
        fatal=True,
    )
    _assert_parsable_row(
        user_assoc_output,
        ["User", "Account", "Fairshare", "MaxJobs", "Priority", "QOS"],
        {
            "User": test_user1,
            "Account": test_account1,
            "Fairshare": "25",
            "MaxJobs": "15",
            "Priority": "50",
            "QOS": test_qos1,
        },
        "Initial user association",
    )

    # Second load - modify various fields
    # test_user2 appears only here, so the same load that modifies covers
    # adding alongside modifying.
    config_text2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Description='Modified Desc':Organization='ModOrg':Fairshare=60:GrpJobs=15:MaxJobs=25:Priority=200:QOS='{test_qos2}'
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account1}':Fairshare=30:MaxJobs=18:Priority=75:QOS='{test_qos2}'
User - '{test_user2}':DefaultAccount='{test_account1}':Fairshare=20
"""
    write_load_file(config_file2, config_text2)

    output = atf.run_command_output(
        f"sacctmgr -i load {config_file2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    logging.debug(f"sacctmgr load output: {output}")

    # Check account metadata modifications
    metadata_output = atf.run_command_output(
        f"sacctmgr -n -P list account {test_account1} format=Account,Description,Organization",
        fatal=True,
    )
    logging.debug(f"Account metadata after modification: {metadata_output}")
    assert (
        "modified desc" in metadata_output.lower()
    ), "Account description should be updated"
    assert "modorg" in metadata_output.lower(), "Account organization should be updated"

    # Check account association modifications
    acct_assoc_output = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' cluster={test_cluster} format=Account,Fairshare,GrpJobs,MaxJobs,Priority,QOS",
        fatal=True,
    )
    logging.debug(f"Account association after modification: {acct_assoc_output}")
    acct_got = _assert_parsable_row(
        acct_assoc_output,
        ["Account", "Fairshare", "GrpJobs", "MaxJobs", "Priority", "QOS"],
        {
            "Account": test_account1,
            "Fairshare": "60",
            "GrpJobs": "15",
            "MaxJobs": "25",
            "Priority": "200",
        },
        "Account association after modification",
    )
    acct_qos = set(acct_got["QOS"].split(","))
    assert {
        test_qos1,
        test_qos2,
    } <= acct_qos, "Account QOS should merge additively (both test_qos1 and test_qos2)"

    # Check user association modifications
    user_assoc_output = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} account={test_account1} cluster={test_cluster} format=User,Account,Fairshare,MaxJobs,Priority,QOS",
        fatal=True,
    )
    logging.debug(f"User association after modification: {user_assoc_output}")
    user_got = _assert_parsable_row(
        user_assoc_output,
        ["User", "Account", "Fairshare", "MaxJobs", "Priority", "QOS"],
        {
            "User": test_user1,
            "Account": test_account1,
            "Fairshare": "30",
            "MaxJobs": "18",
            "Priority": "75",
        },
        "User association after modification",
    )
    user_qos = set(user_got["QOS"].split(","))
    assert {
        test_qos1,
        test_qos2,
    } <= user_qos, "User QOS should merge additively (both test_qos1 and test_qos2)"

    new_user_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user2} account={test_account1} "
        f"cluster={test_cluster} format=User,Fairshare",
        fatal=True,
    )
    new_user = [p.strip() for p in new_user_out.strip().split("|")]
    assert new_user[0] == test_user2, "User only in the second file should be added"
    assert new_user[1] == "20", "Added user should take the Fairshare from the file"


ERROR_LOAD_CASES = [
    pytest.param(
        f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=50\n",
        ("You need to specify a cluster name first",),
        id="no-cluster",
    ),
    pytest.param(
        f"Cluster - '{test_cluster}'\nQOS - 'bad_qos'\nParent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50\n",
        ("You need to specify all QOS before",),
        id="qos-after-cluster",
    ),
    pytest.param(
        f"Cluster - '{test_cluster}'\nParent - 'nonexistent_parent'\n"
        f"Account - '{test_account1}':Fairshare=50\n",
        ("You need to add this parent",),
        id="parent-missing",
    ),
    pytest.param(
        f"Cluster - '{test_cluster}'\nParent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50\n"
        f"Cluster - 'test_cluster2'\nAccount - '{test_account2}':Fairshare=30\n",
        ("You can only add one cluster at a time",),
        id="multiple-clusters",
    ),
    pytest.param(
        f"QOS - 'duplicate_qos':Priority=100\nQOS - 'duplicate_qos':Priority=200\n"
        f"Cluster - '{test_cluster}'\nParent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50\n",
        ("has multiple entries",),
        id="duplicate-qos",
    ),
    pytest.param(
        f"Cluster - '{test_cluster}'\nParent - 'root'\nAccount - ':Fairshare=50'\n",
        ("No name given",),
        id="no-name",
    ),
    pytest.param(
        f"Cluster - '{test_cluster}'\nParent - 'root'\n"
        f"Account - '{test_account1}':UnknownOption=50\n",
        ("Unknown option",),
        id="unknown-option",
    ),
    pytest.param(
        f"QOS - '{test_qos1}'\nCluster - '{test_cluster}'\nParent - 'root'\nAccount -\n",
        ("Nothing after object",),
        id="nothing-after-object",
    ),
    pytest.param(
        f"QOS - '{test_qos1}'\nCluster - '{test_cluster}'\nParent - 'root'\n"
        f"Account - 'a':Description='unclosed\n",
        ("quotes",),
        id="unbalanced-quotes",
    ),
    pytest.param(
        f"QOS - '{test_qos1}'\nCluster - '{test_cluster}'\nParent - 'root'\n"
        f"Account - '{test_account1}':Fairshare=50:QOS='nonexistent_qos_xyz'\n",
        ("bad qos", "nonexistent_qos_xyz"),
        id="account-nonexistent-qos",
    ),
]


def _assert_stderr_has(stderr, expected, label):
    """Check stderr against expected substrings, all case-insensitive.

    Every entry has to appear. An entry that is itself a tuple is an
    alternation, for the messages that differ by which check rejected the
    line first.
    """
    for want in expected:
        alts = want if isinstance(want, tuple) else (want,)
        assert any(
            a.lower() in stderr.lower() for a in alts
        ), f"{label}: stderr should mention one of {alts}: {stderr}"


@pytest.mark.parametrize("content, expected", ERROR_LOAD_CASES)
def test_error_load_rejected(content, expected):
    """sacctmgr load rejects a malformed file with a specific error message."""
    cfg = "error_load.cfg"
    write_load_file(cfg, content)
    result = atf.run_command(
        f"sacctmgr -i load {cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    _assert_stderr_has(result["stderr"], expected, "load of a malformed file")


ERROR_LOAD_COMMAND_CASES = [
    pytest.param(
        {
            "content": f"Cluster - '{test_cluster}'\nParent - 'root'\n"
            f"Account - '{test_account1}':Fairshare50\n",
            "cmd": "-i load {cfg}",
            # Which of the two fires depends on how far the parse gets.
            "expected": [("Bad format", "Unknown option")],
        },
        id="option-without-equals",
    ),
    pytest.param(
        {
            "cmd": "-i load",
            "expected": ["No filename given"],
        },
        id="no-filename",
    ),
    pytest.param(
        {
            "cmd": "-i load file=/nonexistent/path/load.cfg",
            "expected": ["Unable to read"],
        },
        id="unreadable-file",
    ),
    pytest.param(
        {
            "content": f"QOS - '{test_qos1}'\nCluster - '{test_cluster}'\n"
            f"Parent - 'root'\nAccount - '{test_account1}':Fairshare=50\n",
            "cmd": "-r load file={cfg}",
            "expected": ["readonly mode"],
        },
        id="readonly-mode",
    ),
    pytest.param(
        {
            "content": f"Cluster - '{test_cluster}'\nParent - 'root'\n",
            "content2": f"Cluster - '{test_cluster}'\nParent - 'root'\n",
            "cmd": "-i load file={cfg} file={cfg2}",
            "expected": ["File name already set"],
        },
        id="duplicate-file-option",
    ),
]


@pytest.mark.parametrize("case", ERROR_LOAD_COMMAND_CASES)
def test_error_load_command_rejected(case):
    """sacctmgr rejects a bad load command line with a specific message.

    The file is fine in these cases, or absent; what is wrong is the command
    around it, so the command is what varies. ERROR_LOAD_CASES above covers
    the other direction, a fixed command against a malformed file.
    """
    cfg = "error_load_cmd.cfg"
    cfg2 = "error_load_cmd2.cfg"
    if case.get("content"):
        write_load_file(cfg, case["content"])
    if case.get("content2"):
        write_load_file(cfg2, case["content2"])

    command = case["cmd"].format(cfg=cfg, cfg2=cfg2)
    result = atf.run_command(
        f"sacctmgr {command}",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    _assert_stderr_has(result["stderr"], case["expected"], command)


def test_warning_no_parent_specified():
    """Test informational message when no parent is specified

    Validates:
    - Default parent is root: When no Parent - line is given, defaults to root
    - Warning message: Prints informational message about defaulting to root
    - This happens even when root is the intended parent (idiosyncrasy)
    """

    config_file = "no_parent_warning.cfg"

    # Don't specify Parent - should default to root with a warning
    config_text = f"""Cluster - '{test_cluster}'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(config_file, config_text)

    output = atf.run_command_output(
        f"sacctmgr -i load {config_file}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert (
        "No parent given creating off root" in output
    ), "Should show informational message about default parent"

    # Verify account was still created successfully under root
    result = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} cluster={test_cluster} format=Account,ParentName",
        fatal=True,
    )
    assert test_account1 in result, "Account should be created"
    assert "root" in result, "Account should be under root"


def test_error_clean_multiple_clusters():
    """Test error when load clean is used and system has multiple clusters

    Validates:
    - clean=account/user/qos requires exactly one cluster in the system
    """
    # Ensure two clusters exist in the DB so the clean check triggers (atf may
    # not be in accounting yet, so add both explicitly)
    atf.run_command(
        f"sacctmgr -i add cluster {test_cluster}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add cluster {test_cluster_second}",
        user=atf.properties["slurm-user"],
    )
    config_file = "clean_multi_cluster_error.cfg"
    config_text = f"""QOS - '{test_qos1}'
Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
"""
    write_load_file(config_file, config_text)

    # clean=account (or user/qos) triggers the multi-cluster check; bare "clean" does not
    result = atf.run_command(
        f"sacctmgr -i load file={config_file} clean=account",
        user=atf.properties["slurm-user"],
        fatal=True,
        xfail=True,
    )
    assert (
        "only have one cluster" in result["stderr"]
    ), "Should show multi-cluster clean error"


def test_empty_file():
    """Test load of empty file succeeds with nothing new added

    Validates:
    - Empty file or only blank lines: exit 0, no cluster set, nothing added
    """

    config_file = "empty_file.cfg"
    write_load_file(config_file, "")

    result = atf.run_command(
        f"sacctmgr -i load {config_file}",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] == 0, "Empty file load should succeed"
    assert (
        "Nothing new added" in result["stdout"]
    ), "Empty file should report nothing added"


def test_additive_second_load_preserves_omitted_fields():
    """Additive load must not reset limits or QOS omitted on the second file.

    Without the declarative flag, a second load that only changes Fairshare
    must leave GrpJobs, MaxJobs, and QOS from the first load intact.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
    )
    cfg1 = "additive_preserve1.cfg"
    cfg2 = "additive_preserve2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10:MaxJobs=20:QOS='{test_qos1}'
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=75
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {cfg2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs,MaxJobs,QOS",
        fatal=True,
    )
    parts = out.strip().split("|")
    assert len(parts) == 4, "Expect Fairshare|GrpJobs|MaxJobs|QOS"
    assert parts[0].strip() == "75", "Fairshare updated from second load"
    assert parts[1].strip() == "10", "GrpJobs omitted on second load => unchanged"
    assert parts[2].strip() == "20", "MaxJobs omitted on second load => unchanged"
    assert test_qos1 in parts[3], "QOS omitted on second load => unchanged"


def test_assoc_same_parent_other_fields_update():
    """Second additive load with unchanged Parent still updates association limits.

    Regression for parent=parent skipping association modify on existing rows.
    """
    cfg1 = "parent_unchanged1.cfg"
    cfg2 = "parent_unchanged2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10:MaxJobs=20
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=60:GrpJobs=15
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {cfg2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    out = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs,MaxJobs",
        fatal=True,
    )
    parts = out.strip().split("|")
    assert len(parts) == 3, "Expect Fairshare|GrpJobs|MaxJobs"
    assert parts[0].strip() == "60", "Fairshare should update when parent unchanged"
    assert parts[1].strip() == "15", "GrpJobs should update when parent unchanged"
    assert parts[2].strip() == "20", "MaxJobs omitted on second load => unchanged"


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: DefaultAccount marking is_def on the association requires 26.11+",
)
def test_additive_load_default_account_separate_parent_line():
    """DefaultAccount must mark is_def on the association under a separate Parent.

    list user reports default account from the association with is_def=1. The
    load file may set DefaultAccount on a User line under one Parent while the
    matching association appears under another Parent line in the same file.
    """
    cfg = "def_acct_sep_parent.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Account - '{test_account2}':Fairshare=30
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account2}':Fairshare=25
Parent - '{test_account2}'
User - '{test_user1}':Fairshare=10
"""
    write_load_file(cfg, config)
    atf.run_command(
        f"sacctmgr -i load {cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    user_out = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,DefaultA",
        fatal=True,
    )
    parts = [p.strip() for p in user_out.strip().split("|")]
    assert len(parts) == 2, "Expect User|DefaultA"
    assert parts[1] == test_account2, (
        "DefaultAccount from load file must be honored when the default "
        "association is on a separate Parent line"
    )
    acct1_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} account={test_account1} "
        f"cluster={test_cluster} format=User,Fairshare",
        fatal=True,
    )
    acct2_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} account={test_account2} "
        f"cluster={test_cluster} format=User,Fairshare",
        fatal=True,
    )
    _assert_parsable_row(
        acct1_out,
        ["User", "Fairshare"],
        {"User": test_user1, "Fairshare": "25"},
        "User association under first Parent",
    )
    _assert_parsable_row(
        acct2_out,
        ["User", "Fairshare"],
        {"User": test_user1, "Fairshare": "10"},
        "User association under default-account Parent",
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmdbd") < (26, 11),
    reason="Ticket 23787: idempotent duplicate WCKey insert not rolling back the txn requires 26.11+",
)
def test_additive_load_duplicate_wckey_does_not_rollback_assocs():
    """Existing DefaultWCKey on a second additive load must not roll back new assocs.

    When WCKey rows already exist, the idempotent insert must not reset the load
    transaction and undo association adds from the same load.
    """
    test_wckey = f"{test_user1}_wckey"
    cfg1 = "wckey_dup1.cfg"
    cfg2 = "wckey_dup2.cfg"
    config1 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=50
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account1}':DefaultWCKey='{test_wckey}':Fairshare=25
"""
    config2 = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account2}':Fairshare=30
Parent - '{test_account2}'
User - '{test_user1}':DefaultAccount='{test_account1}':DefaultWCKey='{test_wckey}':Fairshare=20
"""
    write_load_file(cfg1, config1)
    write_load_file(cfg2, config2)
    atf.run_command(
        f"sacctmgr -i load {cfg1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {cfg2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    wckey_out = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,DefaultW",
        fatal=True,
    )
    wckey_parts = [p.strip() for p in wckey_out.strip().split("|")]
    assert len(wckey_parts) == 2, "Expect User|DefaultW"
    assert (
        wckey_parts[1] == test_wckey
    ), "DefaultWCKey should remain set after second load"
    acct2_out = atf.run_command_output(
        f"sacctmgr -n -P list assoc user={test_user1} account={test_account2} "
        f"cluster={test_cluster} format=User,Fairshare",
        fatal=True,
    )
    _assert_parsable_row(
        acct2_out,
        ["User", "Fairshare"],
        {"User": test_user1, "Fairshare": "20"},
        "Second-load association after duplicate DefaultWCKey",
    )


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: per-cluster scoping of the load user modify requires 26.11+",
)
def test_additive_load_user_modify_scoped_to_named_cluster():
    """DefaultAccount from load applies only to the cluster named in the file.

    AdminLevel is stored once per user in the accounting DB (not per cluster),
    so this test does not assert cluster-scoped AdminLevel behavior.
    """
    atf.run_command(
        f"sacctmgr -i add cluster {test_cluster},{test_cluster_second}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add account {test_account1},{test_account2} "
        f"cluster={test_cluster},{test_cluster_second} parent=root",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i add user {test_user1} account={test_account1},{test_account2} "
        f"cluster={test_cluster},{test_cluster_second} "
        f"defaultaccount={test_account1}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i mod user {test_user1} cluster={test_cluster} "
        f"set defaultaccount={test_account1}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i mod user {test_user1} cluster={test_cluster_second} "
        f"set defaultaccount={test_account1}",
        user=atf.properties["slurm-user"],
    )
    before_c1 = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,DefaultA",
        fatal=True,
    )
    before_c2 = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster_second} "
        "format=User,DefaultA",
        fatal=True,
    )
    assert test_account1 in before_c1, "Cluster1 should start with DefaultAccount acct1"
    assert test_account2 not in before_c2, "Cluster2 must not start at the loaded value"
    assert test_account1 in before_c2, "Cluster2 should start with DefaultAccount acct1"

    cfg = "user_scope_cluster1.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}'
Account - '{test_account2}'
Parent - '{test_account1}'
User - '{test_user1}':DefaultAccount='{test_account2}':Fairshare=50
"""
    write_load_file(cfg, config)
    atf.run_command(
        f"sacctmgr -i load {cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    after_c1 = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster} "
        "format=User,DefaultA",
        fatal=True,
    )
    after_c2 = atf.run_command_output(
        f"sacctmgr -n -P list user {test_user1} cluster={test_cluster_second} "
        "format=User,DefaultA",
        fatal=True,
    )
    c1_parts = [p.strip() for p in after_c1.strip().split("|")]
    c2_parts = [p.strip() for p in after_c2.strip().split("|")]
    assert len(c1_parts) == 2, "Expect User|DefaultA for cluster1"
    assert len(c2_parts) == 2, "Expect User|DefaultA for cluster2"
    assert (
        c1_parts[1] == test_account2
    ), "Load file for cluster1 should update DefaultAccount on cluster1"
    assert (
        c2_parts[1] == test_account1
    ), "Cluster2 DefaultAccount must be unchanged by load scoped to cluster1"


def test_dump_reload_round_trip_preserves_assoc():
    """Dump after load and additive reload of the dump reproduces association limits."""
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1}",
        user=atf.properties["slurm-user"],
    )
    cfg = "roundtrip_src.cfg"
    dump_cfg = "roundtrip_dump.cfg"
    config = f"""Cluster - '{test_cluster}':Fairshare=100
Parent - 'root'
Account - '{test_account1}':Fairshare=50:GrpJobs=10:MaxJobs=20:QOS='{test_qos1}'
"""
    write_load_file(cfg, config)
    atf.run_command(
        f"sacctmgr -i load {cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    before = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs,MaxJobs,QOS",
        fatal=True,
    )
    # Pin the pre-dump values so the round-trip comparison below cannot pass
    # by finding the association absent both times.
    before_parts = before.strip().split("|")
    assert len(before_parts) == 4, "Expect Fairshare|GrpJobs|MaxJobs|QOS"
    assert before_parts[0].strip() == "50", "Fairshare set by the initial load"
    assert before_parts[1].strip() == "10", "GrpJobs set by the initial load"
    assert before_parts[2].strip() == "20", "MaxJobs set by the initial load"
    assert test_qos1 in before_parts[3], "QOS set by the initial load"

    atf.run_command(
        f"sacctmgr dump {test_cluster} file={dump_cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {dump_cfg}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    after = atf.run_command_output(
        f"sacctmgr -n -P list assoc account={test_account1} user='' "
        f"cluster={test_cluster} format=Fairshare,GrpJobs,MaxJobs,QOS",
        fatal=True,
    )
    assert (
        before.strip() == after.strip()
    ), "Reloaded dump should match pre-dump association"


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: partition-less load User line scoping requires 26.11+",
)
def test_partition_user_line_scopes_to_id():
    """Partition-less load User line must not reset partition assocs.

    Creates a default (no Partition) user assoc and a partition-specific one.
    A plain (non-declarative) load of a Partition-less User=Fairshare must
    update only the default row; the partition row keeps its MaxJobs.
    """
    # test_cluster is not the live cluster, so sacctmgr never resolves this
    # name against a real partition. A literal keeps the test independent of
    # whether the host config happens to define a default partition.
    part = "load_part_23787"
    cfg_full = "part_full.cfg"
    cfg_partial = "part_partial.cfg"
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
    atf.run_command(
        f"sacctmgr -i load {cfg_full}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i load {cfg_partial}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

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


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: a rejected load modify stopping the load requires 26.11+",
)
def test_removing_qos_a_child_defaults_to_stops_the_load():
    """A rejected load modify must stop the load, not report success.

    Dropping a QOS from an account while a user under it still has that
    QOS as their DefaultQOS makes slurmdbd refuse the modify and discard
    the whole open transaction. A plain (non-declarative) load must stop
    there rather than committing later lines on top of a transaction the
    server already threw away.
    """
    atf.run_command(
        f"sacctmgr -i add qos {test_qos1},{test_qos2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    cfg_full = "defqos_full.cfg"
    cfg_drop = "defqos_drop.cfg"
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
    # A bare QOS list is additive under a plain load (it merges rather than
    # replacing), so "-qos1" is needed here to actually drop it; declarative
    # load is what makes a bare list replace, which is what the counterpart
    # test in test_102_19.py uses instead.
    config_drop = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account2}':Fairshare=77
Account - '{test_account1}':Fairshare=50:QOS='-{test_qos1}'
"""
    write_load_file(cfg_full, config_full)
    write_load_file(cfg_drop, config_drop)
    atf.run_command(
        f"sacctmgr -i load {cfg_full}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    result = atf.run_command(
        f"sacctmgr -i load {cfg_drop}",
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


@pytest.mark.skipif(
    atf.get_version("bin/sacctmgr") < (26, 11),
    reason="Ticket 23787: stopping a load on its own bad options requires 26.11+",
)
@pytest.mark.parametrize(
    "bad_arg",
    ("no_such_option_23787", "no_such_option_23787=5"),
    ids=["bare_word", "key_value"],
)
def test_bad_load_option_applies_nothing(bad_arg):
    """An argument the load cannot use must stop it before the file is read.

    The options say what the load is meant to do, so one that cannot be used
    has to stop it. Both forms are covered: a bare word is taken as a second
    file name, while a key=value that matches nothing is an unknown option.

    The tell that the file was never read is the absence of a line-level
    complaint. exit_code is global and sticky, so without a check on the
    arguments the load goes on to open the file and rejects its first line,
    blaming "Problem with line(1)" for a line that is perfectly valid.
    """
    cfg = "bad_option.cfg"
    config = f"""Cluster - '{test_cluster}'
Parent - 'root'
Account - '{test_account1}':Fairshare=42
"""
    write_load_file(cfg, config)

    result = atf.run_command(
        f"sacctmgr -i load {cfg} {bad_arg}",
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
