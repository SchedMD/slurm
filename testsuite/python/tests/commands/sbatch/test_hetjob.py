############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_nodes(1, [('CPUs', 4), ('RealMemory', 40)])
    atf.require_slurm_running()


def test_hetjob(tmp_path):
    file_in = str(tmp_path / "hetjob.in")
    atf.make_bash_script(file_in, """#SBATCH --cpus-per-task=4 --mem-per-cpu=10 --ntasks=1
#SBATCH hetjob
#SBATCH --cpus-per-task=2 --mem-per-cpu=2  --ntasks=1 -t1
#SBATCH hetjob
#SBATCH --cpus-per-task=1 --mem-per-cpu=6  --ntasks=1 -t1

$bin_sleep 300""")
    leader_job_id = atf.submit_job(f"-t1 {file_in}", fatal=True)
    jobs_dict = atf.get_jobs()

    # Verify details about leader job
    assert leader_job_id in jobs_dict
    leader_job_dict = jobs_dict[leader_job_id]
    assert leader_job_dict['JobId'] == leader_job_id
    assert leader_job_dict['HetJobId'] == leader_job_id
    assert leader_job_dict['HetJobOffset'] == 0
    het_job_expression = leader_job_dict['HetJobIdSet']
    assert re.search(r'[\d,-]+', het_job_expression) is not None
    assert leader_job_dict['CPUs/Task'] == 4
    assert leader_job_dict['MinMemoryCPU'] == '10M'
    het_job_component_list = atf.range_to_list(het_job_expression)
    het_job_component_list.remove(leader_job_id)
    assert len(het_job_component_list) == 2

    # Verify details about first component job
    component1_job_id = het_job_component_list[0]
    assert component1_job_id in jobs_dict
    component1_job_dict = jobs_dict[component1_job_id]
    assert component1_job_dict['JobId'] == component1_job_id
    assert component1_job_dict['HetJobId'] == leader_job_id
    assert component1_job_dict['HetJobOffset'] == 1
    assert re.search(r'[\d,-]+', component1_job_dict['HetJobIdSet']) is not None
    assert component1_job_dict['CPUs/Task'] == 2
    assert component1_job_dict['MinMemoryCPU'] == '2M'

    # Verify details about second component job
    component2_job_id = het_job_component_list[1]
    assert component2_job_id in jobs_dict
    component2_job_dict = jobs_dict[component2_job_id]
    assert component2_job_dict['JobId'] == component2_job_id
    assert component2_job_dict['HetJobId'] == leader_job_id
    assert component2_job_dict['HetJobOffset'] == 2
    assert re.search(r'[\d,-]+', component2_job_dict['HetJobIdSet']) is not None
    assert component2_job_dict['CPUs/Task'] == 1
    assert component2_job_dict['MinMemoryCPU'] == '6M'
