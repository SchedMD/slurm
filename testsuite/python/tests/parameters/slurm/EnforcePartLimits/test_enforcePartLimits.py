############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import os
import re
import time
import pwd

max_mem_cpu = 2
max_mem_node = 1
min_num_nodes = 2
max_num_nodes = 1
max_time = 1
node_list = []
current_policy = ""
previous_limit = ""
p1_node_str = ""

# Limits in development that are not included in this test:
# AllocNodes, AllowGroups, QOS usage threshold

limits_dict = {
    'MaxMemPerCPU'  : { 'flag': '--mem-per-cpu=',
                        'fail': max_mem_cpu + 1,
                        'pass': max_mem_cpu,
                        '_set': max_mem_cpu},

    'MaxMemPerNode' : { 'flag': '--mem=',
                        'fail': max_mem_node + 1,
                        'pass': max_mem_node,
                        '_set': max_mem_node},

    'MinNodes'      : { 'flag': '-N',
                        'fail': min_num_nodes - 1,
                        'pass': min_num_nodes,
                        '_set': min_num_nodes},

    'MaxNodes'      : { 'flag': '-N',
                        'fail': max_num_nodes + 1,
                        'pass': max_num_nodes,
                        '_set': max_num_nodes},

    'MaxTime'       : { 'flag': '-t',
                        'fail': max_time + 1,
                        'pass': max_time,
                        '_set': max_time},

    'AllowAccounts' : { 'flag': '',
                        'fail': '--account=bad_account',
                        'pass': '--account=good_account',
                        '_set': 'good_account'},

    'AllowQos'      : { 'flag': '',
                        'fail': '--qos=bad_qos',
                        'pass': '--qos=good_qos',
                        '_set': 'good_qos'}
}


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_list, p1_node_str

    atf.require_auto_config("wants to change partitions, modify/create nodes")
    atf.require_accounting(modify=True)
    atf.require_config_parameter("AccountingStorageEnforce", "limits")

    # RealMemory is a problem, it defaults as 1M and thus restricts it,
    # require_nodes doesn't set it on default node1 for some reason, below is a fix.
    # See: https://bugs.schedmd.com/show_bug.cgi?id=3807#c1

    # Gather a list of nodes that meet the RealMemory Requirement (node_list)
    atf.require_nodes(4, [('RealMemory', max_mem_node + 1)])
    nodes_dict = atf.get_nodes(live=False)

    for node_name, node_dict in nodes_dict.items():
        for parameter_name, parameter_value in node_dict.items():
            if parameter_name == 'RealMemory' and int(parameter_value) >= (max_mem_node + 1):
                node_list.append(node_name)

    atf.require_config_parameter('PartitionName', {
    'p1'    : {
        'Nodes'         : ','.join(node_list[0:2]),
        'Default'       : 'NO',
        'State'         : 'UP'},

    'p2'    : {
        'Nodes'         : ','.join(node_list[2:5]),
        'State'         : 'UP',
        'MaxTime'       : 'INFINITE'}
    })

    p1_node_str = ','.join(node_list[0:2])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def setup_account():
    test_user = pwd.getpwuid(os.getuid())[0]
    atf.run_command(f"sacctmgr -vi add account good_account", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -vi add user {test_user} account=good_account", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -vi add qos good_qos,bad_qos", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -vi modify user {test_user} set qos=good_qos,bad_qos", user=atf.properties['slurm-user'], fatal=True)


@pytest.fixture(scope='function')
def cancel_jobs():
    """Cancel all jobs after each test"""
    yield
    atf.cancel_all_jobs(fatal=True)


# Helper funcs:

def set_enforce_part_limits_policy(policy):
    global current_policy

    if current_policy != policy:
        atf.set_config_parameter('EnforcePartLimits', policy)
        current_policy = policy


def set_partition_limit(limit_name, limit_value):
    global previous_limit

    if previous_limit:
        atf.set_partition_parameter('p1', previous_limit, None)
    atf.set_partition_parameter('p1', limit_name, limit_value)
    previous_limit = limit_name


def satisfy_pending_job_limit(job_id, limit_name, val_pass):
    atf.wait_for_job_state(job_id, 'PENDING', poll_interval=.1, fatal=True, quiet=True)

    # Update partition limit to comply
    atf.run_command(f"scontrol update partitionname=p1 {limit_name}={val_pass}", user=atf.properties['slurm-user'], fatal=True, quiet=True)

    # Allow time for job to requeue and complete
    atf.wait_for_job_state(job_id, 'DONE', poll_interval=.5, fatal=True, quiet=True)


# Test functions
def enforce_ALL(limit_name, flag, val_fail, val_pass):

    # Must account for the higher value needed to pass first assert when using
    # MaxMemPerCPU as it will allocate more cpus to fill the memory requirement
    # If it can't allocate more cpus it will fail, so the limit will still be imposed
    # on p2 (test 3) in a way because the MaxMemPerCPU will exceed the nodes RealMemory
    # see https://slurm.schedmd.com/slurm.conf.html#OPT_MaxMemPerCPU

    custom_val_fail = val_fail
    if limit_name == "MaxMemPerCPU":
        custom_val_fail = val_fail - 1
    else:
        if limit_name == "AllowAccounts": custom_val_fail = ''

    # 1 Reject p1,p2 with no p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{val_fail} --wrap \"hostname\" -o /dev/null") == 0, f"Job should fail on p1,p2 due to {limit_name} limit not met on the required partition p1 with EnforcePartLimits=ALL"

    # 2 Reject p1 no limit met
    assert atf.submit_job(f"-p p1 {flag}{val_fail} --wrap \"hostname\" -o /dev/null") == 0, f"Job should fail on p1 due to {limit_name} limit not met on the required partition p1 with EnforcePartLimits=ALL"

    # 3 Accept p2 no limit met ** This one and the first have a memory conflict
    assert atf.submit_job(f"-p p2 {flag}{custom_val_fail} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p2 despite {limit_name} limit not met on the required partition p1 with EnforcePartLimits=ALL"

    # 4 Accept p1 with limit met
    assert atf.submit_job(f"-p p1 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=ALL"

    # 5 Accept p1,p2 with p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1,p2 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=ALL"


def enforce_ANY(limit_name, flag, val_fail, val_pass):

    # Must account for the higher value needed to pass first assert (from 'ALL') when using
    # MaxMemPerCPU as it will allocate more cpus to fill the memory requirement

    custom_val_fail = val_fail
    if limit_name == "MaxMemPerCPU":
        custom_val_fail = val_fail - 1
    else:
        if limit_name == "AllowAccounts": custom_val_fail = ''

    # 1 Accept p1,p2 with no p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{custom_val_fail} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1,p2 despite {limit_name} not met on p1 with EnforcePartLimits=ANY"

    # 2 Reject p1 no limit met
    assert atf.submit_job(f"-p p1 {flag}{val_fail} --wrap \"hostname\" -o /dev/null") == 0, f"Job should fail on p1 due to {limit_name} limit not met on the required partition p1 with EnforcePartLimits=ANY"

    # 3 Accept p2 no limit met ** This one and the first have a memory conflict
    assert atf.submit_job(f"-p p2 {flag}{custom_val_fail} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p2 despite {limit_name} limit not met on the required partition p1 with EnforcePartLimits=ANY"

    # 4 Accept p1 with limit met
    assert atf.submit_job(f"-p p1 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=ANY"

    # 5 Accept p1,p2 with p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1,p2 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=ANY"


def enforce_NO(limit_name, flag, val_fail, val_pass):

    # Must account for the higher value needed to pass first assert (from 'ALL') when using
    # MaxMemPerCPU as it will allocate more cpus to fill the memory requirement

    custom_val_fail = val_fail
    if limit_name == "MaxMemPerCPU":
        custom_val_fail = val_fail - 1
    else:
        if limit_name == "AllowAccounts": custom_val_fail = ''

    # 1 Submit -> pend on p1,p2 with bad p1 limit set -> complete with p1 limit met
    job_id = atf.submit_job(f"-p p1,p2 {flag}{custom_val_fail} --wrap \"hostname&\" -o /dev/null", timeout=1)
    satisfy_pending_job_limit(job_id, limit_name, custom_val_fail);
    assert atf.get_job_parameter(job_id, 'JobState', quiet=True) == 'COMPLETED', f"Job should submit, pend, then complete on p1,p2 with updated limit {limit_name} on partition p1 to passing valueswith EnforcePartLimits=NO"

    # 2 Submit -> pend on just p1 with bad limit, then complete with good limit
    job_id = atf.submit_job(f"-p p1 {flag}{custom_val_fail} --wrap \"hostname&\" -o /dev/null", timeout=1)
    satisfy_pending_job_limit(job_id, limit_name, custom_val_fail);
    assert atf.get_job_parameter(job_id, 'JobState', quiet=True) == 'COMPLETED', f"Job should submit, pend, then complete on p1 with updated limit {limit_name} on partition p1 to passing values with EnforcePartLimits=NO"

    # 3 Submit -> complete on p2 with no limit set
    assert atf.submit_job(f"-p p2, {flag}{custom_val_fail} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p2 despite {limit_name} limit not met on the required partition p1 with EnforcePartLimits=NO"

    # 4 Submit -> complete on p1 with p1 limit met
    assert atf.submit_job(f"-p p1 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=NO"

    # 5 Submit -> complete on p1,p2 with p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1,p2 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=NO"


def enforce_NO_QOS(limit_name, flag, val_fail, val_pass):
    flag = "--qos="
    val_pass = "good_qos"
    val_fail = "bad_qos"

    # 1 Submit -> pend on p1,p2 with bad p1 limit set -> complete with p1 limit met
    job_id = atf.submit_job(f"-p p1,p2 {flag}{val_fail} --wrap \"hostname&\" -o /dev/null", timeout=1)
    satisfy_pending_job_limit(job_id, limit_name, f"{val_pass},{val_fail}");
    assert atf.get_job_parameter(job_id, 'JobState', quiet=True) == 'COMPLETED', f"Job should submit, pend, then complete on p1,p2 with updated limit {limit_name} on partition p1 to passing valueswith EnforcePartLimits=NO"

    # Reset partition QOS
    atf.run_command(f"scontrol update partitionname=p1 {limit_name}={val_pass}", user=atf.properties['slurm-user'], fatal=True, quiet=True)

    # 2 Submit -> pend on just p1 with bad limit, then complete with good limit
    job_id = atf.submit_job(f"-p p1 {flag}{val_fail} --wrap \"hostname&\" -o /dev/null", timeout=1)
    satisfy_pending_job_limit(job_id, limit_name, f"{val_pass},{val_fail}");
    assert atf.get_job_parameter(job_id, 'JobState', quiet=True) == 'COMPLETED', f"Job should submit, pend, then complete on p1 with updated limit {limit_name} on partition p1 to passing values with EnforcePartLimits=NO"

    # Reset partition QOS
    atf.run_command(f"scontrol update partitionname=p1 {limit_name}={val_pass}", user=atf.properties['slurm-user'], fatal=True, quiet=True)

    # 3 Submit -> complete on p2 with no limit set
    assert atf.submit_job(f"-p p2, {flag}{val_fail} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p2 despite {limit_name} limit not met on the required partition p1 with EnforcePartLimits=NO"

    # 4 Submit -> complete on p1 with p1 limit met
    assert atf.submit_job(f"-p p1 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=NO"

    # 5 Submit -> complete on p1,p2 with p1 limit met
    assert atf.submit_job(f"-p p1,p2 {flag}{val_pass} --wrap \"hostname\" -o /dev/null") != 0, f"Job should pass on p1,p2 due to {limit_name} limit met on the required partition p1 with EnforcePartLimits=NO"


# Tests:
@pytest.mark.parametrize("limit_name", limits_dict.keys())
def test_ALL(limit_name, setup_account, cancel_jobs):
    """Verify jobs are accepted and rejected with EnforePartLimits=ALL"""

    set_enforce_part_limits_policy('ALL')
    value = limits_dict[limit_name]
    set_partition_limit(limit_name, value['_set'])
    enforce_ALL(limit_name, value['flag'], value['fail'], value['pass'])


@pytest.mark.parametrize("limit_name", limits_dict.keys())
def test_ANY(limit_name, setup_account, cancel_jobs):
    """Verify jobs are accepted and rejected with EnforePartLimits=ANY"""

    set_enforce_part_limits_policy('ANY')
    value = limits_dict[limit_name]
    set_partition_limit(limit_name, value['_set'])
    enforce_ANY(limit_name, value['flag'], value['fail'], value['pass'])


@pytest.mark.parametrize("limit_name", limits_dict.keys())
def test_NO(limit_name, setup_account, cancel_jobs):
    """Verify jobs are accepted and rejected with EnforePartLimits=NO"""

    set_enforce_part_limits_policy('NO')
    value = limits_dict[limit_name]
    set_partition_limit(limit_name, value['_set'])

    if limit_name == "AllowQos":
        enforce_NO_QOS(limit_name, value['flag'], value['fail'], value['pass'])
    else:
        enforce_NO(limit_name, value['flag'], value['fail'], value['pass'])
