# SPDX-License-Identifier: GPL-2.0-or-later
"""Read live association-cache snapshots from both job_submit/lua hooks.

All jobs are held. Accounting updates are observed by repeatedly modifying one
job, without reconfiguring the controller or reloading the Lua script. ATF's
auto-config lifecycle restores the original accounting database and config.
"""

import pwd
import shlex

import pytest

import atf

BASE = "test1577base"
PARENT = "test1577parent"
LEAF = "test1577leaf"
OTHER_PARENT = "test1577other"
PARTITION = "test1577part"

LUA = r"""
local saved

local function encode(rows)
    local output = {}
    for _, row in ipairs(rows) do
        assert(type(row.id) == "number")
        assert(type(row.parent_id) == "number")
        assert(type(row.account) == "string")
        assert(type(row.shares_raw) == "number")
        if row.user ~= nil then
            assert(type(row.user) == "string")
            assert(type(row.uid) == "number")
        else
            assert(row.uid == nil)
        end
        output[#output + 1] = table.concat({
            string.format("%.0f", row.id),
            string.format("%.0f", row.parent_id),
            row.account, row.user or "-",
            row.uid ~= nil and string.format("%.0f", row.uid) or "-",
            row.partition or "-", string.format("%.0f", row.shares_raw)}, "|")
    end
    table.sort(output)
    return #output == 0 and "EMPTY" or table.concat(output, ";")
end

local function snapshot(options)
    local rows, err = slurm.get_associations(options)
    assert(type(rows) == "table", tostring(err))
    assert(err == nil)
    return rows
end

local function query(command)
    if command == "all" then
        local result, err = slurm.get_associations()
        assert(type(result) == "table", tostring(err))
        assert(err == nil)
        return encode(result)
    elseif command == "nil" then
        return encode(snapshot(nil))
    elseif command == "empty_options" then
        return encode(snapshot({}))
    elseif command == "users" then
        return encode(snapshot({include_accounts = false}))
    elseif command == "save" then
        saved = snapshot()
        return encode(saved)
    elseif command == "saved" then
        assert(saved ~= nil, "no saved snapshot")
        return encode(saved)
    elseif command == "mutate_copy" then
        local first = snapshot()
        local before = encode(first)
        assert(#first > 0)
        first[1].account = "locally_changed"
        first[1].shares_raw = 123456
        table.remove(first, #first)
        assert(encode(snapshot()) == before,
            "editing a Lua snapshot changed the controller cache")
        return "OK"
    elseif command == "invalid" then
        local invalid = {
            false, 7, "options", {uid = -1}, {uid = 1.5},
            {uid = "1"}, {uid = true}, {uid = slurm.NO_VAL},
            {uid = math.huge}, {uid = 0/0},
            {include_accounts = 1}, {include_accounts = "true"},
            {unknown = true}, {[1] = true}
        }
        for _, options in ipairs(invalid) do
            local ok = pcall(slurm.get_associations, options)
            assert(not ok, "invalid options were accepted")
        end
        assert(type(slurm.FS_USE_PARENT) == "number")
        return "OK"
    elseif command == "fs_use_parent" then
        return string.format("%.0f", slurm.FS_USE_PARENT)
    end
    local mode, uid = string.match(command, "^(%a+):(%d+)$")
    assert(mode == "uid" or mode == "user", "unknown test command")
    return encode(snapshot({uid = tonumber(uid),
        include_accounts = mode == "uid"}))
end

local function handle(job_desc)
    local command = string.match(job_desc.comment or "", "^assoc:(.+)$")
    if command ~= nil then
        -- Capture assertion failures in a job field: otherwise a Lua runtime
        -- error can be logged without failing the user's submit/update RPC.
        local ok, result = pcall(query, command)
        job_desc.comment = (ok and "assoc_result:" or "assoc_error:") ..
            string.gsub(tostring(result), "%s", "_")
    end
    return slurm.SUCCESS
end

function slurm_job_submit(job_desc, part_list, submit_uid)
    return handle(job_desc)
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    return handle(job_desc)
end

return slurm.SUCCESS
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("creates and removes accounting associations")
    atf.require_build_config("HAVE_LUA")
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", LUA)
    atf.require_config_parameter(
        "PartitionName", {PARTITION: {"Nodes": "ALL", "Default": "YES"}}
    )
    atf.require_slurm_running()


def db(*arguments):
    return atf.run_command_output(
        shlex.join(["sacctmgr", "-i", *arguments]),
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def accounts(setup, module_setup):
    cluster = atf.get_config_parameter("ClusterName")
    user = atf.properties["test-user"]
    uid = pwd.getpwnam(user).pw_uid
    other_user = next(
        name
        for name in ("root", "nobody", "daemon")
        if name != user and _user_exists(name)
    )
    other_uid = pwd.getpwnam(other_user).pw_uid
    db("add", "account", BASE, f"Cluster={cluster}", "Parent=root")
    db("add", "user", user, f"Cluster={cluster}", f"Account={BASE}")
    # Wait outside the Lua hook for the initial DBD push before submitting a
    # job charged to BASE. Subsequent updates are observed through Lua itself.
    assert atf.repeat_until(
        lambda: atf.run_command_output(
            shlex.join(["sshare", "-hP", f"--users={user}", "--format=Account,User"]),
            fatal=True,
        ),
        lambda output: any(
            [field.strip() for field in line.split("|")] == [BASE, user]
            for line in output.splitlines()
        ),
        timeout=30,
    )
    return {
        "cluster": cluster,
        "user": user,
        "uid": uid,
        "other_user": other_user,
        "other_uid": other_uid,
    }


def _user_exists(name):
    try:
        pwd.getpwnam(name)
    except KeyError:
        return False
    return True


def query(command, job_id=None):
    """Return the result of a real submit or modify hook invocation."""
    comment = shlex.quote("assoc:" + command)
    if job_id is None:
        job_id = atf.submit_job_sbatch(
            f"--hold -n1 -t1 --account={BASE} --partition={PARTITION} "
            f"--comment={comment} --output=/dev/null --wrap=true",
            fatal=True,
        )
    else:
        atf.run_command(f"scontrol update JobId={job_id} Comment={comment}", fatal=True)
    result = atf.get_job_parameter(job_id, "Comment", fatal=True)
    assert result.startswith("assoc_result:"), result
    return result.removeprefix("assoc_result:"), job_id


def rows(command="all", job_id=None):
    value, _ = query(command, job_id)
    if value == "EMPTY":
        return []
    result = []
    for line in value.split(";"):
        fields = line.split("|")
        assert len(fields) == 7, line
        result.append(
            dict(
                zip(
                    (
                        "id",
                        "parent_id",
                        "account",
                        "user",
                        "uid",
                        "partition",
                        "shares_raw",
                    ),
                    [
                        int(fields[0]),
                        int(fields[1]),
                        fields[2],
                        None if fields[3] == "-" else fields[3],
                        None if fields[4] == "-" else int(fields[4]),
                        None if fields[5] == "-" else fields[5],
                        int(fields[6]),
                    ],
                )
            )
        )
    return result


def wait_rows(job_id, condition):
    observed = []

    def read():
        nonlocal observed
        observed = rows(job_id=job_id)
        return observed

    assert atf.repeat_until(read, condition, timeout=30, poll_interval=0.2), observed
    return observed


def select(records, account, user=None, partition=None):
    return [
        r
        for r in records
        if r["account"] == account and r["user"] == user and r["partition"] == partition
    ]


def test_options_and_independent_snapshots(accounts):
    """Validate strict options and Lua ownership of returned tables."""
    _, job_id = query("all")
    wait_rows(job_id, lambda r: bool(select(r, BASE, accounts["user"])))
    expected, _ = query("all", job_id)
    assert query("nil", job_id)[0] == expected
    assert query("empty_options", job_id)[0] == expected
    assert query("all")[0] == expected, "Submit and modify must see the same cache"
    assert query("invalid", job_id)[0] == "OK"
    assert query("mutate_copy", job_id)[0] == "OK"
    assert query("all", job_id)[0] == expected


def test_dynamic_associations_and_retained_snapshot(accounts):
    """DB pushes update the next snapshot but never change a retained one."""
    cluster, user = accounts["cluster"], accounts["user"]
    _, job_id = query("all")
    wait_rows(job_id, lambda r: bool(select(r, BASE, user)))
    original, _ = query("save", job_id)
    for name in (PARENT, OTHER_PARENT):
        db("add", "account", name, f"Cluster={cluster}", "Parent=root", "Fairshare=10")
    db("add", "account", LEAF, f"Cluster={cluster}", f"Parent={PARENT}", "Fairshare=7")
    db("add", "user", user, f"Cluster={cluster}", f"Account={LEAF}", "Fairshare=3")
    db(
        "add",
        "user",
        user,
        f"Cluster={cluster}",
        f"Account={LEAF}",
        f"Partition={PARTITION}",
        "Fairshare=parent",
    )
    db(
        "add",
        "user",
        accounts["other_user"],
        f"Cluster={cluster}",
        f"Account={LEAF}",
        "Fairshare=5",
    )
    current = wait_rows(
        job_id, lambda r: len([x for x in r if x["account"] == LEAF]) == 4
    )
    parent = select(current, PARENT)[0]
    leaf = select(current, LEAF)[0]
    assert leaf["parent_id"] == parent["id"]
    assert leaf["shares_raw"] == 7
    assert select(current, LEAF, user)[0]["parent_id"] == leaf["id"]
    assert select(current, LEAF, user)[0]["shares_raw"] == 3
    inherited = select(current, LEAF, user, PARTITION)[0]
    assert inherited["parent_id"] == leaf["id"]
    assert inherited["shares_raw"] == int(query("fs_use_parent", job_id)[0])

    uid_rows = rows(f"uid:{accounts['uid']}", job_id)
    assert {r["id"] for r in uid_rows if r["user"] is None} == {
        r["id"] for r in current if r["user"] is None
    }, "Account rows must not be filtered by UID"
    assert all(r["uid"] == accounts["uid"] for r in uid_rows if r["user"] is not None)
    assert len(select(uid_rows, LEAF, user)) == 1
    assert len(select(uid_rows, LEAF, user, PARTITION)) == 1
    only_users = rows(f"user:{accounts['uid']}", job_id)
    assert only_users and all(r["user"] == user for r in only_users)
    all_users = rows("users", job_id)
    assert all(r["user"] is not None for r in all_users)
    assert any(r["uid"] == accounts["other_uid"] for r in all_users)
    unknown_uid = next(
        uid for uid in range(123456, 124456) if all(r["uid"] != uid for r in current)
    )
    assert rows(f"user:{unknown_uid}", job_id) == []
    assert all(r["user"] is None for r in rows(f"uid:{unknown_uid}", job_id))
    assert query("saved", job_id)[0] == original
    # Retain the new records too: they will be modified, reparented and freed
    # from the controller's cache while this Lua table remains reachable.
    retained, _ = query("save", job_id)

    db(
        "modify",
        "user",
        "where",
        f"Name={user}",
        f"Cluster={cluster}",
        f"Account={LEAF}",
        "set",
        "Fairshare=0",
    )
    wait_rows(
        job_id,
        lambda r: len(
            [
                x
                for x in r
                if x["account"] == LEAF and x["user"] == user and x["shares_raw"] == 0
            ]
        )
        == 2,
    )
    db(
        "modify",
        "account",
        "where",
        f"Name={LEAF}",
        f"Cluster={cluster}",
        "set",
        f"Parent={OTHER_PARENT}",
        "Fairshare=11",
    )
    changed = wait_rows(
        job_id, lambda r: any(x["shares_raw"] == 11 for x in select(r, LEAF))
    )
    assert (
        select(changed, LEAF)[0]["parent_id"] == select(changed, OTHER_PARENT)[0]["id"]
    )
    assert query("saved", job_id)[0] == retained

    db(
        "delete",
        "user",
        "where",
        f"Name={user}",
        f"Cluster={cluster}",
        f"Account={LEAF}",
    )
    wait_rows(
        job_id, lambda r: not any(x["account"] == LEAF and x["user"] == user for x in r)
    )
    db(
        "delete",
        "user",
        "where",
        f"Name={accounts['other_user']}",
        f"Cluster={cluster}",
        f"Account={LEAF}",
    )
    db("delete", "account", "where", f"Name={LEAF}", f"Cluster={cluster}")
    wait_rows(job_id, lambda r: not any(x["account"] == LEAF for x in r))
    assert query("saved", job_id)[0] == retained
