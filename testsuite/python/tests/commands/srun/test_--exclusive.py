############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_count = 4

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    
    atf.require_accounting()
    atf.require_slurm_running()
    atf.require_nodes(node_count)


def sacct_verify_unique_nodes(sacct_string):
    sacct_output_list = sacct_string.split('\n')
    sacct_output_set = set()

    # The steps don't start until the second index
    for i in sacct_output_list[2:-1]:
        sacct_output_set.add(i.split('|')[0])
    # Convert to set verify that the set and list have the same number of elements
    # If not then the one of the steps ran on the same node which it shouldn't
    if len(sacct_output_set) == node_count:
        return True
    return False
    

def test_exclusive(tmp_path):
    file_in = str(tmp_path / "exclusive.in")
    atf.make_bash_script(file_in, """srun -n1 --exclusive sleep 10 &
srun -n1 --exclusive sleep 10 &
srun -n1 --exclusive sleep 10 &
srun -n1 --exclusive sleep 10 &
wait
    """)
    output = atf.run_command_output(f"sbatch -N{node_count} -t2 {file_in}")
    job_id = int(match.group(1)) if (match := re.search(r'(\d+)', output)) else None
    assert job_id is not None, "A job id was not returned"
    assert atf.wait_for_job_state(job_id, 'DONE', timeout=60, poll_interval=2), f"Job ({job_id}) did not run"
    assert sacct_verify_unique_nodes(atf.run_command_output(f"sacct -j {job_id} --format=Nodelist --parsable2 --noheader")) is True, "Jobs ran on the same node"
