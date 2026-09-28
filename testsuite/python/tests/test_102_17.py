############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""test_102_17.py - sacctmgr TresDecayHalfLife.

Ticket 50920: a per-TRES half-life for the decay of GrpTRESMins usage, so a
TRES quota can be kept from replenishing without turning off the fairshare
decay that PriorityDecayHalfLife controls.

This module covers only what sacctmgr stores and reports, so it submits no
jobs. What the value does to usage is test_130_6.py.
"""

import pytest

import atf

acct = "test_acct_102_17"
child = "test_child_102_17"
grandchild = "test_grandchild_102_17"
qos = "test_qos_102_17"

VERSION_REASON = "Ticket 50920: TresDecayHalfLife requires Slurm 26.11"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version((26, 11), "bin/sacctmgr", reason=VERSION_REASON)
    atf.require_version((26, 11), "sbin/slurmdbd", reason=VERSION_REASON)
    atf.require_accounting(modify=True)
    atf.require_slurm_running()

    # 'sacctmgr load' refuses to run for a uid the accounting system does not
    # know about, so the user running these commands needs to exist in it.
    # Existence is all that is needed - the privilege check exempts SlurmUser
    # and root - so do not grant AdminLevel. Add only what is missing and
    # remove exactly that, since under --local-config there is no mysqldump to
    # restore and a site-owned slurm user must survive the run.
    slurm_user = atf.properties["slurm-user"]
    added_acct = added_user = False

    if not atf.run_command_output(
        f"sacctmgr -n -P show account {slurm_user} format=Account",
        user=slurm_user,
    ).strip():
        atf.run_command(
            f"sacctmgr -i add account {slurm_user}",
            user=slurm_user,
            fatal=True,
        )
        added_acct = True

    if not atf.run_command_output(
        f"sacctmgr -n -P show user {slurm_user} format=User",
        user=slurm_user,
    ).strip():
        atf.run_command(
            f"sacctmgr -i add user {slurm_user} Account={slurm_user}",
            user=slurm_user,
            fatal=True,
        )
        added_user = True

    yield

    if added_user:
        atf.run_command(
            f"sacctmgr -i remove user {slurm_user}",
            user=slurm_user,
        )
    if added_acct:
        atf.run_command(
            f"sacctmgr -i remove account {slurm_user}",
            user=slurm_user,
        )


@pytest.fixture(scope="function", autouse=True)
def accounts():
    atf.run_command(
        f"sacctmgr -i add qos {qos}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    # Deepest first: an account cannot be removed while it still has children.
    atf.run_command(
        f"sacctmgr -i remove account {grandchild},{child},{acct}",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"sacctmgr -i remove qos {qos}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="function")
def coordinator():
    """Make the test user a coordinator of acct, with a sub-account under it.

    The restriction only fires for a coordinator acting on an association that
    is not its own, so every modify below targets the sub-account. Yields the
    coordinator's name.
    """
    user = atf.get_user_name()
    slurm_user = atf.properties["slurm-user"]
    added_user = False

    atf.run_command(
        f"sacctmgr -i add account {child} parent={acct}",
        user=slurm_user,
        fatal=True,
    )
    # Add only what is missing, as the module fixture does for SlurmUser: a
    # site-owned test user has to survive the run under --local-config.
    if not atf.run_command_output(
        f"sacctmgr -n -P show user {user} format=User",
        user=slurm_user,
    ).strip():
        added_user = True
    atf.run_command(
        f"sacctmgr -i add user {user} account={acct}",
        user=slurm_user,
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add coordinator account={acct} names={user}",
        user=slurm_user,
        fatal=True,
    )

    yield user

    atf.run_command(
        f"sacctmgr -i remove coordinator account={acct} names={user}",
        user=slurm_user,
    )
    if added_user:
        atf.run_command(f"sacctmgr -i remove user {user}", user=slurm_user)
    else:
        atf.run_command(
            f"sacctmgr -i remove user {user} account={acct}",
            user=slurm_user,
        )


def _set(entity, name, value):
    """Run a sacctmgr modify, returning the result rather than asserting."""
    return atf.run_command(
        f"sacctmgr -i modify {entity} {name} set TresDecayHalfLife={value}",
        user=atf.properties["slurm-user"],
    )


def _coord_set(name, value, coord):
    """Run a sacctmgr modify as the coordinator rather than as SlurmUser."""
    return atf.run_command(
        f"sacctmgr -i modify account {name} set TresDecayHalfLife={value}",
        user=coord,
    )


def _show(entity, name):
    """Return the TresDecayHalfLife sacctmgr reports, as a string.

    The value lives on the association, and 'show account' leaves every
    association field blank unless WithAssoc is asked for, so an account is
    read through 'show assoc' instead.
    """
    if entity == "account":
        cmd = f"show assoc account={name} format=Account,User,TresDecayHalfLife"
    else:
        cmd = f"show {entity} {name} format=Name,TresDecayHalfLife"

    out = atf.run_command_output(
        f"sacctmgr -n -P {cmd}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    for line in out.splitlines():
        parts = [p.strip() for p in line.split("|")]
        if parts[0] != name:
            continue
        # An account owns the association with no user on it; the rest
        # belong to its users.
        if entity == "account" and parts[1]:
            continue
        return parts[-1]
    pytest.fail(f"no {entity} row for {name} in: {out!r}")


def _entity_name(entity):
    """The name of the fixture-created record for the given entity."""
    return acct if entity == "account" else qos


def _as_dict(shown):
    """Parse 'cpu=3600,mem=7200' into {'cpu': '3600', 'mem': '7200'}.

    Compared whole rather than searched, so a value that grew a unit suffix
    on its way out - 7200 printed as the 7200M a memory TRES would get - is
    a mismatch rather than a substring hit.
    """
    return dict(part.split("=", 1) for part in shown.split(",") if part)


@pytest.mark.parametrize(
    "written, expected",
    [
        # A bare number is seconds, the same as the REST API reads it and the
        # same as it is stored, so the two clients cannot disagree.
        ("cpu=3600", "cpu=3600"),
        # The time forms are accepted and converted, never stored as written.
        ("cpu=1-0", "cpu=86400"),
        ("cpu=12:00:00", "cpu=43200"),
        ("cpu=60:00", "cpu=3600"),
        # 0 is the value that matters: that TRES never decays.
        ("cpu=0", "cpu=0"),
        # More than one TRES, each with its own rate.
        ("cpu=3600,mem=7200", "cpu=3600,mem=7200"),
    ],
)
def test_set_and_show(written, expected):
    """sacctmgr stores what was meant, in seconds, and reports it back."""
    _set("account", acct, written)
    got = _show("account", acct)
    assert _as_dict(got) == _as_dict(
        expected
    ), f"{written} should store {expected}, showed {got!r}"


def test_bare_number_is_seconds_not_minutes():
    """A bare number is seconds.

    time_str2secs() reads a bare number as minutes, so a half-life parsed
    that way would be stored 60x too large. Pin the value rather than the
    parse, since that is what an admin and the REST API both see.
    """
    _set("account", acct, "cpu=3600")
    got = _show("account", acct)
    assert _as_dict(got) == {"cpu": "3600"}, "3600 should mean 3600 seconds"


@pytest.mark.parametrize("entity", ["account", "qos"])
@pytest.mark.parametrize(
    "written, why",
    [
        # The amend arithmetic clamps an underflow to 0, and 0 means never
        # decay, so a '-=' that went too far would freeze the TRES silently.
        ("cpu-=7200", "'-=' underflows to 0, which means never decay"),
        # '+=' is the same trap facing the other way: on a TRES sitting at 0
        # it quietly restores decay to a quota meant to be permanent.
        ("cpu+=3600", "'+=' on a frozen TRES restores decay"),
        # Both words reach strtoull() as 0, so they would store the most
        # constrictive value the field has while saying the least.
        ("cpu=INFINITE", "INFINITE would store 0, not 'no limit'"),
        ("cpu=UNLIMITED", "UNLIMITED would store 0, not 'no limit'"),
        # A truncated value took the same path, since strspn("") == strlen("").
        ("cpu=", "an empty value would store 0"),
        # time_str2secs() reads the leading '-' as the days separator, so -5
        # came back as -432000 rather than an error and wrapped through
        # strtoull() to a count just short of INFINITE64 - large enough to
        # round to never decay, but not the removal sentinel, so it stored.
        ("cpu=-5", "a negative other than -1 would store never decay"),
        # -0 reaches the same place by converting to 0 outright.
        ("cpu=-0", "-0 would store 0, which means never decay"),
    ],
)
def test_refused_values_leave_the_stored_value_alone(entity, written, why):
    """Inputs that would store the reverse of what they say are refused.

    Every case here used to be accepted and stored 0 - never decay - which
    is the single most destructive value the field has. Each must fail, say
    something, and change nothing. doc/man/man1/sacctmgr.1's QOS section
    points back at these same refusals, so an account and a QOS are both
    exercised rather than trusting that the shared call site keeps them
    in sync.
    """
    name = _entity_name(entity)
    _set(entity, name, "cpu=3600")
    result = _set(entity, name, written)
    assert result["exit_code"] != 0, f"{written} should be refused: {why}"
    assert result["stderr"].strip(), f"{written} should say why it failed"
    assert _as_dict(_show(entity, name)) == {
        "cpu": "3600"
    }, f"a refused modify must leave the stored value alone ({written})"


@pytest.mark.parametrize("entity", ["account", "qos"])
def test_minus_one_removes(entity):
    """TRES=-1 removes an override, leaving the TRES on the global."""
    name = _entity_name(entity)
    _set(entity, name, "cpu=3600,mem=7200")
    _set(entity, name, "cpu=-1")
    got = _show(entity, name)
    assert _as_dict(got) == {"mem": "7200"}, f"only cpu should go, showed {got!r}"


def test_child_inherits_from_parent():
    """A child with no value of its own reports the parent's."""
    _set("account", acct, "cpu=0,mem=3600")
    atf.run_command(
        f"sacctmgr -i add account {child} parent={acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    got = _show("account", child)
    assert _as_dict(got) == {
        "cpu": "0",
        "mem": "3600",
    }, f"child should inherit both, showed {got!r}"


def test_child_value_wins_over_parent():
    """A child's own value beats the one it would inherit."""
    _set("account", acct, "cpu=3600,mem=3600")
    atf.run_command(
        f"sacctmgr -i add account {child} parent={acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _set("account", child, "cpu=0")
    got = _show("account", child)
    assert _as_dict(got) == {
        "cpu": "0",
        "mem": "3600",
    }, f"child's own cpu should win, mem should still be inherited: {got!r}"


def test_nearest_ancestor_wins():
    """With two levels above it, a child takes the nearer value per TRES.

    get_parent_limits walks up appending each level, and the merge keeps the
    first entry it sees per TRES, so this is what says the walk is ordered
    nearest-first rather than merely reaching the top.
    """
    _set("account", acct, "cpu=3600,mem=1800")
    atf.run_command(
        f"sacctmgr -i add account {child} parent={acct}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    _set("account", child, "cpu=7200")
    atf.run_command(
        f"sacctmgr -i add account {grandchild} parent={child}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    got = _show("account", grandchild)
    assert _as_dict(got) == {
        # cpu from the parent, which overrode the grandparent's 3600.
        "cpu": "7200",
        # mem was only ever set two levels up, so it still arrives.
        "mem": "1800",
    }, f"cpu should come from the parent and mem from the grandparent: {got!r}"


# A coordinator may not end up less constrictive than what it inherits, and
# for this field that is inverted from every other limit: a longer half-life
# replenishes the quota more slowly, and 0 never replenishes it at all, so a
# shorter one is what has to be refused.
def test_coordinator_may_lengthen(coordinator):
    """A longer half-life replenishes more slowly, so it is allowed."""
    _set("account", acct, "cpu=3600")
    result = _coord_set(child, "cpu=7200", coordinator)
    assert result["exit_code"] == 0, (
        f"lengthening is more constrictive and must be allowed: "
        f"{result['stderr']!r}"
    )
    assert _as_dict(_show("account", child)) == {
        "cpu": "7200"
    }, "the lengthened value should have been stored"


def test_coordinator_may_freeze(coordinator):
    """0 never replenishes, the most constrictive value there is."""
    _set("account", acct, "cpu=3600")
    result = _coord_set(child, "cpu=0", coordinator)
    assert result["exit_code"] == 0, (
        f"0 is the most constrictive value and must be allowed: "
        f"{result['stderr']!r}"
    )
    assert _as_dict(_show("account", child)) == {
        "cpu": "0"
    }, "the frozen value should have been stored"


def test_coordinator_may_not_shorten(coordinator):
    """A shorter half-life replenishes faster, so it is refused."""
    _set("account", acct, "cpu=3600")
    result = _coord_set(child, "cpu=1800", coordinator)
    assert result["exit_code"] != 0, "shortening should be refused"
    assert _as_dict(_show("account", child)) == {
        "cpu": "3600"
    }, "a refused modify must leave the inherited value in place"


def test_coordinator_may_not_unfreeze(coordinator):
    """Moving a TRES off never-decay replenishes faster, so it is refused."""
    _set("account", acct, "cpu=0")
    result = _coord_set(child, "cpu=3600", coordinator)
    assert result["exit_code"] != 0, "leaving never-decay should be refused"
    assert _as_dict(_show("account", child)) == {
        "cpu": "0"
    }, "a refused modify must leave the inherited value in place"


def test_coordinator_may_not_set_where_no_ancestor_has_one(coordinator):
    """With nothing above to compare against, there is no ceiling to check.

    What the TRES inherits then is PriorityDecayHalfLife, which slurmdbd does
    not have, so the value is refused rather than compared against something
    unknown.
    """
    _set("account", acct, "cpu=3600")
    result = _coord_set(child, "mem=1800", coordinator)
    assert result["exit_code"] != 0, "mem has no ancestor value, so refuse it"
    assert _as_dict(_show("account", child)) == {
        "cpu": "3600"
    }, "only the inherited cpu should remain"


def test_coordinator_may_not_remove(coordinator):
    """Removing an override falls back to whatever an ancestor set, which may
    be shorter (or unset), so a coordinator may not remove one either."""
    _set("account", acct, "cpu=3600")
    _set("account", child, "cpu=7200")
    result = _coord_set(child, "cpu=-1", coordinator)
    assert result["exit_code"] != 0, "removing should be refused"
    assert _as_dict(_show("account", child)) == {
        "cpu": "7200"
    }, "a refused modify must leave the override in place"


def test_qos_set_and_show():
    """A QOS carries the same value, with no parent to inherit from."""
    _set("qos", qos, "cpu=0,mem=1800")
    got = _show("qos", qos)
    assert _as_dict(got) == {"cpu": "0", "mem": "1800"}, f"showed {got!r}"


def test_dump_and_load_round_trip():
    """sacctmgr dump writes the value and load reads it back unchanged."""
    cluster = atf.get_config_parameter("ClusterName")
    dump = f"{atf.module_tmp_path}/tres_decay.dump"

    _set("account", acct, "cpu=0,mem=3600")
    atf.run_command(
        f"sacctmgr -i dump {cluster} file={dump}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Raw seconds in the file: no colon, which the flat file would split on,
    # and no unit suffix on mem, which 'sacctmgr load' cannot read back.
    atf.assert_file_contents(dump, "TresDecayHalfLife=cpu=0,mem=3600", contains=True)

    _set("account", acct, "cpu=-1,mem=-1")
    assert not _show("account", acct), "value should be cleared before reload"

    atf.run_command(
        f"sacctmgr -i load {dump}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    got = _show("account", acct)
    assert _as_dict(got) == {
        "cpu": "0",
        "mem": "3600",
    }, f"both should survive the round trip, showed {got!r}"
