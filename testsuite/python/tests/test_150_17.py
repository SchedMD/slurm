############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test partition DefSegmentSize with topology/block."""

import pytest

import atf


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11), "bin/scontrol", reason="Issue 50961: DefSegmentSize added in 26.11"
    )
    atf.require_nodes(8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    topology_config = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockSizes=4,8
    """
    atf.require_config_file("topology.conf", topology_config)
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def part_with_default(request):
    """Configure DefSegmentSize (2 unless parametrized) for the default partition"""

    partition = atf.default_partition()
    atf.set_partition_parameter(
        partition, "DefSegmentSize", getattr(request, "param", 2)
    )
    yield partition
    atf.set_partition_parameter(partition, "DefSegmentSize", None)


def submit_job(sbatch_args):
    return atf.submit_job_sbatch(
        f'{sbatch_args} --exclusive --mem=1 --wrap="sleep infinity"', fatal=True
    )


def segment_size(job_id):
    return atf.get_job_parameter(
        job_id, "SegmentSize", default=0, quiet=True, fatal=True
    )


def test_no_default():
    """Verify jobs get no segment size when DefSegmentSize is not configured"""

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 0, "Job should have no segment size"

    # SLURM_JOB_SEGMENT_SIZE is only set when a segment size applies.
    job_id = atf.submit_job_sbatch(
        "-N 2 --exclusive --mem=1 -o out.txt "
        "--wrap='echo SEG=${SLURM_JOB_SEGMENT_SIZE:-unset}'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.assert_file_contents("out.txt", "SEG=unset")


def test_default_applied(part_with_default):
    """Verify DefSegmentSize is used by a job which does not request one"""

    # Containment: scontrol prints multiple job defaults comma-joined.
    assert "DefSegmentSize=2" in (
        atf.get_partition_parameter(part_with_default, "JobDefaults") or ""
    ), "DefSegmentSize should be reported as a job default"

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 2, "Job should use DefSegmentSize"


def test_job_request_wins(part_with_default):
    """Verify --segment takes precedence over DefSegmentSize"""

    job_id = submit_job("-N 4 --segment=4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 4, "Job should keep its requested segment size"


def test_sbatch_segment_zero_opts_out(part_with_default):
    """Verify sbatch --segment=0 opts a job out of DefSegmentSize"""

    # 3 is not a multiple of the default of 2, so the job is only accepted
    # (see test_default_always_applied) if it does not use the default.
    job_id = submit_job("-N 3 --segment=0")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 0, "--segment=0 should leave the job unsegmented"


@pytest.mark.parametrize("command", ["srun", "salloc"])
def test_interactive_segment_zero_opts_out(part_with_default, command):
    """Verify srun and salloc --segment=0 opt a job out of DefSegmentSize"""

    # As above, -N 3 is rejected under DefSegmentSize=2 unless the job opts out.
    result = atf.run_command(f"{command} -N 3 --segment=0 --exclusive --mem=1 true")
    assert (
        result["exit_code"] == 0
    ), f"{command} --segment=0 should not be rejected by DefSegmentSize"


def test_default_always_applied(part_with_default):
    """Verify DefSegmentSize is applied even when it doesn't fit the job size"""

    # 3 is not a multiple of 2, so once the default is applied the job
    # cannot be scheduled and is rejected at submission. (An explicit
    # --segment=2 is instead rejected client side by sbatch.) Without the
    # default the same job is accepted, see test_no_default_odd_node_count().
    error = atf.run_command_error(
        'sbatch -N 3 --exclusive --mem=1 --wrap="sleep infinity"',
        xfail=True,
        fatal=True,
    )
    assert (
        "Requested topology configuration is not available" in error
    ), "Job should be rejected -- 3 % 2"


def test_no_default_odd_node_count():
    """Verify the same job is accepted when DefSegmentSize is not set"""

    job_id = submit_job("-N 3")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 0, "Job should have no segment size"


@pytest.mark.parametrize("part_with_default", [4], indirect=True)
def test_default_noop_when_job_fits_in_segment(part_with_default):
    """Verify a job which fits within a single segment is not rejected.

    --segment documents that a job whose nodes fit within a single segment is
    unaffected. The same must hold when the size comes from DefSegmentSize:
    a -N 2 job under DefSegmentSize=4 must run, not be rejected or left
    pending. This guards the blast radius of a cluster wide default, which
    would otherwise reject every job smaller than the default.
    """

    job_id = submit_job("-N 2")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        segment_size(job_id) == 4
    ), "The default is recorded even when it has no scheduling effect"


def test_segment_size_env_vars(part_with_default):
    """Verify the segment size environment variables work with DefSegmentSize"""

    # The output variable reflects the applied default.
    job_id = atf.submit_job_sbatch(
        "-N 2 --exclusive --mem=1 -o out.txt --wrap='printenv SLURM_JOB_SEGMENT_SIZE'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    atf.assert_file_contents("out.txt", "2")

    # salloc reports the applied default in the allocation environment.
    output = atf.run_command_output(
        "salloc -N 2 --exclusive --mem=1 printenv SLURM_JOB_SEGMENT_SIZE",
        fatal=True,
    )
    assert output.strip() == "2", "salloc allocation should carry the default"

    # The input variable is an explicit request and beats the default.
    job_id = atf.submit_job_sbatch(
        '-N 4 --exclusive --mem=1 --wrap="sleep infinity"',
        env_vars="SBATCH_SEGMENT_SIZE=4",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 4, "SBATCH_SEGMENT_SIZE should beat DefSegmentSize"

    # srun's input variable is likewise an explicit request and beats the
    # default; the job environment reflects the requested size (one line of
    # output per node).
    output = atf.run_job_output(
        "-N 4 --exclusive --mem=1 printenv SLURM_JOB_SEGMENT_SIZE",
        env_vars="SRUN_SEGMENT_SIZE=4",
        fatal=True,
    )
    lines = output.splitlines()
    assert lines and all(
        line == "4" for line in lines
    ), "SRUN_SEGMENT_SIZE should beat DefSegmentSize"


@pytest.fixture(scope="function")
def cluster_default(request):
    """Configure a cluster wide DefSegmentSize and unset it again afterwards"""

    atf.set_config_parameter("DefSegmentSize", request.param)
    yield
    atf.set_config_parameter("DefSegmentSize", None)


@pytest.mark.parametrize("cluster_default", [2], indirect=True)
def test_cluster_default_applied(cluster_default):
    """Verify the cluster wide DefSegmentSize applies to a job without one"""

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 2, "Job should use the cluster wide DefSegmentSize"


@pytest.mark.parametrize("cluster_default", [4], indirect=True)
def test_partition_default_overrides_cluster(part_with_default, cluster_default):
    """Verify the partition DefSegmentSize wins over the cluster wide one"""

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert segment_size(job_id) == 2, "Job should use the partition DefSegmentSize"


@pytest.mark.parametrize("part_with_default", [0], indirect=True)
@pytest.mark.parametrize("cluster_default", [4], indirect=True)
def test_partition_zero_opts_out_of_cluster(part_with_default, cluster_default):
    """Verify DefSegmentSize=0 opts a partition out of the cluster wide default"""

    job_id = submit_job("-N 4")
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    assert (
        segment_size(job_id) == 0
    ), "DefSegmentSize=0 should override the cluster wide default"
