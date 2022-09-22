############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to set the Prolog")
    atf.require_slurm_running()

def test_prolog_success(tmp_path):
    """Test successful exit"""

    prolog_script = str(tmp_path / 'prolog.sh')
    prolog_touched_file = str(tmp_path / 'prolog_touched_file')
    atf.make_bash_script(prolog_script, f"touch {prolog_touched_file}")
    atf.set_config_parameter('Prolog', prolog_script)

    job_id = atf.submit_job(fatal=True)

    # Verify that the prolog ran by checking for the file creation
    assert atf.wait_for_file(prolog_touched_file), f"File ({prolog_touched_file}) was not created"

    # Verify that the job runs
    assert atf.wait_for_job_state(job_id, "RUNNING")

    atf.cancel_jobs([job_id])


def test_prolog_failure(tmp_path):
    """Test failed exit"""

    prolog_script = str(tmp_path / 'prolog.sh')
    prolog_output_file = str(tmp_path / 'prolog_output_file')
    atf.make_bash_script(prolog_script, f"""printenv SLURMD_NODENAME > {prolog_output_file}
exit 1
""")
    atf.set_config_parameter('Prolog', prolog_script)

    job_id = atf.submit_job(fatal=True)

    # Verify that the prolog ran by checking for the file creation
    assert atf.wait_for_file(prolog_output_file), f"File ({prolog_output_file}) was not created"

    # The job should be requeued in a held state
    assert atf.wait_for_job_state(job_id, "PENDING")
    assert atf.repeat_command_until(f"scontrol show job {job_id}", lambda results: re.search(r"Reason=launch_failed_requeued_held", results['stdout']))

    # The node should be set to a DRAIN state
    node_name = atf.run_command_output(f"cat {prolog_output_file}", fatal=True).strip()
    assert atf.repeat_until(lambda : atf.get_node_parameter(node_name, 'State'), lambda state: re.search(r'DRAIN', state) != None, fatal=True), "The node is not in the drain state"

    atf.cancel_jobs([job_id])
