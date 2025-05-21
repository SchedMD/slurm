############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import pathlib
import signal
import time


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


def test_signal_forwarding():
    """
    Test of srun signal forwarding.
    Submits a batch job running a python script via srun.
    Waits for the script to signal readiness by creating a file.
    Sends SIGUSR1 and SIGUSR2 signals via scancel.
    Verifies the script received the correct number of signals via job output.
    """

    sig1_expected_count = 2
    sig2_expected_count = 3
    readiness_timeout = 30  # Max seconds to wait for the script ready file
    job_wait_timeout = 60  # Max seconds to wait for job completion

    # Define file paths within the module's temporary directory
    file_in = "batch_script.sh"
    file_out = "job_output.log"
    file_ready = "script_ready.txt"

    # Get path to the python script
    script_file = (
        pathlib.Path(atf.properties["testsuite_python_lib"])
        / "signal_forwarding_script.py"
    )
    if not script_file.exists():
        pytest.fail(f"Signal script not found at: {script_file}")

    # Create the batch script content
    # Pass the readiness file path as an argument to the python script
    atf.make_bash_script(
        file_in, f"""srun --output={file_out} python3 {script_file} {file_ready}"""
    )

    # Submit the batch job
    job_id = atf.submit_job_sbatch(f"{file_in}", fatal=True)

    # Wait for the script to signal readiness by creating the file
    atf.wait_for_file(file_ready, fatal=True, timeout=readiness_timeout)

    # Send SIGUSR1 signals
    for i in range(sig1_expected_count):
        atf.run_command(f"scancel --signal {signal.SIGUSR1.value} {job_id}")
        # Keep a small delay between signals
        time.sleep(1)

    # Send SIGUSR2 signals
    for i in range(sig2_expected_count):
        atf.run_command(f"scancel --signal {signal.SIGUSR2.value} {job_id}")
        time.sleep(1)

    # Wait for the job and file to complete
    atf.wait_for_job_state(job_id, "DONE", timeout=job_wait_timeout, fatal=True)
    atf.wait_for_file(file_out, timeout=5, fatal=True)

    # Define expected exact lines from the signal handler
    expected_sig1_line = f"Received: {signal.SIGUSR1.value}"
    expected_sig2_line = f"Received: {signal.SIGUSR2.value}"

    # Read the output file
    with open(file_out, "r") as fp:
        content = fp.read()

    # Verify signal counts in the output file
    assert (
        content.count(expected_sig1_line) == sig1_expected_count
    ), f"Incorrect SIGUSR1 ({signal.SIGUSR1.value}) count. Expected: {sig1_expected_count}, Got: {content.count(expected_sig1_line)}"
    assert (
        content.count(expected_sig2_line) == sig2_expected_count
    ), f"Incorrect SIGUSR2 ({signal.SIGUSR2.value}) count. Expected: {sig2_expected_count}, Got: {content.count(expected_sig2_line)}"
