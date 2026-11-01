############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for memory drain and resume via the TresName/Action node options.

Validates that draining node memory marks the node DEGRADED, reduces effective
scheduling capacity without altering configured RealMemory, survives slurmctld
restart, and is fully reversed by Action=Resume.

NODE_A and NODE_B are non-adjacent in the cluster's node bitmap (node1 is never
targeted and node3 sits between them), so spanning allocations
exercise the discontinuous-bitmap code paths in the select plugin.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE_MEM = 2048
DRAIN_MEM = 1536  # leaves 512 MB effective
# require_nodes(4, ...) yields node1..node4. node1 is never targeted, so it
# absorbs the scheduler's first-choice bias, and node3 sits between the two
# drain nodes to keep them non-adjacent in the node bitmap.
NODE_A = "node2"
NODE_B = "node4"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: mem TresName/Action added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: node MaxDrainedTres added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU_Memory")
    # sched_interval and bf_interval default to 60s and 30s. A job that becomes
    # runnable only after another job ends is reconsidered on those cycles, so
    # the "still pending" check below needs them short enough to observe.
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_nodes(4, [("CPUs", 4), ("RealMemory", NODE_MEM)])
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def clean_nodes():
    """Leave both nodes undrained and idle, whatever the test did or failed at.

    Memory drains add to what is already drained and an allocation holds memory
    the next test expects to be free, so a test that ends early otherwise makes
    the rest of the module fail for reasons of its own making.
    """
    yield

    # The wait below needs the jobs gone, and function_setup cancels them only
    # after this teardown.
    atf.cancel_all_jobs(quiet=True)
    for node in (NODE_A, NODE_B):
        atf.update_tres_drain(
            node, "mem", f"Count={NODE_MEM}", action="Resume", quiet=True
        )
        atf.run_command(
            f"scontrol update NodeName={node} State=RESUME",
            user=atf.properties["slurm-user"],
            quiet=True,
        )

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            state = (
                atf.get_node_tres_drained(node),
                atf.get_node_parameter(node, "alloc_memory"),
            )
            if not state[0] and (not state[1]):
                break


@pytest.fixture
def max_drained_tres():
    """Set a node's MaxDrainedTres for one test, and drop it afterwards.

    set_node_parameter() only restores slurm.conf when the module ends, so
    the later tests would still have the limit.
    """
    nodes = []

    def _set(node, spec):
        if node not in nodes:
            nodes.append(node)
        atf.set_node_parameter(node, "MaxDrainedTres", spec)

    yield _set

    for node in nodes:
        atf.set_node_parameter(node, "MaxDrainedTres", None)


def test_mem_drain_reduces_effective_memory_and_resume_restores():
    """Draining memory on NODE_A blocks a spanning 2-node job; NODE_B is unaffected."""
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="mem_drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after memory drain"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if bool(f) and "mem" in f.lower():
            break
    assert (
        int(atf.get_node_parameter(NODE_A, "real_memory")) == NODE_MEM
    ), "configured RealMemory must not change on drain"
    assert "DEGRADED" not in (
        atf.get_node_parameter(NODE_B, "state") or ""
    ), "NODE_B must not be DEGRADED -- drain is per-node"

    # 1024 MB per node on a spanning job: NODE_A only has 512 MB effective -> PENDING.
    job_id = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --mem=1024 --wrap="sleep infinity"'
    )
    assert job_id != 0, "job should be accepted (but stay pending)"
    assert atf.job_stays_pending(
        job_id
    ), "1024 MB spanning job must stay PENDING while NODE_A memory is drained"

    # Resume: NODE_A returns to full 2048 MB and the spanning job can start.
    result = atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if not f or "mem" not in f.lower():
            break
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "spanning job should start once NODE_A memory is resumed"


