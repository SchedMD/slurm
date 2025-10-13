############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

IMEX_CHANNEL_PATH = "/dev/nvidia-caps-imex-channels"
NUM_NODES = 4
CHANNEL_MAX = NUM_NODES - 1
TTY_MAJOR_NUM = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(NUM_NODES)
    atf.require_config_parameter("SwitchType", "switch/nvidia_imex")
    atf.require_config_parameter(
        "SwitchParameters",
        f"imex_channel_count={CHANNEL_MAX},imex_dev_major={TTY_MAJOR_NUM}",
    )
    atf.require_slurm_running()


def _simple_channel_job():
    output = atf.run_command(
        f"srun ls {IMEX_CHANNEL_PATH}",
        timeout=5,
        fatal=False,
    )
    assert output["exit_code"] == 0, "Expected srun to run successfully"

    # Convert ls output into a list channels
    channel_list = output["stdout"].strip().splitlines()

    return channel_list


def _job_expect_channel(channel):
    channel_list = _simple_channel_job()

    assert channel_list == [channel], f"Unexpected channel found in {IMEX_CHANNEL_PATH}"


def _job_expect_pending():
    job_id = atf.submit_job_sbatch(
        f'--wrap="srun ls {IMEX_CHANNEL_PATH}; sleep 30"', fatal=True
    )
    if atf.get_version("bin/scontrol") >= (25, 11):
        # Dev #50642: Unique IMEX channel per segment
        reason = "NvidiaImexChannels"
    else:
        reason = None
    atf.wait_for_job_state(job_id, "PENDING", desired_reason=reason, fatal=True)

    return job_id


def test_single_channel():
    """Test single channel creation"""

    # Run twice to make sure that channels ids are getting released properly
    for _ in range(2):
        _job_expect_channel("channel1")


def _allocate_single_channel():
    job_id = atf.submit_job_sbatch(
        f'--wrap="srun ls {IMEX_CHANNEL_PATH}; sleep 30"', fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    return job_id


@pytest.mark.xfail(
    atf.get_version("sbin/slurmctld") < (25, 11),
    reason="Dev #50642: Unique IMEX channel per segment",
)
def test_multiple_channels():
    """Test channel creation for multiple jobs"""

    # Run twice to make sure that channels ids are getting released properly
    for _ in range(2):
        running_jobid = 0
        pending_jobid = 0

        atf.cancel_all_jobs()
        for i in range(CHANNEL_MAX):
            # Make sure that the channel id is as expected for a quick job
            _job_expect_channel(f"channel{i+1}")
            # Allocate a channel with a long job to keep it from the next job
            running_jobid = _allocate_single_channel()

        # At this point there should be no more channels left, and a job should
        # pend waiting for a channel to be released
        pending_jobid = _job_expect_pending()

        # Cancel last running job
        atf.cancel_jobs([running_jobid])

        # Previously pending job should start running
        atf.wait_for_job_state(pending_jobid, "RUNNING", fatal=True)
