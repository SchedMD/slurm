############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Early bail out of block scheduling for --spread-segments jobs."""

import pytest

import atf

SEGMENT_REASON = "No_suitable_topology_unit_found_for_segment"

# What sbatch prints for a job the controller decides can never run. The
# plugin's own ESLURM_TOPO_SEGMENT_NO_FIT does not reach the client here:
# _pick_best_nodes() forwards only ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE and
# ESLURM_NOT_SUPPORTED, and turns anything else into this generic error
# (src/slurmctld/node_scheduler.c). sbatch fails for many reasons, so the
# rejection still has to be identified by its text, and the segment specific
# evidence comes from the log that assert_early_bail() reads.
REJECT_ERROR = "Requested node configuration is not available"

# Signatures of the pre-fix path, where the plugin kept evaluating its way into
# a block shortage. Neither line means "late" on its own: in eval_nodes_block.c
# "node_map is empty" has both an alloc_node_map retry branch and a
# first-segment break branch, and "unable to find block" is reached on the
# first segment too. They mark the old retry path in general, and the fix must
# give up before either one is logged.
RETRY_PATTERNS = ["node_map is empty", "unable to find block"]

# The bail out the fix added, logged under DebugFlags=SelectType. Matches both
# of its lines: "base blocks fit N of M requested segments (spread check)" and
# "blocks fit N of M requested segments (block estimate)".
BAIL_PATTERN = "blocks fit.*requested segments"

# Resolved once in setup(), then read by log_count().
logfile = None

topology_conf = """
        BlockName=b1 Nodes=node[1-4]
        BlockName=b2 Nodes=node[5-8]
        BlockName=b3 Nodes=node[9-12]
        BlockName=b4 Nodes=node[13-16]
        BlockSizes=4,16
"""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("wants to create custom topology.conf")
    atf.require_nodes(16, [("CPUs", 4)])
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TopologyPlugin", "topology/block")
    # Every line log_count() looks for is emitted below info: "node_map is
    # empty" is a debug(), and "unable to find block" and both BAIL_PATTERN
    # lines are log_flag(), which prints at verbose. SlurmctldDebug defaults
    # to info, so without this every count stays at zero, and that would make
    # assert_early_bail() fail its positive check rather than pass silently.
    atf.require_config_parameter("SlurmctldDebug", "debug")
    atf.require_config_parameter_includes("DebugFlags", "SelectType")
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_file("topology.conf", topology_conf)
    atf.require_slurm_running()

    global logfile
    logfile = resolve_logfile()


