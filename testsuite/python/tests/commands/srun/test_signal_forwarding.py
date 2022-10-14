############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import pathlib
import re
import signal
import time
import logging


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_signal_forwarding():
    """Test of srun signal forwarding"""

    sig1_count = 2
    sig2_count = 2

    file_in = atf.module_tmp_path / 'file_in'
    file_out = atf.module_tmp_path / 'file_out'
    script_file = pathlib.Path(__file__).parent.resolve() / 'signal_forwarding_script.py'
    atf.make_bash_script(file_in, f"""srun --output={file_out} python3 {script_file}""")
    job_id = atf.submit_job(f'{file_in}')

    for i in range(sig1_count):
        atf.run_command(f"scancel --signal {signal.SIGUSR1.value} {job_id}")
        time.sleep(1)
    for i in range(sig2_count):
        atf.run_command(f"scancel --signal {signal.SIGUSR2.value} {job_id}")
        time.sleep(1)
    atf.wait_for_job_state(job_id, 'DONE', timeout=60)

    received_all_sigs = False
    user1_count = 0
    user2_count = 0
    with open(file_out, 'r') as fp:
        for line in fp:
            if re.search(str(signal.SIGUSR1.value), line) is not None:
                user1_count += 1
            if re.search(str(signal.SIGUSR2.value), line) is not None:
                user2_count += 1
            if user1_count == sig1_count and user2_count == sig2_count:
                received_all_sigs = True
                break
    assert received_all_sigs == True, f"Did not receive all signals. Signal {signal.SIGUSR1.value} count: {user1_count}, Signal {signal.SIGUSR2.value} count: {user2_count}"
