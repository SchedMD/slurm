############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test adding an account whose long name shares its first 42 characters.

Ticket 26066: the association table's unique key covers only the first 42
characters of the account name. Adding a second account whose name matches
an existing one in those 42 characters updates the existing account's
association in place. Its acct becomes the new name, while its children keep
the old name as their parent. The original account then has no association.
Any association read that includes parent limits repeats its lookup for the
old name forever: a running slurmdbd stops answering that request, and a
restarted slurmdbd never accepts connections.

The grandchild is load-bearing for the hang. The parent-limit walk only
repeats forever after it has read a row whose parent is the old name. That
row is the child, and only the grandchild's lookup walks through it.

The collision add is rejected, so the tests that need a parent without an
association write that state directly. Teardown renames the overwritten
association back, then kills any connection still running a query. A lookup
that is already looping reads from the snapshot its transaction started with,
so it never sees the rename.
"""

import pytest

import atf

CLUSTER = "test10226"
OTHER_CLUSTER = "test10226b"
FIRST_DESCRIPTION = "firstdescription"
FIRST_ORGANIZATION = "firstorganization"
PARENT = "test10226parent"
SHARED = "test10226-shared-account-name-prefix-".ljust(42, "x")
FIRST = f"{SHARED}-first"
SECOND = f"{SHARED}-second"
CHILD = "test10226child"
GRANDCHILD = "test10226grandchild"
ASSOC_TABLE = f"{CLUSTER}_assoc_table"

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config(
        "Ticket 26066: restarts slurmdbd and repairs the association table"
    )
    atf.require_version(
        (26, 5, 6),
        component="sbin/slurmdbd",
        reason="Ticket 26066: slurmdbd rejects names sharing the key prefix",
    )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def mysql(setup, sql_statement_repeat):
    if not sql_statement_repeat:
        pytest.skip(
            "Ticket 26066: teardown needs the mysql helper, which requires auto-config"
        )
    return sql_statement_repeat


@pytest.fixture(scope="function")
def hierarchy(mysql):
    """Build parent > first > child > grandchild on a new cluster."""
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"sacctmgr -i add cluster {CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    first_id = None
    try:
        _add_account(PARENT, "root")
        _add_account(FIRST, PARENT, FIRST_DESCRIPTION, FIRST_ORGANIZATION)
        _add_account(CHILD, FIRST)
        _add_account(GRANDCHILD, CHILD)

        first_id = _assoc_ids().get(FIRST)
        if first_id is None:
            pytest.fail(f"{FIRST} has no association on {CLUSTER} before the add")

        yield first_id
    finally:
        if first_id is not None:
            _restore_first_name(mysql, first_id)
        _recover_slurmdbd(mysql)
        atf.run_command(
            f"sacctmgr -i remove cluster {CLUSTER}",
            user=slurm_user,
            fatal=True,
            timeout=120,
        )
        # Accounts are not cluster-scoped, so removing the cluster leaves
        # them in the account table. The second name only has its own entry
        # there if the add created a separate account.
        for account in (GRANDCHILD, CHILD, FIRST, PARENT, SECOND):
            atf.run_command(
                f"sacctmgr -i remove account {account}",
                user=slurm_user,
            )


@pytest.fixture(scope="function")
def shared_prefix(hierarchy):
    """Add the second name, whose key collides with the first."""
    second_add = atf.run_command(
        f"sacctmgr -i add account {SECOND} cluster={CLUSTER} parent={PARENT}",
        user=atf.properties["slurm-user"],
    )
    return {"first_id": hierarchy, "second_add": second_add}


@pytest.fixture(scope="function")
def dangling_parent(hierarchy, mysql):
    """Leave the first account's children with a parent that has no assoc.

    The collision itself is rejected, so the damage it used to cause is
    written directly: the first association is renamed to the second name.
    """
    _sql(
        mysql,
        f"UPDATE {ASSOC_TABLE} "
        f"SET acct='{SECOND}', lineage=REPLACE(lineage, '{FIRST}', '{SECOND}') "
        f"WHERE id_assoc={hierarchy} AND acct='{FIRST}'",
    )
    by_id = {assoc_id: account for assoc_id, account, _ in _account_assocs()}
    if by_id.get(hierarchy) != SECOND:
        pytest.fail(f"could not rename association {hierarchy} to {SECOND}")
    return hierarchy


@pytest.fixture(scope="function")
def other_cluster(hierarchy):
    """Add a cluster on which the first account has no association."""
    slurm_user = atf.properties["slurm-user"]
    atf.run_command(
        f"sacctmgr -i add cluster {OTHER_CLUSTER}",
        user=slurm_user,
        fatal=True,
    )
    yield
    atf.run_command(
        f"sacctmgr -i remove cluster {OTHER_CLUSTER}",
        user=slurm_user,
        fatal=True,
        timeout=120,
    )


def _add_account(account, parent, description="", organization=""):
    options = ""
    if description:
        options += f" description={description}"
    if organization:
        options += f" organization={organization}"
    atf.run_command(
        f"sacctmgr -i add account {account} cluster={CLUSTER} parent={parent}"
        f"{options}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def _account_assocs():
    """Return (id, account, parent) for each account-level association.

    WOPLimits keeps the read from walking parent limits, so it answers even
    when that walk would never finish.
    """
    out = atf.run_command_output(
        f"sacctmgr -nP show assoc WOPLimits where cluster={CLUSTER} "
        "format=id,account,user,parentname",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assocs = []
    for line in out.splitlines():
        fields = line.split("|")
        if len(fields) < 4 or fields[2]:
            continue
        assocs.append((int(fields[0]), fields[1], fields[3]))
    return assocs


def _assoc_ids():
    return {account: assoc_id for assoc_id, account, _ in _account_assocs()}


def _sql(mysql, statement, fatal=True):
    return atf.run_command(
        f"{mysql} --batch --skip-column-names",
        input=statement,
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=True,
    )


def _slurmdbd_exe():
    return f"{atf.properties['slurm-sbin-dir']}/slurmdbd"


def _slurmdbd_pids():
    return atf.pids_from_exe(_slurmdbd_exe())


def _dbd_responsive():
    return (
        atf.run_command_exit(
            "sacctmgr show cluster",
            user=atf.properties["slurm-user"],
            quiet=True,
            timeout=5,
        )
        == 0
    )


def _first_name_lookup(mysql):
    """Text of a running parent-limit lookup for the first account name."""
    statement = (
        "SELECT INFO FROM information_schema.PROCESSLIST "
        f"WHERE INFO LIKE '%acct = ''{FIRST}''%' "
        "AND INFO NOT LIKE '%PROCESSLIST%'"
    )
    return _sql(mysql, statement, fatal=False)["stdout"].strip()


def _restore_first_name(mysql, first_id):
    """Rename the overwritten association back so later lookups can finish."""
    _sql(
        mysql,
        f"UPDATE {ASSOC_TABLE} "
        f"SET acct='{FIRST}', lineage=REPLACE(lineage, '{SECOND}', '{FIRST}') "
        f"WHERE id_assoc={first_id} AND acct='{SECOND}'",
        fatal=False,
    )


def _stop_slurmdbd_process():
    pids = _slurmdbd_pids()
    if not pids:
        return
    atf.run_command(
        "kill " + " ".join(str(pid) for pid in pids),
        user="root",
        quiet=True,
    )
    stopped = atf.repeat_until(
        _slurmdbd_pids,
        lambda found: not found,
        timeout=15,
    )
    if stopped:
        return
    pids = _slurmdbd_pids()
    if pids:
        atf.run_command(
            "kill -9 " + " ".join(str(pid) for pid in pids),
            user="root",
            quiet=True,
        )


def _running_query_ids(mysql):
    """Ids of the other accounting database connections running a query."""
    out = _sql(
        mysql,
        "SELECT ID FROM information_schema.PROCESSLIST "
        "WHERE COMMAND = 'Query' AND DB = DATABASE() AND ID != CONNECTION_ID()",
        fatal=False,
    )["stdout"]
    return [line.strip() for line in out.splitlines() if line.strip()]


def _kill_running_queries(mysql):
    """Kill connections running a query until none are left.

    A looping lookup keeps its transaction open. That blocks the DROP TABLE
    that removing the cluster runs, and stops slurmdbd from shutting down.
    """

    def _kill():
        ids = _running_query_ids(mysql)
        for conn_id in ids:
            _sql(mysql, f"KILL {conn_id}", fatal=False)
        return ids

    return atf.repeat_until(
        _kill,
        lambda killed: not killed,
        timeout=atf.default_polling_timeout,
    )


def _recover_slurmdbd(mysql):
    """Leave slurmdbd answering with no lookup still looping.

    Killing a looping startup load can make slurmdbd exit, so restart it when
    it does not answer.
    """
    _kill_running_queries(mysql)
    if atf.repeat_until(
        _dbd_responsive,
        lambda responsive: responsive,
        timeout=atf.default_polling_timeout,
    ):
        return
    _stop_slurmdbd_process()
    atf.start_slurmdbd(quiet=True)


def _launch_slurmdbd():
    results = atf.run_command(
        _slurmdbd_exe(),
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    if results["exit_code"] != 0:
        pytest.fail(
            "unable to start slurmdbd "
            f"(rc={results['exit_code']}): {results['stderr']}"
        )


def test_shared_prefix_add_keeps_existing_account(shared_prefix):
    """Adding the second name is rejected and leaves the first account intact.

    The first account's association must still be listed under its own name,
    and every account-level association must name a parent that is listed.
    """
    first_id = shared_prefix["first_id"]
    assocs = _account_assocs()
    by_id = {assoc_id: account for assoc_id, account, _ in assocs}
    accounts = set(by_id.values())
    dangling = sorted(
        {parent for _, _, parent in assocs if parent and parent not in accounts}
    )

    assert (
        shared_prefix["second_add"]["exit_code"] != 0
    ), f"adding {SECOND} should be rejected"
    assert SECOND not in accounts, f"{SECOND} should not be listed"
    assert by_id.get(first_id) == FIRST, (
        f"after adding {SECOND}, association {first_id} should still be "
        f"{FIRST}, got {by_id.get(first_id)!r}"
    )
    assert (
        not dangling
    ), f"after adding {SECOND}, these parents have no association: {dangling}"


def test_shared_prefix_add_keeps_account_description(hierarchy, other_cluster):
    """Adding the second name leaves the first account's own row unchanged.

    The first account has no association on the other cluster, so only the
    account table can reject the add. An add that is not rejected overwrites
    the description and organization of the first account.
    """
    second_add = atf.run_command(
        f"sacctmgr -i add account {SECOND} cluster={OTHER_CLUSTER} parent=root "
        "description=seconddescription organization=secondorganization",
        user=atf.properties["slurm-user"],
    )
    out = atf.run_command_output(
        f"sacctmgr -nP show account where name={FIRST} "
        "format=description,organization",
        user=atf.properties["slurm-user"],
        fatal=True,
    ).strip()

    assert (
        second_add["exit_code"] != 0
    ), f"adding {SECOND} on {OTHER_CLUSTER} should be rejected"
    assert out == f"{FIRST_DESCRIPTION}|{FIRST_ORGANIZATION}", (
        f"after adding {SECOND} on {OTHER_CLUSTER}, {FIRST} should keep its "
        f"description and organization, got {out!r}"
    )


def test_running_slurmdbd_lists_assocs_with_dangling_parent(dangling_parent, mysql):
    """A running slurmdbd answers an association list that walks parent limits.

    Without WOPLimits, the list reads each association's parent limits by
    walking the parent chain by name.
    """
    results = atf.run_command(
        f"sacctmgr -nP show assoc where cluster={CLUSTER} format=id,account",
        user=atf.properties["slurm-user"],
    )
    lookup = _first_name_lookup(mysql)

    assert results["exit_code"] == 0, (
        f"sacctmgr show assoc did not answer with {FIRST} unlisted "
        f"(rc={results['exit_code']}); processlist={lookup!r}"
    )


def test_slurmdbd_restarts_with_dangling_parent(dangling_parent, mysql):
    """slurmdbd accepts connections again with a parent that has no assoc.

    The restart runs the startup association load, which walks each
    association's parent limits by name.
    """
    failures = atf.stop_slurmdbd(quiet=True)
    if failures or _slurmdbd_pids():
        pytest.fail(f"could not stop slurmdbd before the restart: {failures}")

    seen = {"lookup": False, "info": ""}
    _launch_slurmdbd()

    def _observe():
        info = _first_name_lookup(mysql)
        seen["info"] = info
        if info:
            seen["lookup"] = True
        return _dbd_responsive()

    ready = atf.repeat_until(
        _observe,
        lambda is_ready: is_ready,
        timeout=atf.default_polling_timeout,
    )
    assert ready, (
        f"slurmdbd did not accept connections with {FIRST} unlisted; "
        f"lookup_seen={seen['lookup']}; pids={_slurmdbd_pids()}; "
        f"processlist={seen['info']!r}"
    )
