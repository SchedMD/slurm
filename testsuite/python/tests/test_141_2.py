############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

max_nodes = 5

global_option_str = "GLOBAL"
infinite_option_str = "INFINITE"
infinite_option_num = 4294967295

g_suspend_timeout = 101
g_resume_timeout = 102

p1_suspend_time = 103
p1_suspend_timeout = 104
p1_resume_timeout = 105

p2_suspend_time = 106
p2_suspend_timeout = 107
p2_resume_timeout = 108

n_suspend_time = 109


def suspend_time_val(value):
    """Build expected suspend_time no_val_struct dict for node comparison."""
    if value == infinite_option_num:
        return {"set": False, "infinite": True, "number": 0}
    return {"set": True, "infinite": False, "number": value}


@pytest.fixture
def setup(request):
    atf.require_nodes(1, [("CPUs", 4), ("RealMemory", 40)])
    atf.require_config_parameter("ReconfigFlags", "KeepPartInfo,KeepPartState")
    atf.require_config_parameter("MaxNodeCount", max_nodes)
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", request.param)
    atf.require_config_parameter("SuspendTimeout", g_suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", g_resume_timeout)

    atf.require_config_parameter(
        "PartitionName",
        {
            "all": {"Nodes": "ALL"},
            "p1": {
                "SuspendTime": p1_suspend_time,
                "SuspendTimeout": p1_suspend_timeout,
                "ResumeTimeout": p1_resume_timeout,
            },
            "p2": {
                "SuspendTime": p2_suspend_time,
                "SuspendTimeout": p2_suspend_timeout,
                "ResumeTimeout": p2_resume_timeout,
            },
            "p3": {
                "SuspendTime": "INFINITE",
            },
        },
    )

    # Needed for MaxNodeCount
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")

    atf.require_slurm_running()

    yield request.param

    atf.run_command(
        "scontrol delete nodename=d1",
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        "scontrol delete nodename=d2",
        user=atf.properties["slurm-user"],
    )

    atf.set_config_parameter("ReconfigFlags", "None")


@pytest.mark.skipif(
    atf.get_version() < (26, 5), reason="Per-node SuspendTime added in 26.05"
)
@pytest.mark.parametrize("setup", [100, "INFINITE"], indirect=True)
def test_cloud_options(setup):
    g_suspend_time = setup

    atf.run_command(
        "scontrol create nodename=d1 state=cloud", user=atf.properties["slurm-user"]
    )
    atf.run_command(
        f"scontrol create nodename=d2 state=cloud SuspendTime={n_suspend_time}",
        user=atf.properties["slurm-user"],
    )

    if g_suspend_time == "INFINITE":
        assert atf.get_config_parameter("SuspendTime") == str(g_suspend_time).lower()
        # convert g_suspend_time to int from now on because the json output
        # reports the number and not the string.
        g_suspend_time = infinite_option_num
    else:
        assert atf.get_config_parameter("SuspendTime") == str(g_suspend_time) + " sec"

    parts = atf.get_partitions()
    assert parts["all"]["SuspendTime"] == global_option_str
    assert parts["all"]["SuspendTimeout"] == global_option_str
    assert parts["all"]["ResumeTimeout"] == global_option_str

    assert parts["p1"]["SuspendTime"] == p1_suspend_time
    assert parts["p1"]["SuspendTimeout"] == p1_suspend_timeout
    assert parts["p1"]["ResumeTimeout"] == p1_resume_timeout

    assert parts["p2"]["SuspendTime"] == p2_suspend_time
    assert parts["p2"]["SuspendTimeout"] == p2_suspend_timeout
    assert parts["p2"]["ResumeTimeout"] == p2_resume_timeout

    assert parts["p3"]["SuspendTime"] == infinite_option_str

    nodes = atf.get_nodes()
    assert nodes["node1"]["suspend_time"] == suspend_time_val(g_suspend_time)
    assert nodes["d1"]["suspend_time"] == suspend_time_val(g_suspend_time)
    assert nodes["d2"]["suspend_time"] == suspend_time_val(n_suspend_time)

    atf.run_command(
        "scontrol update partitionname=p1 nodes=node1,d[1-2]",
        user=atf.properties["slurm-user"],
    )
    nodes = atf.get_nodes()
    assert nodes["node1"]["suspend_time"] == suspend_time_val(p1_suspend_time)
    assert nodes["d1"]["suspend_time"] == suspend_time_val(p1_suspend_time)
    assert nodes["d2"]["suspend_time"] == suspend_time_val(n_suspend_time)

    # Node will take (in order):
    # - node SuspendTime
    # - the largest partition
    # - global SuspendTime
    atf.run_command(
        "scontrol update partitionname=p2 nodes=node1,d[1-2]",
        user=atf.properties["slurm-user"],
    )
    nodes = atf.get_nodes()
    assert nodes["node1"]["suspend_time"] == suspend_time_val(p2_suspend_time)
    assert nodes["d1"]["suspend_time"] == suspend_time_val(p2_suspend_time)
    assert nodes["d2"]["suspend_time"] == suspend_time_val(n_suspend_time)

    # Validate options stay after a reconfig
    atf.run_command("scontrol reconfig", user=atf.properties["slurm-user"])

    parts = atf.get_partitions()
    assert parts["all"]["SuspendTime"] == global_option_str
    assert parts["all"]["SuspendTimeout"] == global_option_str
    assert parts["all"]["ResumeTimeout"] == global_option_str

    assert parts["p1"]["SuspendTime"] == p1_suspend_time
    assert parts["p1"]["SuspendTimeout"] == p1_suspend_timeout
    assert parts["p1"]["ResumeTimeout"] == p1_resume_timeout

    assert parts["p2"]["SuspendTime"] == p2_suspend_time
    assert parts["p2"]["SuspendTimeout"] == p2_suspend_timeout
    assert parts["p2"]["ResumeTimeout"] == p2_resume_timeout

    assert parts["p3"]["SuspendTime"] == infinite_option_str

    nodes = atf.get_nodes()
    assert nodes["node1"]["suspend_time"] == suspend_time_val(p2_suspend_time)
    assert nodes["d1"]["suspend_time"] == suspend_time_val(p2_suspend_time)
    assert nodes["d2"]["suspend_time"] == suspend_time_val(n_suspend_time)
