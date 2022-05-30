############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

dw_wlm_cli_client = f"{atf.properties['slurm-sbin-dir']}/dw_wlm_cli"
dwstat_client = f"{atf.properties['slurm-sbin-dir']}/dwstat"


def require_datawarp_clients():
    # For the time being, we are installing the clients if necessary,
    # but not removing them afterwards. Later on, we could generalize
    # {backup,restore}_config_file to work for any file

    if atf.run_command_exit(f"test -f {dw_wlm_cli_client}", user=atf.properties['slurm-user'], quiet=True) != 0:
        if atf.properties['auto-config']:
            atf.run_command(f"cp {atf.properties['slurm-source-dir']}/src/plugins/burst_buffer/datawarp/dw_wlm_cli {dw_wlm_cli_client}", user=atf.properties['slurm-user'], fatal=True, quiet=True)
        else:
            pytest.skip(f"This test requires the dw_wlm_cli client to be installed in {atf.properties['slurm-sbin-dir']}", allow_module_level=True)

    if atf.run_command_exit(f"test -f {dwstat_client}", user=atf.properties['slurm-user'], quiet=True) != 0:
        if atf.properties['auto-config']:
            atf.run_command(f"cp {atf.properties['slurm-source-dir']}/src/plugins/burst_buffer/datawarp/dwstat {dwstat_client}", user=atf.properties['slurm-user'], fatal=True, quiet=True)
        else:
            pytest.skip(f"This test requires the dwstat client to be installed in {atf.properties['slurm-sbin-dir']}", allow_module_level=True)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter('BurstBufferType', 'burst_buffer/datawarp')
    #atf.require_config_parameter('DebugFlags', 'burstbuffer')
    atf.require_config_parameter('AllowUsers', f"{atf.properties['test-user']}", source='burst_buffer')
    atf.require_config_parameter_includes('Flags', 'EmulateCray', source='burst_buffer')
    atf.require_config_parameter_includes('Flags', 'EnablePersistent', source='burst_buffer')
    require_datawarp_clients()
    atf.require_config_parameter('GetSysState', dw_wlm_cli_client, source='burst_buffer')
    atf.require_config_parameter('GetSysStatus', dwstat_client, source='burst_buffer')
    atf.require_slurm_running()


def test_create_and_use_burst_buffer(tmp_path):
    """Create and use burst buffer"""

    bb_use_script = str(tmp_path / f"bb_use.sh")
    bb_use_output = str(tmp_path / f"bb_use.out")
    atf.make_bash_script(bb_use_script, f"""#DW persistentdw name=test_buffer
scontrol show burst
""")
    bb_create_script = str(tmp_path / f"bb_create.sh")
    bb_create_output = str(tmp_path / f"bb_create.out")
    atf.make_bash_script(bb_create_script, f"""#BB create_persistent name=test_buffer capacity=48 access=striped type=scratch
scontrol show burst
""")

    # Submit a job to use the persisent burst buffer
    bb_use_job_id = atf.submit_job(f"-N1 -t1 -o {bb_use_output} {bb_use_script}", fatal=True)

    # Submit a job to create the persisent burst buffer
    bb_create_job_id = atf.submit_job(f"-N1 -t1 -o {bb_create_output} {bb_create_script}", fatal=True)

    # The burst buffer creation should complete first
    assert atf.wait_for_job_state(bb_create_job_id, 'DONE', timeout=660), f"Burst buffer creation job ({bb_create_job_id}) did not run"

    # The burst buffer usage should then complete
    assert atf.wait_for_job_state(bb_use_job_id, 'DONE', timeout=660), f"Burst buffer usage job ({bb_create_job_id}) did not run"

    # Ensure the burst buffer usage job ran after the buffer creation
    assert atf.wait_for_file(bb_use_output, timeout=660)
    assert atf.run_command_exit(f"grep test_buffer {bb_use_output}") == 0, "Job using burst buffer ran before bb creation"

    assert atf.wait_for_file(bb_create_output, timeout=660)
    assert atf.run_command_exit(f"grep test_buffer {bb_create_output}") == 0, "Job creating burst buffer failed to do so"


def test_remove_burst_buffer(tmp_path):
    """Delete burst buffer"""

    bb_delete_script = str(tmp_path / f"bb_delete.sh")
    bb_delete_output = str(tmp_path / f"bb_delete.out")
    atf.make_bash_script(bb_delete_script, f"""#BB destroy_persistent name=test_buffer
scontrol show burst
""")

    # Submit a job to delete the persisent burst buffer
    bb_delete_job_id = atf.submit_job(f"-N1 -t1 -o {bb_delete_output} {bb_delete_script}", fatal=True)

    # The burst buffer deletion job should complete
    assert atf.wait_for_job_state(bb_delete_job_id, 'DONE', timeout=660), f"Burst buffer deletion job ({bb_delete_job_id}) did not run"

    assert atf.wait_for_file(bb_delete_output, timeout=660)
    assert atf.run_command_exit(f"grep test_buffer {bb_delete_output}") != 0, "Job deleting burst buffer failed to do so"