def test_mem_drain_job_within_remaining_capacity_runs():
    """A job that fits within the remaining undrained memory runs despite the drain."""
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="small-job-drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "node should be DEGRADED after memory drain"

    # 512 MB remains effective; a 256 MB job fits.
    job_id = atf.submit_job_sbatch(f'-w {NODE_A} -n1 --mem=256 --wrap="sleep infinity"')
    assert job_id != 0, "256 MB job submission should succeed"
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "256 MB job should run within the remaining 512 MB of effective memory"

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_mem_drain_persists_across_slurmctld_restart():
    """Memory drain on both NODE_A and NODE_B survives a non-clean slurmctld restart."""
    atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="persist-a", fatal=True
    )
    atf.update_tres_drain(
        NODE_B, "mem", f"Count={DRAIN_MEM}", reason="persist-b", fatal=True
    )
    expected = {
        NODE_A: f"mem:{DRAIN_MEM}(persist-a)",
        NODE_B: f"mem:{DRAIN_MEM}(persist-b)",
    }

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break
        assert (
            int(atf.get_node_parameter(node, "real_memory")) == NODE_MEM
        ), "configured RealMemory should not change"

    atf.restart_slurmctld(clean=False, quiet=True)

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should remain DEGRADED after controller restart"
        assert (
            int(atf.get_node_parameter(node, "real_memory")) == NODE_MEM
        ), "configured RealMemory should not change"

    atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    atf.update_tres_drain(NODE_B, "mem", f"Count={DRAIN_MEM}", action="Resume")


def test_mem_drain_partial_resume_keeps_node_degraded():
    """Per-node partial memory resume: NODE_B's remaining drain keeps spanning job PENDING.

    Both nodes start with 1536 MB drained (512 MB effective each).
    A spanning 768 MB-per-node job cannot start (512 MB < 768 MB on each node).
    Resuming 1024 MB on NODE_A raises it to 1024 MB effective, but NODE_B still has
    only 512 MB -- the job stays PENDING because the per-node drains are independent.
    Resuming 1024 MB on NODE_B too gives both nodes 1024 MB effective and the job runs.
    """
    atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="partial-resume-a", fatal=True
    )
    atf.update_tres_drain(
        NODE_B, "mem", f"Count={DRAIN_MEM}", reason="partial-resume-b", fatal=True
    )

    for node in (NODE_A, NODE_B):
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED with {DRAIN_MEM} MB drained"

    # 512 MB effective per node; a 768 MB spanning job cannot start.
    job_id = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --mem=768 --wrap="sleep infinity"'
    )
    assert job_id != 0, "768 MB spanning job submission should succeed"
    assert atf.job_stays_pending(
        job_id
    ), "768 MB spanning job must stay PENDING with only 512 MB effective per node"

    # Resume 1024 MB on NODE_A: A has 1024 MB effective, B still has 512 MB -> PENDING.
    result = atf.update_tres_drain(NODE_A, "mem", "Count=1024", action="Resume")
    assert result["exit_code"] == 0, result
    assert "partial-resume-a" in (
        atf.get_node_tres_drained(NODE_A) or ""
    ), "partial resume must preserve the reason for memory still drained"

    assert atf.wait_for_node_state(
        NODE_B, "DEGRADED"
    ), "NODE_B should remain DEGRADED while 1536 MB is still drained"

    assert atf.job_stays_pending(job_id), (
        "spanning job must remain PENDING after resuming only NODE_A -- "
        "NODE_B still has only 512 MB effective"
    )

    # Resume 1024 MB on NODE_B too: both nodes now have 1024 MB effective -> job starts.
    result = atf.update_tres_drain(NODE_B, "mem", "Count=1024", action="Resume")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "768 MB spanning job should start once both nodes have 1024 MB effective"

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM - 1024}", action="Resume")
    atf.update_tres_drain(NODE_B, "mem", f"Count={DRAIN_MEM - 1024}", action="Resume")
    for node in (NODE_A, NODE_B):
        atf.wait_for_node_state(node, "DEGRADED", reverse=True)


