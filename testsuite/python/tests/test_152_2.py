############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

IMEX_CHANNEL_PATH = "/dev/nvidia-caps-imex-channels"
NUM_NODES = 8
CHANNEL_MAX = NUM_NODES
TTY_MAJOR_NUM = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf")
    atf.require_nodes(NUM_NODES)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    atf.require_config_parameter("SwitchType", "switch/nvidia_imex")
    atf.require_config_parameter(
        "SwitchParameters",
        f"imex_channel_count={CHANNEL_MAX},imex_dev_major={TTY_MAJOR_NUM}",
    )
    atf.require_version((25, 11), "sbin/slurmd")

    # Mark topology for teardown and overwrite with proper data.
    # require_config_parameter marks a file for teardown,
    #  but it doesn't allow us to write multiple lines easily to an external conf.
    # We're using it to create/mark the file.
    atf.require_config_parameter("", "", source="topology")
    # This is where we write the actual data
    overwrite_topology_conf()

    atf.require_slurm_running()


def overwrite_topology_conf():
    conf = atf.properties["slurm-config-dir"] + "/topology.conf"
    content = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.run_command(f"cat > {conf}", input=content, user="slurm", fatal=True)


def _simple_channel_job(job_args):
    output = atf.run_command(
        f"srun {job_args} ls {IMEX_CHANNEL_PATH}",
        timeout=5,
        fatal=False,
    )
    assert output["exit_code"] == 0, "Expected srun to run successfully"

    # Convert ls output into a list of unique channels
    return len(set(output["stdout"].strip().splitlines()))


channel_per_segment_params = [
    ("-N8 --segment=1", 1),
    ("-N8 --segment=2", 1),
    ("-N8 --segment=4", 1),
    ("-N8 --segment=8", 1),
    pytest.param(
        "-N8 --segment=1 --network=unique-channel-per-segment",
        8,
        marks=pytest.mark.xfail(
            atf.get_version("bin/scontrol") < (25, 11),
            reason="Dev #50642: Unique IMEX channel per segment",
        ),
    ),
    pytest.param(
        "-N8 --segment=2 --network=unique-channel-per-segment",
        4,
        marks=pytest.mark.xfail(
            atf.get_version("bin/scontrol") < (25, 11),
            reason="Dev #50642: Unique IMEX channel per segment",
        ),
    ),
    pytest.param(
        "-N8 --segment=4 --network=unique-channel-per-segment",
        2,
        marks=pytest.mark.xfail(
            atf.get_version("bin/scontrol") < (25, 11),
            reason="Dev #50642: Unique IMEX channel per segment",
        ),
    ),
    ("-N8 --segment=8 --network=unique-channel-per-segment", 1),
]


@pytest.mark.parametrize(
    "job_args,expected_channel_count",
    channel_per_segment_params,
)
def test_channel_per_segment(job_args, expected_channel_count):
    num_channels = _simple_channel_job(job_args)
    assert (
        num_channels is expected_channel_count
    ), "Unexpected number of channels created"
