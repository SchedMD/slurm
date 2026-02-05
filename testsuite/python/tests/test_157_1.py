############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

# Global variables
qos1 = {"name": "qos1", "prio": 1}
qos2 = {"name": "qos2", "prio": 2}
qos3 = {"name": "qos3", "prio": 3}

part1 = "pa"
part2 = "pb"

acct = "acct"

comment = {
    None: "no partition",
    part1: f"comment of {part1}",
    part2: f"comment of {part2}",
}

job_submit_lua = """
function slurm_job_modify(job_desc, part_list, submit_uid)
    return slurm.SUCCESS
end

function slurm_job_submit(job_desc, part_list, submit_uid)
    assoc_qos = job_desc["assoc_qos"]
    assoc_comment = job_desc["assoc_comment"]

    if assoc_qos ~= nil then
        slurm.log_user("assoc_qos: %s", assoc_qos)

        for qos in string.gmatch(assoc_qos, '([^,]+)') do
            slurm.log_user("%s priority is %d", qos, slurm.get_qos_priority(qos))
        end
    else
        slurm.log_user("No assoc_qos")
    end

    if assoc_comment ~= nil then
        slurm.log_user("assoc_comment: %s", assoc_comment)
    end

    return slurm.SUCCESS
end

return slurm.SUCCESS
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", job_submit_lua)
    atf.require_config_parameter(
        "PartitionName",
        {
            "debug": {"Nodes": "ALL", "Default": "YES"},
            part1: {"Nodes": "ALL"},
            part2: {"Nodes": "ALL"},
        },
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db(module_setup):
    def _sacctmgr_add(opts):
        atf.run_command(
            f"sacctmgr -i add {opts}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )

    _sacctmgr_add(f"qos {qos1['name']} Priority={qos1['prio']}")
    _sacctmgr_add(f"qos {qos2['name']} Priority={qos2['prio']}")
    _sacctmgr_add(f"qos {qos3['name']} Priority={qos3['prio']}")

    _sacctmgr_add(f"account {acct}")

    user = atf.get_user_name()
    qos1_3 = qos1["name"] + "," + qos3["name"]
    _sacctmgr_add(f"user {user} Account={acct} qos={qos1_3} Comment='{comment[None]}'")
    _sacctmgr_add(
        f"user {user} Account={acct} qos={qos1_3} Comment='{comment[part1]}' Partition={part1}",
    )
    _sacctmgr_add(
        f"user {user} Account={acct} qos={qos1_3} Comment='{comment[part2]}' Partition={part2}",
    )


def test_assoc_qos():
    """Test that submitting a job reports the correct assoc QOS info"""

    # Submit a job
    output = atf.run_command_error("srun true", fatal=True)
    assert f"assoc_qos: {qos1['name']},{qos3['name']}" in output
    assert f"{qos1['name']} priority is {qos1['prio']}" in output
    assert f"{qos3['name']} priority is {qos3['prio']}" in output

    # Check for slurmctld crash (bug 23497)
    if atf.get_version() >= (25, 5, 3):
        output = atf.run_command_error("srun --account=__not_an_acct true", fatal=True)
        assert "No assoc_qos" in output


def test_assoc_comment():
    """Test that submitting a job reports the correct assoc comment"""

    # Submit a job
    output = atf.run_command_error("srun true", fatal=True)
    assert comment[None] in output

    output = atf.run_command_error(f"srun --partition={part1} true", fatal=True)
    assert comment[part1] in output

    output = atf.run_command_error(f"srun --partition={part2} true", fatal=True)
    assert comment[part2] in output