def test_mem_drain_over_capacity_accepted_and_capped():
    """A drain for more than RealMemory is accepted and capped at RealMemory."""
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={NODE_MEM + 1024}", reason="over-cap-drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "node should be DEGRADED after over-capacity memory drain"
    assert (
        int(atf.get_node_parameter(NODE_A, "real_memory")) == NODE_MEM
    ), "configured RealMemory must not change on over-capacity drain"
    drained = atf.get_node_tres_drained(NODE_A) or ""
    assert f"mem:{NODE_MEM}(over-cap-drain)" in drained, drained

    # Resuming the same over-limit amount clears the drain (implementation caps
    # internally, so resuming >= the cap is always sufficient to clear).
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={NODE_MEM + 1024}", action="Resume"
    )
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if not f or "mem" not in f.lower():
            break


def test_mem_drain_scheduler_does_not_reclaim_drained_memory():
    """Drained memory is never recycled by the scheduler as running jobs finish.

    Memory drain is a permanent phantom allocation in the select plugin.
    When a job that consumed all undrained memory completes, the freed real
    memory must NOT allow a job that exceeds the undrained capacity to start.
    This guards against a backfill regression where simulated job removal could
    expose drained memory as schedulable capacity.
    """
    # Drain 1536 MB -> 512 MB of effective memory remains on the 2048 MB node.
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="backfill-drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "node should be DEGRADED after memory drain"

    # Fill the remaining 512 MB with a short-lived job (-t 1 caps it at 1 min;
    # the sleep ensures it stays RUNNING long enough to also submit the next job).
    blocker_id = atf.submit_job_sbatch(
        f'-w {NODE_A} -n1 --mem=512 -t 1 --wrap="sleep 20"'
    )
    assert blocker_id != 0, "blocker job submission should succeed"
    assert atf.wait_for_job_state(
        blocker_id, "RUNNING"
    ), "512 MB blocker should start -- 512 MB fits in remaining undrained memory"

    # Submit a job that needs 1024 MB. Even after the blocker finishes and
    # releases its 512 MB, only 512 MB of real memory is undrained; this job
    # must never fit until the drain is explicitly resumed.
    oversized_id = atf.submit_job_sbatch(
        f'-w {NODE_A} -n1 --mem=1024 --wrap="sleep infinity"'
    )
    assert oversized_id != 0, "oversized job submission should succeed"
    assert atf.job_stays_pending(
        oversized_id
    ), "1024 MB job must stay PENDING while the blocker holds all undrained memory"

    # Wait for the blocker to complete.
    assert atf.wait_for_job_state(
        blocker_id, "DONE", timeout=90
    ), "blocker job should complete within 90 s"

    # sched_interval and bf_interval are 1s (see setup). Watching for 10s covers
    # several main-scheduler and backfill cycles after the blocker ends. The
    # 1024 MB job must stay PENDING: the drain is not real memory the scheduler
    # may hand out once the blocker releases its share.
    assert atf.job_stays_pending(oversized_id, timeout=10), (
        f"1024 MB job must remain PENDING after blocker completes; "
        f"drained memory ({DRAIN_MEM} MB) must not be recycled by the "
        "scheduler as real jobs release their share"
    )

    # Resuming the drain releases 1536 MB; now 2048 MB is fully available.
    result = atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_job_state(
        oversized_id, "RUNNING"
    ), "1024 MB job should start once the drain is resumed"

    atf.cancel_all_jobs(quiet=True)
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_mem_drain_exclusive_mem_zero_gets_only_undrained_memory():
    """--exclusive --mem=0 on a drained node runs and is allocated only undrained memory.

    --mem=0 triggers the "allocate all node memory" branch in the select plugin.
    That branch computes needed_mem = avail_mem - mem_drain so that the job
    receives only the undrained portion.  The alloc_memory > 0 guard that blocks
    the placement when a real job is already running must NOT fire on an idle
    drained node (mem_drain is a separate field, not part of alloc_memory).

    This test verifies both invariants: the job runs and is granted exactly
    NODE_MEM - DRAIN_MEM = 512 MB, not the full 2048 MB.
    """
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="excl-mem0-drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after memory drain"

    # --mem=0 requests all available node memory; drain must not cause rejection.
    job_id = atf.submit_job_sbatch(
        f'--exclusive -w {NODE_A} -n1 --mem=0 --wrap="sleep infinity"'
    )
    assert job_id != 0, "--mem=0 exclusive job should be accepted"
    assert atf.wait_for_job_state(job_id, "RUNNING"), (
        "--mem=0 exclusive job must run on a drained node "
        "(needed_mem = avail_mem - mem_drain; alloc_memory is clean)"
    )

    # Confirm the allocation is the undrained portion, not the full node.
    alloc_tres = atf.get_job_parameter(job_id, "AllocTRES", quiet=True)
    m = re.search(r"\bmem=(\d+)([MG])\b", alloc_tres or "")
    assert m, f"job has no allocated mem TRES: {alloc_tres!r}"
    alloc_mb = int(m.group(1)) * (1024 if m.group(2) == "G" else 1)
    expected_mb = NODE_MEM - DRAIN_MEM  # 512 MB
    assert alloc_mb == expected_mb, (
        f"exclusive --mem=0 job allocated {alloc_mb} MB; "
        f"expected {expected_mb} MB ({NODE_MEM} MB node with "
        f"{DRAIN_MEM} MB drained)"
    )

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_mem_drain_multinode_spanning_job():
    """Memory drain on one node of a discontinuous pair blocks a spanning job that needs more.

    NODE_A and NODE_B are non-adjacent, so allocations across them exercise
    discontinuous node-bitmap paths. Draining 1536 MB on NODE_A leaves 512 MB
    effective there; NODE_B is unaffected (full 2048 MB).

    A per-node 512 MB request fits on both nodes and must run.
    A per-node 1024 MB request cannot fit on NODE_A and must stay PENDING until
    the drain is resumed.
    """
    result = atf.update_tres_drain(
        NODE_A, "mem", f"Count={DRAIN_MEM}", reason="multinode-drain"
    )
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after memory drain"
    assert "DEGRADED" not in (
        atf.get_node_parameter(NODE_B, "state") or ""
    ), "NODE_B must not be DEGRADED -- drain is per-node"

    # 512 MB per node fits: NODE_A has 512 MB effective, NODE_B has 2048 MB.
    job_fit = atf.submit_job_sbatch(
        f"--nodes=2 -w {NODE_A},{NODE_B} --mem=512 --wrap='sleep infinity'"
    )
    assert job_fit != 0, "512 MB spanning job submission should succeed"
    assert atf.wait_for_job_state(
        job_fit, "RUNNING"
    ), "512 MB spanning job should run: fits within NODE_A's 512 MB effective memory"

    # 1024 MB per node: NODE_A only has 512 MB effective -> spanning job PENDING.
    job_over = atf.submit_job_sbatch(
        f"--nodes=2 -w {NODE_A},{NODE_B} --mem=1024 --wrap='sleep infinity'"
    )
    assert job_over != 0, "1024 MB spanning job submission should succeed"
    assert atf.job_stays_pending(
        job_over
    ), "1024 MB spanning job must stay PENDING: NODE_A has only 512 MB effective"

    # Resume drain: NODE_A returns to full 2048 MB -> oversized job starts.
    result = atf.update_tres_drain(NODE_A, "mem", f"Count={DRAIN_MEM}", action="Resume")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_job_state(
        job_over, "RUNNING"
    ), "1024 MB spanning job should start once NODE_A memory drain is resumed"

    atf.cancel_all_jobs(quiet=True)
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_mem_set_drain_is_idempotent():
    """Action=SetDrain names the drained amount, so repeating it changes nothing.

    A count is where SetDrain and Drain differ: Drain adds to what is already
    drained, while SetDrain replaces it. That makes SetDrain safe to re-run from
    configuration management, and lets one request lower a drain as well as
    raise it.
    """
    assert (
        atf.update_tres_drain(
            NODE_A, "mem", "Count=512", action="SetDrain", reason="set512"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if "mem:512(set512)" in f:
            break
    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED while memory is drained"

    # Repeating the same request is a no-op.
    before = atf.get_node_tres_drained(NODE_A)
    assert (
        atf.update_tres_drain(
            NODE_A, "mem", "Count=512", action="SetDrain", reason="set512"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    assert atf.get_node_tres_drained(NODE_A) == before, atf.get_node_detail(NODE_A)

    # Action=Drain is cumulative, which is what makes the above meaningful.
    assert (
        atf.update_tres_drain(NODE_A, "mem", "Count=512", reason="add512")["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if "mem:1024(add512)" in f:
            break

    # SetDrain can lower the drain again, which Drain cannot.
    assert (
        atf.update_tres_drain(
            NODE_A, "mem", "Count=768", action="SetDrain", reason="set768"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if "mem:768(set768)" in f:
            break

    # Count=0 names an empty drained set, resuming everything.
    assert (
        atf.update_tres_drain(
            NODE_A, "mem", "Count=0", action="SetDrain", reason="none"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if f is None:
            break
    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED", reverse=True
    ), "NODE_A should stop being DEGRADED once nothing is drained"


def test_mem_max_drained_tres_over_limit_drains_node(max_drained_tres):
    """MaxDrainedTres=mem:<MB> auto-drains the node once drained memory exceeds it.

    Draining memory at or below the limit only marks the node DEGRADED; draining
    past the limit drains the node itself (recoverable via ``scontrol update
    State=RESUME``), leaving a reason that names the mem breach. MaxDrainedTres
    is node-local: NODE_B, which has no limit, stays merely DEGRADED even with
    more memory drained.
    """
    limit_mb = 1000
    max_drained_tres(NODE_A, f"mem:{limit_mb}")
    assert (
        atf.get_node_parameter(NODE_A, "max_drained_tres") == f"mem:{limit_mb}"
    ), "JSON max_drained_tres should be the configured limit"
    assert (
        atf.get_node_field(NODE_A, "MaxDrainedTres") == f"mem:{limit_mb}"
    ), "scontrol show node MaxDrainedTres should be the configured limit"
    assert not atf.get_node_parameter(
        NODE_B, "max_drained_tres"
    ), "JSON max_drained_tres should be unset on a node with no limit"
    assert (
        atf.get_node_field(NODE_B, "MaxDrainedTres") is None
    ), "scontrol show node should omit MaxDrainedTres on a node with no limit"
    # Drain exactly the limit -> DEGRADED, not DRAIN.
    assert (
        atf.update_tres_drain(
            NODE_A, "mem", f"Count={limit_mb}", reason="within_limit"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        s = atf.get_node_parameter(NODE_A, "state") or ""
        if "DEGRADED" in s and "DRAIN" not in s:
            break

    # Drain 100 MB more (1100 > limit of 1000) -> whole-node DRAIN.
    assert (
        atf.update_tres_drain(NODE_A, "mem", "Count=100", reason="over_limit")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(NODE_A, "DRAIN"), (
        "NODE_A should DRAIN once drained memory (1100) exceeds "
        f"MaxDrainedTres ({limit_mb}): "
        f"{atf.get_node_parameter(NODE_A, 'state')!r}"
    )
    assert (
        atf.get_node_parameter(NODE_A, "reason")
        == "mem has 1100 drained, exceeding MaxDrainedTres limit of 1000"
    ), atf.get_node_parameter(NODE_A, "reason")

    # The limit is node-local: NODE_B (no limit) never auto-drains, even
    # when far more memory is drained than NODE_A's limit.
    assert (
        atf.update_tres_drain(NODE_B, "mem", "Count=2000", reason="nodeb")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        NODE_B, "DEGRADED"
    ), "NODE_B should be DEGRADED after memory drain"
    assert "DRAIN" not in (
        atf.get_node_parameter(NODE_B, "state") or ""
    ), "NODE_B must not DRAIN -- it has no MaxDrainedTres limit"


def test_tresname_memory_is_reported_as_mem():
    """TresName=memory is the mem alias and is reported as mem:."""
    result = atf.update_tres_drain(
        NODE_A, "memory", f"Count={DRAIN_MEM}", reason="alias"
    )
    assert result["exit_code"] == 0, result
    drained = ""
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE_A) or ""
        if f"mem:{DRAIN_MEM}(alias)" in drained:
            break
    assert f"mem:{DRAIN_MEM}(alias)" in drained, drained