def resolve_logfile():
    """Return the slurmctld log path, checked readable by the slurm user.

    get_config_parameter() casefolds the value it returns, so it cannot be
    used for a path: any upper case in the log path would yield a name that
    does not exist, and every log_count() below would silently return zero.
    get_config() preserves case.
    """

    config = atf.get_config(live=False, quiet=True)
    path = next(
        (
            value
            for key, value in config.items()
            if key.casefold() == "slurmctldlogfile"
        ),
        None,
    )
    assert path, "SlurmctldLogFile must be set to count slurmctld log lines"

    atf.run_command(
        f'test -r "{path}"',
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    return path


def log_position():
    """Return the line number the slurmctld log currently ends at.

    The harness collects this log, so a test must never truncate it. Reading
    on from a saved position isolates a measurement just as well and leaves
    the file intact.
    """

    result = atf.run_command(
        f'wc -l < "{logfile}"',
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )

    return int(result["stdout"].strip())


def log_count(pattern, job_id=None, since=0):
    """Count slurmctld log lines after since that match pattern, an awk ERE.

    Every plugin line counted here carries JobId=<n> from %pJ, so passing a
    job_id keeps the count from picking up lines that another job logged while
    the scheduler re-evaluated the queue. Where there is no job id to scope by,
    since does the same job for a window the caller opened with
    log_position().
    """

    if job_id:
        pattern = f"JobId={job_id} .*{pattern}"

    result = atf.run_command(
        f'awk -v pat="{pattern}" -v since={since} '
        f"'NR > since && $0 ~ pat {{n++}} END {{print n+0}}' \"{logfile}\"",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] == 0, f"Unable to search {logfile}: {result['stderr']}"

    return int(result["stdout"].strip())


def start_blockers(blockers):
    """Submit each blocker job and wait for all of them to run."""

    job_ids = []
    for blocker in blockers:
        job_ids.append(
            atf.submit_job_sbatch(
                f'{blocker} --mem=1 --wrap="sleep infinity"', fatal=True
            )
        )
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    return job_ids


def assert_early_bail(job_id=None, since=0):
    """Assert that the plugin bailed out early instead of retrying into it.

    Scope the counts by job_id where there is one, and otherwise by a since
    position the caller took before it submitted.
    """

    assert (
        log_count(BAIL_PATTERN, job_id, since) > 0
    ), "Plugin must log the segment shortage that it bailed out on"

    for pattern in RETRY_PATTERNS:
        assert (
            log_count(pattern, job_id, since) == 0
        ), f"Plugin must bail out before it retries, but logged '{pattern}'"


# Each case is (blocker jobs, job arguments, job can never run).
# The topology has four base blocks of four nodes in one 16 node block, so a
# segment of one to four nodes needs one base block, and a larger segment
# gathers whole base blocks.
BAIL_CASES = [
    pytest.param(
        [],
        "-N 5 --segment=1 --spread-segments --exclusive",
        True,
        id="more_segments_than_base_blocks",
    ),
    pytest.param(
        [],
        "-N 15 --segment=5 --spread-segments --exclusive",
        True,
        id="partial_base_blocks_fill_too_few_segments",
    ),
    pytest.param(
        ["-w node[1-2] -N 2 --exclusive", "-w node[9-16] -N 8 --exclusive"],
        "-N 6 --segment=3 --spread-segments --exclusive",
        False,
        id="only_one_base_block_fits_a_segment",
    ),
    pytest.param(
        [
            "-w node[1-2] -N 2 --ntasks-per-node=1 -c 3",
            "-w node[3-4] -N 2 --exclusive",
            "-w node[9-16] -N 8 --exclusive",
        ],
        "-N 4 -n 8 --segment=2 --spread-segments",
        False,
        id="base_block_short_on_cpus",
    ),
]

# Each case is (blocker jobs, job arguments).
RUN_CASES = [
    pytest.param(
        [],
        "-N 4 --segment=1 --spread-segments --exclusive",
        id="one_node_per_base_block",
    ),
    pytest.param(
        [],
        "-N 12 --segment=6 --spread-segments --exclusive",
        id="segment_spans_two_base_blocks",
    ),
    pytest.param(
        [],
        "-N 10 --segment=5 --spread-segments --exclusive",
        id="segments_at_block_capacity",
    ),
    pytest.param(
        ["-w node[1-2] -N 2 --exclusive"],
        "-N 4 --segment=2 --spread-segments --exclusive",
        id="partly_used_base_block",
    ),
]

# Each case is (job arguments). These are the two BAIL_CASES that no cluster
# state can satisfy, minus --spread-segments. Without spreading, segments may
# share a base block, so both fit the four base blocks and must run. The
# bail out the fix added applies to every job of more than one segment, not
# only to spreading ones, so these pin the tighter bound to --spread-segments.
NO_SPREAD_CASES = [
    pytest.param("-N 5 --segment=1 --exclusive", id="five_one_node_segments"),
    pytest.param(
        "-N 15 --segment=5 --exclusive",
        id="three_five_node_segments",
        marks=pytest.mark.skipif(
            atf.get_version("sbin/slurmctld") < (25, 11),
            reason="A segment larger than a base block needs 25.11+.",
        ),
    ),
]


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (26, 5, 5)
    or atf.get_version("bin/sbatch") < (25, 11),
    reason="Ticket 25720: spread segments did not bail out on a block shortage before 26.05.5, and sbatch --spread-segments needs 25.11+.",
)
@pytest.mark.parametrize("blockers, job_args, never_runnable", BAIL_CASES)
def test_spread_segments_bails_out(blockers, job_args, never_runnable):
    """The plugin gives up before it places a segment it cannot follow.

    A job that no cluster state can satisfy is rejected at submit. A job that
    only the current cluster state blocks pends with the segment reason, and
    runs once the blockers are gone.
    """

    blocker_ids = start_blockers(blockers)

    if never_runnable:
        # A rejected job never gets an id to scope the log search by, so note
        # where the log ends and read on from there after the submit.
        since = log_position()
        result = atf.run_command(
            f'sbatch {job_args} --mem=1 --wrap="exit 0"', xfail=True
        )
        assert (
            result["exit_code"] != 0
        ), "Job that no cluster state can satisfy must be rejected"
        assert REJECT_ERROR in result["stderr"], (
            "Job must be rejected as never runnable, but sbatch said"
            f' "{result["stderr"].strip()}"'
        )
        assert_early_bail(since=since)
        return

    job_id = atf.submit_job_sbatch(f'{job_args} --mem=1 --wrap="exit 0"', fatal=True)
    assert atf.wait_for_job_state(
        job_id, "PENDING", SEGMENT_REASON
    ), "Job must pend with the segment topology reason"
    assert_early_bail(job_id)

    # The bail out must be temporary, not a rejection.
    atf.cancel_jobs(blocker_ids, fatal=True)
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Job {job_id} must run once the blockers are gone"


@pytest.mark.skipif(
    atf.get_version("sbin/slurmctld") < (25, 11)
    or atf.get_version("bin/sbatch") < (25, 11),
    reason="--spread-segments was added in 25.11.",
)
@pytest.mark.parametrize("blockers, job_args", RUN_CASES)
def test_spread_segments_runs(blockers, job_args):
    """The bail out does not reject a job the blocks can still hold."""

    start_blockers(blockers)

    job_id = atf.submit_job_sbatch(f'{job_args} --mem=1 --wrap="exit 0"', fatal=True)
    assert atf.wait_for_job_state(job_id, "COMPLETED"), f"Job {job_id} should have run"


@pytest.mark.parametrize("job_args", NO_SPREAD_CASES)
def test_segments_without_spread_run(job_args):
    """These bail out cases need to run with --spread-segments removed"""

    job_id = atf.submit_job_sbatch(f'{job_args} --mem=1 --wrap="exit 0"', fatal=True)
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"Job {job_id} should have run since it doesn't use --spread-segments"
