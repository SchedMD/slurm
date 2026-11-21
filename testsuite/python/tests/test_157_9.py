# SPDX-License-Identifier: GPL-2.0-or-later
"""Preserve packed per-CPU memory exactly through real job_submit/lua hooks."""

import pytest

import atf

LUA = r"""
local packed_cpu_1025 = "9223372036854776833"

local function check_cpu(job)
    assert(type(job.pn_min_memory) == "string")
    assert(job.pn_min_memory == packed_cpu_1025)
    assert(job.min_mem_per_cpu == 1025)
    assert(job.min_mem_per_node == nil)
end

local function check_submit(job_desc)
    local command = job_desc.comment
    if command == "cpu" then
        check_cpu(job_desc)
        job_desc.pn_min_memory = job_desc.pn_min_memory
        check_cpu(job_desc)
    elseif command == "set_cpu" then
        job_desc.pn_min_memory = packed_cpu_1025
        check_cpu(job_desc)
    elseif command == "unset" then
        assert(job_desc.pn_min_memory == slurm.NO_VAL64)
        assert(job_desc.min_mem_per_node == nil)
        assert(job_desc.min_mem_per_cpu == nil)
        job_desc.pn_min_memory = job_desc.pn_min_memory
    elseif command == "zero" then
        assert(job_desc.pn_min_memory == 0)
        assert(job_desc.min_mem_per_node == 0)
        job_desc.pn_min_memory = job_desc.pn_min_memory
    elseif command == "boundaries" then
        local original = job_desc.pn_min_memory
        -- Use floating arithmetic even when Lua also has native integers.
        local float_boundary = 1.0
        while float_boundary < 9007199254740992.0 and
                float_boundary + 1.0 ~= float_boundary do
            float_boundary = float_boundary * 2.0
        end
        local max_safe = float_boundary - 1.0
        job_desc.pn_min_memory = max_safe
        assert(type(job_desc.pn_min_memory) == "number")
        assert(job_desc.pn_min_memory == max_safe)
        for _, value in ipairs({string.format("%.0f", float_boundary),
                "9007199254740992", "9007199254740993",
                "9223372036854775808", "18446744073709551613"}) do
            job_desc.pn_min_memory = value
            assert(type(job_desc.pn_min_memory) == "string")
            assert(job_desc.pn_min_memory == value)
        end
        -- Native Lua integers may be exact beyond the portable float range.
        if math.type and math.type(9007199254740993) == "integer" then
            job_desc.pn_min_memory = 9007199254740993
            assert(job_desc.pn_min_memory == "9007199254740993")
        end
        job_desc.pn_min_memory = slurm.INFINITE64
        assert(job_desc.pn_min_memory == -1)
        job_desc.pn_min_memory = slurm.NO_VAL64
        assert(job_desc.pn_min_memory == -2)
        job_desc.pn_min_memory = original
    elseif command == "invalid" then
        check_cpu(job_desc)
        local invalid = {true, {}, -3, 1.5, math.huge, 0/0,
            9007199254740992.0, "", "-3", " 1025", "+1025", "1.5",
            "0x401", "1025M", "18446744073709551616"}
        for _, value in ipairs(invalid) do
            local ok = pcall(function() job_desc.pn_min_memory = value end)
            assert(not ok, "invalid raw memory accepted: " .. tostring(value))
            check_cpu(job_desc)
        end
    end
    return command
end

local function check_modify(job_desc, job_rec)
    assert(job_desc.pn_min_memory == slurm.NO_VAL64)
    check_cpu(job_rec)
    if job_desc.comment == "copy_record" then
        job_desc.pn_min_memory = job_rec.pn_min_memory
        check_cpu(job_desc)
    else
        assert(job_desc.comment == "leave_unset")
        job_desc.pn_min_memory = slurm.NO_VAL64
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
        {"test1579": {"Nodes": "ALL", "Default": "YES", "DefMemPerNode": 64}},
    )
    atf.require_slurm_running()


def submit(command, memory=""):
    job_id = atf.submit_job_sbatch(
        f"--hold -N1 -n1 -t1 --partition=test1579 --comment={command} "
        f"{memory} --output=/dev/null --wrap=true",
        fatal=True,
    )
    assert atf.get_job_parameter(job_id, "Comment") == "ok:" + command
    return job_id


@pytest.mark.parametrize(
    "command,memory",
    [("cpu", "--mem-per-cpu=1025"), ("set_cpu", ""), ("invalid", "--mem-per-cpu=1025")],
)
def test_exact_per_cpu_roundtrip(command, memory):
    """Reading, writing and invalid writes preserve every packed memory bit."""
    job_id = submit(command, memory)
    assert atf.get_job_parameter(job_id, "MinMemoryCPU") == "1025M"
    assert atf.get_job_parameter(job_id, "MinMemoryNode") is None


@pytest.mark.parametrize("command", ["copy_record", "leave_unset"])
def test_modify_preserves_record_memory(command):
    """Existing job records expose the exact raw value to the modify hook."""
    job_id = submit("cpu", "--mem-per-cpu=1025")
    atf.run_command(f"scontrol update JobId={job_id} Comment={command}", fatal=True)
    assert atf.get_job_parameter(job_id, "Comment") == "ok:" + command
    assert atf.get_job_parameter(job_id, "MinMemoryCPU") == "1025M"


@pytest.mark.parametrize(
    "command,memory,expected",
    [("unset", "", "64"), ("zero", "--mem=0", "0"), ("boundaries", "", "64")],
)
def test_sentinels_zero_and_exact_boundaries(command, memory, expected):
    """Temporary high values are restored before native admission checks."""
    job_id = submit(command, memory)
    assert str(atf.get_job_parameter(job_id, "MinMemoryNode")).rstrip("M") == expected
