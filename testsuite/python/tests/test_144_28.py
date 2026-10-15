############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""acct_gather_energy/gpu survives a step node that holds no GPU of its own.

A step may request fewer GPUs than the number of nodes it spans, and step
gres allocation then satisfies the whole request on the first node it picks,
leaving the step's gres record on every other node with no local bitmap.
Before slurmd 26.11 the energy plugin's step filter passed that missing
bitmap to a bit count under DebugFlags=Energy and a slurmd log level of
verbose or above, crashing the slurmstepd at step launch, and since 26.11
it reports that it gathers no power for the step there instead. Two fake
nodes stage the shape hardware-free: each carries a gpu gres backed by a
tty character device, and a two-node step asking for a single GPU takes it
on one node while the other node holds a bitmap-less record. A second step
of the same job asks for no GRES at all, so a guard widened to gpu-less
steps would log the report on both nodes and fail the count.
"""

import os
import re

import pytest

import atf

# Resolved in setup: the two nodes carrying a gpu gres, sorted by name.
gpu_nodes = []

# The report the filter logs on a node whose step record has no bitmap.
NO_BITMAP_REPORT = "Step holds gpus with no allocation bitmap, no power gathering"

# The count line the filter logs on the node that does hold the GPU: set bits
# out of the node's configured GPU count, which setup pins at one. Neither
# report carries a regular expression metacharacter, so each doubles as the
# pattern a log wait greps for.
USABLE_GPUS_REPORT = "usable_gpus = 1 of 1"


@pytest.fixture(scope="module", autouse=True)
def setup():
    global gpu_nodes

    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason=(
            "Issue 50982: the energy plugin's no-bitmap step guard was added "
            "in 26.11"
        ),
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # select/cons_tres refuses to start without a CPU, core or socket
    # allocation unit, and the select/linear variants carry cr_memory alone,
    # which left slurmctld down there. The units are mutually exclusive, so
    # an additive include would leave slurm.conf unloadable: a value that
    # already names a unit is kept, anything else becomes CR_CPU.
    atf.require_config_parameter(
        "SelectTypeParameters",
        [
            "CR_CPU",
            "CR_Core",
            "CR_Socket",
            "CR_CPU_Memory",
            "CR_Core_Memory",
            "CR_Socket_Memory",
        ],
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # The filter runs from the plugin the stepd loads for energy, and it
    # reports through the Energy debug flag, so the per-node slurmd log is
    # its channel. No accounting poll is involved, since the stepd runs the
    # filter once at step launch.
    atf.require_config_parameter("AcctGatherEnergyType", "acct_gather_energy/gpu")
    atf.require_config_parameter_includes("DebugFlags", "Energy")
    # Both reports go through log_flag(ENERGY), which prints at the verbose
    # level or above, and the unguarded count only ran there too, so the
    # module needs SlurmdDebug at verbose or higher. A level that already
    # qualifies is kept, a lower one is raised to verbose.
    atf.require_config_parameter(
        "SlurmdDebug", ["verbose", "debug", "debug2", "debug3", "debug4", "debug5"]
    )
    atf.require_tty(0)

    atf.require_nodes(2, [("Gres", "gpu:1"), ("CPUs", 2)])

    # Learn the gres nodes from the written slurm.conf and scope a gres.conf
    # record to each, so every slurmd declaring the gpu gres loads it. The
    # job runs on the first two of them, and a two-node step asking for one
    # GPU leaves exactly one of them without a bitmap, whatever the variant's
    # node count.
    # get_nodes() indexes the NodeName=DEFAULT stanza separately instead of
    # merging it, while require_nodes() above counts a node as qualifying with
    # a gres its DEFAULT stanza supplies, so the DEFAULT parameters have to be
    # folded in here for the two views to agree. require_nodes() takes gpu:1
    # as a floor, while the count line pins "of 1" and the gres.conf written
    # below backs one device per node, so every gpu node has to carry exactly
    # one untyped gpu, or the module skips.
    nodes = atf.get_nodes(live=False, quiet=True)
    default_params = nodes.get("DEFAULT", {})

    def _gpu_count(params):
        merged = dict(default_params)
        merged.update(params)
        match = re.search(
            r"(?:^|,)gpu:(\d+)(?:,|$)",
            str(merged.get("Gres") or merged.get("gres") or ""),
        )
        return int(match.group(1)) if match else 0

    gpu_counts = {
        name: _gpu_count(params)
        for name, params in nodes.items()
        if name != "DEFAULT" and _gpu_count(params)
    }
    all_gpu_nodes = sorted(gpu_counts)
    if len(all_gpu_nodes) < 2:
        pytest.skip(
            f"this test requires two nodes carrying the gpu gres, got {all_gpu_nodes}",
            allow_module_level=True,
        )
    if any(count != 1 for count in gpu_counts.values()):
        pytest.skip(
            f"this test requires every gpu node at exactly Gres=gpu:1, got {gpu_counts}",
            allow_module_level=True,
        )
    gpu_nodes = all_gpu_nodes[:2]
    atf.require_config_file(
        "gres.conf",
        "".join(f"NodeName={node} Name=gpu File=/dev/tty0\n" for node in all_gpu_nodes),
    )
    atf.require_slurm_running()


def _slurmd_log_path(node):
    """The per-node slurmd log file the module reads its reports from.

    SlurmdLogFile holds the real location, and may carry the %n placeholder
    that gives every node of a multiple slurmd host its own log. Auto-config
    leaves SlurmdLogFile unset, so the log directory of the controller is the
    fallback, the same way atf derives it. conftest fills slurm-logs-dir only
    in auto-config mode, so a local config needs that fallback derived here.
    """
    log_file = atf.get_config_parameter(
        "SlurmdLogFile", default=None, live=False, quiet=True
    )
    if log_file:
        return log_file.replace("%n", node)

    if "slurm-logs-dir" not in atf.properties:
        atf.properties["slurm-logs-dir"] = os.path.dirname(
            atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
        )

    return f"{atf.properties['slurm-logs-dir']}/slurmd.{node}.log"


def _slurmd_log_size(node):
    """The current byte size of the node's slurmd log.

    Recorded before an observation window opens, so a later read scans only
    what the daemon appended from there on.
    """
    return int(
        atf.run_command_output(
            f"stat -c %s {_slurmd_log_path(node)}",
            user="root",
            quiet=True,
            fatal=True,
        )
    )


def _slurmd_log_tail(node, offset):
    """The node's slurmd log from a byte offset recorded earlier.

    The daemon logs through an O_APPEND descriptor and a restart reopens the
    same file in append mode, so a recorded-offset tail read sees exactly the
    lines written from that offset on while preserving the archived log.
    """
    return atf.run_command_output(
        f"tail -c +{offset + 1} {_slurmd_log_path(node)}",
        user="root",
        quiet=True,
        fatal=True,
    )


def _wait_for_slurmd_log(node, pattern, offset):
    """Poll the slurmd log from a byte offset for an extended regular expression.

    The grep exit code is the polled condition, so the command is never
    fatal, and the pattern is an extended regular expression whose literal
    periods and brackets the caller escapes. Only what the daemon appended
    from the offset on is scanned, as _slurmd_log_tail describes. The wait is
    bounded and never fails by itself: the tests judge the log they get.
    """
    command = f"tail -c +{offset + 1} {_slurmd_log_path(node)} | grep -E '{pattern}'"
    for _ in atf.timer():
        if atf.run_command(command, user="root", quiet=True)["exit_code"] == 0:
            return


@pytest.fixture(scope="module")
def step_reports(setup):
    """The slurmd log each node appended while one two-node GPU job ran.

    Runs the job once and hands both tests the same observation, since the
    pair of reports is one event: the job asks for a GPU on each node so the
    step has one to take, and the step spans both nodes while asking for a
    single GPU, which is the request that leaves one node bitmap-less. A
    second step asks for no GRES and must log neither report. Returns the
    job's final state and the per-node log text. The tests own every
    assertion, so a regression fails a test instead of erroring here. The
    two report lines are the tests' proof that the crash shape was reached,
    so a reworded report, or a getter that starts handing this node a bitmap
    of its own, has to be mirrored here.
    """
    offsets = {node: _slurmd_log_size(node) for node in gpu_nodes}
    job_id = atf.submit_job_sbatch(
        f"-N2 -w {','.join(gpu_nodes)} --ntasks-per-node=1 --gpus=2 -t1 "
        "-J no_bitmap_energy_step "
        '--wrap "srun -N2 --ntasks-per-node=1 --gpus=1 --mpi=none sleep 10 '
        '&& srun -N2 --ntasks-per-node=1 --gres=none --mpi=none true"',
        fatal=True,
    )
    # The job's own limit is one minute, so a step that hangs at launch, the
    # shape of the fixed crash, ends as TIMEOUT within it. The wait outlives
    # that limit by far, lag of the limit enforcement included, so the job's
    # final state, not this wait, decides the tests.
    atf.wait_for_job_state(job_id, "DONE", timeout=180, fatal=True)
    job_state = atf.get_job_parameter(job_id, "JobState")

    # Each node logs at most one of the two reports for the GPU step, so
    # waiting for either one waits out the write without assuming which node
    # got the GPU. A node that logs neither is left to the tests to judge.
    either = f"{NO_BITMAP_REPORT}|{USABLE_GPUS_REPORT}"
    for node, offset in offsets.items():
        _wait_for_slurmd_log(node, either, offset=offset)
    return {
        "job_state": job_state,
        "text": {
            node: _slurmd_log_tail(node, offset) for node, offset in offsets.items()
        },
    }


def test_bitmapless_node_reports_no_power_gathering(step_reports):
    """The node left without a local bitmap logs the guard and stays up.

    The step spans both nodes and asks for one GPU, so one node resolves the
    step's gpu record with no local bitmap. That node runs the filter at step
    launch, where the unguarded form crashed the stepd, and reports that it
    gathers no power for the step instead.
    """
    # A slurmstepd that dies at launch never answers srun, which hangs until
    # the job's time limit ends it, so a job that completed is the crash
    # regression's contradiction.
    assert step_reports["job_state"] == "COMPLETED", (
        f"the two-node GPU job must complete, it ended "
        f"{step_reports['job_state']}: a slurmstepd crash at step launch leaves "
        f"srun hanging until the time limit"
    )

    reporting = [
        node for node, text in step_reports["text"].items() if NO_BITMAP_REPORT in text
    ]
    assert len(reporting) == 1, (
        f"exactly one node must log the no-bitmap report {NO_BITMAP_REPORT!r}, "
        f"got {reporting!r}"
    )


def test_allocated_node_still_filters_by_bitmap(step_reports):
    """The node holding the GPU keeps its normal bitmap-filtered path.

    The guard must not swallow the healthy form: the node the step's single
    GPU landed on resolves a real bitmap, so the plugin logs the usable count
    line there, and it is the other node of the pair.
    """
    counting = [
        node
        for node, text in step_reports["text"].items()
        if USABLE_GPUS_REPORT in text
    ]
    assert len(counting) == 1, (
        f"exactly one node must log the usable count line {USABLE_GPUS_REPORT!r}, "
        f"got {counting!r}"
    )

    reporting = [
        node for node, text in step_reports["text"].items() if NO_BITMAP_REPORT in text
    ]
    assert (
        counting != reporting
    ), f"the counting node and the no-bitmap node must differ, both are {counting!r}"
