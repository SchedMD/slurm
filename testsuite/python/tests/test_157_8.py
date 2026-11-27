# SPDX-License-Identifier: GPL-2.0-or-later
"""Distinguish uint64 sentinels through real submit and modify Lua hooks."""

import pytest

import atf

LUA = r"""
local function check_constants()
    assert(type(slurm.NO_VAL64) == "number")
    assert(type(slurm.INFINITE64) == "number")
    assert(slurm.NO_VAL64 == -2)
    assert(slurm.INFINITE64 == -1)
    assert(slurm.NO_VAL64 ~= slurm.INFINITE64)
end

local function check_submit(job_desc)
    check_constants()
    local command = job_desc.comment
    if command == "zero" then
        assert(job_desc.pn_min_memory == 0)
        assert(job_desc.min_mem_per_node == 0)
        assert(job_desc.min_mem_per_cpu == nil)
    elseif command == "unset" or command == "sentinels" then
        assert(job_desc.pn_min_memory == slurm.NO_VAL64)
        assert(job_desc.min_mem_per_node == nil)
        assert(job_desc.min_mem_per_cpu == nil)
        if command == "sentinels" then
            job_desc.pn_min_memory = slurm.INFINITE64
            assert(job_desc.pn_min_memory == slurm.INFINITE64)
            assert(job_desc.pn_min_memory ~= slurm.NO_VAL64)
            job_desc.pn_min_memory = slurm.NO_VAL64
        end
    elseif command == "node" then
        assert(job_desc.pn_min_memory == 1025)
        assert(job_desc.min_mem_per_node == 1025)
    end
    job_desc.pn_min_memory = job_desc.pn_min_memory
    return command
end

local function check_modify(job_desc, job_rec)
    check_constants()
    assert(job_desc.pn_min_memory == slurm.NO_VAL64)
    assert(job_desc.min_mem_per_node == nil)
    assert(job_desc.min_mem_per_cpu == nil)
    if job_desc.comment == "modify_unset" then
        assert(job_rec.pn_min_memory == 64)
        assert(job_rec.min_mem_per_node == 64)
        job_desc.pn_min_memory = slurm.NO_VAL64
    elseif job_desc.comment == "copy_record" then
        assert(job_rec.pn_min_memory == 1025)
        assert(job_rec.min_mem_per_node == 1025)
        job_desc.pn_min_memory = job_rec.pn_min_memory
        assert(job_desc.min_mem_per_node == 1025)
    end
    return job_desc.comment
end

function slurm_job_submit(job_desc, part_list, submit_uid)
    local ok, result = pcall(check_submit, job_desc)
    job_desc.comment = (ok and "ok:" or "error:") .. tostring(result)
    return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    local ok, result = pcall(check_modify, job_desc, job_rec)
    job_desc.comment = (ok and "ok:" or "error:") .. tostring(result)
    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("installs a Lua hook and known memory defaults")
    atf.require_build_config("HAVE_LUA")
    atf.require_accounting(enabled=False)
    atf.require_config_parameter("AccountingStorageEnforce", None)
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", LUA)
    atf.require_nodes(1, [("RealMemory", 4096)])
    atf.require_config_parameter(
        "PartitionName",
        {"test1578": {"Nodes": "ALL", "Default": "YES", "DefMemPerNode": 64}},
    )
    atf.require_slurm_running()


def submit(command, memory=""):
    job_id = atf.submit_job_sbatch(
        f"--hold -N1 -n1 -t1 --partition=test1578 --comment={command} "
        f"{memory} --output=/dev/null --wrap=true",
        fatal=True,
    )
    assert atf.get_job_parameter(job_id, "Comment") == "ok:" + command
    return job_id


def modify(job_id, command):
    atf.run_command(f"scontrol update JobId={job_id} Comment={command}", fatal=True)
    assert atf.get_job_parameter(job_id, "Comment") == "ok:" + command


@pytest.mark.parametrize(
    "command,memory,expected",
    [("unset", "", "64"), ("zero", "--mem=0", "0"), ("sentinels", "", "64")],
)
def test_submit_sentinels_and_zero(command, memory, expected):
    """Unset round-trips to the partition default; explicit zero stays zero."""
    job_id = submit(command, memory)
    assert str(atf.get_job_parameter(job_id, "MinMemoryNode")).rstrip("M") == expected


def test_modify_unset_does_not_clear_memory():
    """An update's unset memory remains distinct from a request for zero."""
    job_id = submit("unset")
    modify(job_id, "modify_unset")
    assert atf.get_job_parameter(job_id, "MinMemoryNode") == "64M"


def test_modify_reads_and_copies_job_record():
    """The record getter and descriptor setter agree on an ordinary value."""
    job_id = submit("node", "--mem=1025")
    modify(job_id, "copy_record")
    assert atf.get_job_parameter(job_id, "MinMemoryNode") == "1025M"
