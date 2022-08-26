############################################################################
# Purpose:  Test of Slurm functionality
#           Test MPS resource limits with various allocation options
#
# Requires: AccountingStorageEnforce=limits
#           AccountingStorageTRES=gres/mps
#           SelectType=select/cons_tres
#           Administrator permissions
#           GresTypes=gpu,mps
#           tty0
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

mps_cnt = 100
mps_nodes = []
mps_limit = 0
node_count = 2


#Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('SelectType', 'select/cons_tres')
    atf.require_config_parameter_includes('GresTypes', 'gpu')
    atf.require_config_parameter_includes('GresTypes', 'mps')
    atf.require_tty(0)
    atf.require_config_parameter('Name', {'gpu': {'File': '/dev/tty0'}, 'mps': {'Count': 100}}, source='gres')
    atf.require_nodes(node_count, [('Gres',f"gpu:1,mps:{mps_cnt}")])
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes('AccountingStorageTRES', 'gres/mps')
    atf.require_config_parameter_includes('AccountingStorageEnforce', 'limits')
    atf.require_slurm_running()

    if atf.get_config_parameter('frontendname') is not None:
        pytest.skip("SKIP: This test is incompatible with front-end systems")


@pytest.fixture(scope="function", autouse=True)
def create_account_with_limit():
    global mps_limit

    mps_limit = mps_cnt * node_count
    mps_limit = 50 if mps_limit > 8 else (mps_limit - 1)
    acct = "test_mps_acct" 
    cluster = atf.get_config_parameter("ClusterName")
    user = atf.get_user_name()

    atf.run_command(f"sacctmgr -i add account name={acct} cluster={cluster} parent=root maxtres=gres/mps={mps_limit}", user=atf.properties['slurm-user'], fatal = True)

    atf.run_command(f"sacctmgr -i add user user={user} cluster={cluster} account={acct}", user=atf.properties['slurm-user'], fatal = True)


def test_gres_mps_option_job():
    """TEST 1: --gres=mps option by job (first job over limit, second job under limit)"""

    file_in = atf.module_tmp_path / "input"
    file_out1 = atf.module_tmp_path / "output1"
    file_out2 = atf.module_tmp_path / "output2"

    mps_good_cnt = int( (mps_limit + node_count - 1) / node_count )
    mps_fail_cnt = int( mps_limit + 1 if node_count == 1 else (mps_good_cnt + 1) )

    atf.make_bash_script(file_in, """
    scontrol -dd show job $SLURM_JOBID | grep mps
    exit 0""")

    job_id1 = atf.submit_job(f"--gres=craynetwork:0 --gres=mps:{mps_fail_cnt} -N{node_count} -t1 -o {file_out1} -J 'test_job' {file_in}")
    assert job_id1 != 0, "Job 1 failed to submit"

    atf.repeat_command_until(f"scontrol show job {job_id1}", lambda results: re.search(r"Reason=.*AssocMaxGRESPerJob", results['stdout']), fatal=True)

    output = atf.run_command_output(f"scontrol show job {job_id1}")
    assert output is not None, "scontrol output required"

    assert re.search(r"JobState=PENDING", output), "Job state is bad (JobState != PENDING)"
    assert re.search(r"Reason=.*AssocMaxGRESPerJob", output), "Job state is bad (Reason != '.*AssocMaxGRESPerJob    ')"

    job_id2 = atf.submit_job(f"--account='test_mps_acct' --gres=craynetwork:0 --gres=mps:{mps_good_cnt} -N{node_count} -t1 -o {file_out2} -J 'test_job2' {file_in}")
    assert job_id2 != 0, "Job 2 failed to submit"
    assert atf.wait_for_job_state(job_id2, 'DONE', fatal=True)    
