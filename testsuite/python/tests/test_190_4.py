############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Tests for CPU drain and resume via the TresName/Action node options.

Validates that draining individual CPU indices marks a node DEGRADED, reduces
scheduling capacity, survives slurmctld restart, and is fully reversed by
Action=Resume.

NODE_A and NODE_B are non-adjacent in the cluster's node bitmap (node1 is never
targeted and NODE_GOOD sits between them), so spanning allocations
exercise the discontinuous-bitmap code paths in the select plugin. NODE_SMT
schedules two threads per core, so a drain names one thread and removes the
whole core. NODE_SOCKETS has as many CPUs as sockets, so a CPU names a core
and there are more cores than CPUs.

Some tests here guard against a specific control-plane failure rather than
check a behaviour:

  - a resume naming CPUs that were never drained is a no-op, and must not leave
    the node an empty drain map with no reasons beside it, which the next state
    save would walk
  - a request naming a node that does not exist must apply to no node, rather
    than drain the rest of the hostlist and report only a failure, which leaves
    the admin unable to tell what was applied
  - every field, not only the TRES selectors, must be refused when given twice,
    rather than a repeated TresName= or Action= silently taking the last one
  - a MaxDrainedTres the parser cannot read is read as "no limit", so it must
    be reported rather than silently disable the safety limit
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# require_nodes(4, ...) yields node1..node4. node1 is never targeted, so it
# absorbs the scheduler's first-choice bias, and NODE_GOOD sits between the two
# drain nodes to keep them non-adjacent in the node bitmap.
NODE_A = "node2"
NODE_GOOD = "node3"
NODE_B = "node4"
# Filled in by setup(); an existing node may not have this topology.
NODE_SMT = None
NODE_SOCKETS = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="bin/scontrol",
        reason="Ticket 25180: cpu TresName/Action added in 26.11",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="Ticket 25180: node MaxDrainedTres added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(4, [("CPUs", 4)])
    global NODE_SMT
    # 1 socket, 2 cores, 2 threads: CPUs 0-1 are core 0, CPUs 2-3 are core 1.
    NODE_SMT = atf.require_nodes(
        1,
        [
            ("Sockets", 1),
            ("CoresPerSocket", 2),
            ("ThreadsPerCore", 2),
            ("CPUs", 4),
        ],
    )[0]
    global NODE_SOCKETS
    # 2 sockets of 4 cores, configured with one CPU per socket.
    NODE_SOCKETS = atf.require_nodes(
        1,
        [
            ("Sockets", 2),
            ("CoresPerSocket", 4),
            ("ThreadsPerCore", 1),
            ("CPUs", 2),
        ],
    )[0]
    atf.require_slurm_running()


def _update_tres_raw(*args):
    """Run "scontrol update" with the fields exactly as given.

    atf.update_tres_drain() builds a well-formed command, so it cannot express
    a field supplied twice.
    """
    return atf.run_command(
        f"scontrol update {' '.join(args)}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _drained_cpus(node):
    """The node's DrainedTRES CPU text, or "" when nothing is drained."""
    return atf.get_node_tres_drained(node) or ""


@pytest.fixture(autouse=True)
def resume_everything():
    """Leave every node undrained and resumed, so each test starts clean.

    State=RESUME also returns a node that a MaxDrainedTres breach or the test
    drained as a whole.
    """
    yield
    for node in (NODE_A, NODE_B, NODE_GOOD, NODE_SMT, NODE_SOCKETS):
        if node:
            atf.update_tres_drain(node, "cpu", "Index=ALL", action="Resume", quiet=True)
            # Not fatal: a node that is not drained refuses the transition.
            atf.run_command(
                f"scontrol update NodeName={node} State=RESUME Reason=cleanup",
                user=atf.properties["slurm-user"],
                quiet=True,
            )


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


@pytest.fixture
def clear_triggers():
    """Clear the slurm user's triggers after the test."""
    yield
    atf.run_command(
        f"strigger --clear --user={atf.properties['slurm-user']}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_cpu_drain_reduces_capacity_and_resume_restores():
    """Draining one CPU on NODE_A degrades it and blocks a spanning 2-node job."""
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="cpu_drain")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after CPU drain"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if bool(f) and "cpu" in f.lower():
            break
    assert "DEGRADED" not in (
        atf.get_node_parameter(NODE_B, "state") or ""
    ), "NODE_B must not be DEGRADED -- drain is per-node"

    # 4 tasks per node needs all 4 CPUs on NODE_A, but IDX:0 is drained -> PENDING.
    job_id = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=4 --wrap="sleep infinity"'
    )
    assert job_id != 0, "job should be accepted (but stay pending)"
    assert atf.job_stays_pending(
        job_id
    ), "4-tasks-per-node job must stay PENDING while a CPU is drained on NODE_A"

    # Resume: NODE_A returns to 4 CPUs and the spanning job can start.
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A)
        if not f or "cpu" not in f.lower():
            break
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "spanning job should start once the CPU is resumed"


def test_cpu_drain_allows_already_running_job():
    """A spanning job running before a CPU drain on NODE_A continues running after."""
    job_id = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=4 --wrap="sleep infinity"'
    )
    assert job_id != 0, "job submission should succeed"
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "4-tasks-per-node spanning job should reach RUNNING before drain"

    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="running-job-drain")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after CPU drain"

    job_state = atf.get_job_parameter(job_id, "JobState", quiet=True)
    assert (
        job_state == "RUNNING"
    ), "spanning job should still be RUNNING after CPU drain"

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_cpu_drain_new_job_avoids_drained_cpu():
    """Draining CPU 0 on NODE_A: a 3-tasks-per-node spanning job runs; 4-per-node stays PENDING.

    On NODE_SMT the same index drains both threads of the core, so a 2-task
    job still fits and a 3-task job does not. Resuming the other thread
    clears the core.
    """
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="avoidance-drain")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after CPU drain"

    job3 = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=3 --wrap="sleep infinity"'
    )
    assert job3 != 0, "3-tasks-per-node job submission should succeed"
    assert atf.wait_for_job_state(
        job3, "RUNNING"
    ), "3-tasks-per-node job should run on 3 undrained CPUs on NODE_A"

    job4 = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=4 --wrap="sleep infinity"'
    )
    assert job4 != 0, "4-tasks-per-node job submission should succeed"
    assert atf.job_stays_pending(
        job4
    ), "4-tasks-per-node job must stay PENDING while CPU 0 is drained on NODE_A"

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    # After resume NODE_A is back to 4 CPUs; NODE_B was never affected.
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)

    # NODE_SMT has two threads per core. Draining CPU 0 drains its sibling
    # too, so the node keeps 2 CPUs: a 3-task job stays pending and a 2-task
    # job runs. Resuming the other thread clears the core.
    result = atf.update_tres_drain(NODE_SMT, "cpu", "Index=0", reason="core0")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_SMT) or ""
        if f == "cpu(IDX:0-1)(core0)":
            break
    assert atf.wait_for_node_state(
        NODE_SMT, "DEGRADED"
    ), "NODE_SMT should reach DEGRADED"

    # --nodes=1: without a node limit the scheduler adds another node to fit
    # the third task, and the job runs without needing the drained core.
    job3 = atf.submit_job_sbatch(
        f'--nodes=1 --nodelist={NODE_SMT} --ntasks=3 --wrap="sleep infinity"'
    )
    assert job3 != 0, "3-CPU job submission should succeed"
    assert atf.job_stays_pending(
        job3
    ), "3-CPU job must stay PENDING while one core (2 CPUs) is drained"
    for _ in atf.timer(fatal=True):
        if atf.get_job_parameter(job3, "Reason", quiet=True) == "Resources":
            break

    job2 = atf.submit_job_sbatch(
        f'--nodes=1 --nodelist={NODE_SMT} --ntasks=2 --wrap="sleep infinity"'
    )
    assert job2 != 0, "2-CPU job submission should succeed"
    assert atf.wait_for_job_state(
        job2, "RUNNING"
    ), "2-CPU job should run on the undrained core"

    atf.cancel_all_jobs(quiet=True)
    result = atf.update_tres_drain(NODE_SMT, "cpu", "Index=1", action="Resume")
    assert result["exit_code"] == 0, result
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_SMT)
        if f is None:
            break


def test_cpu_drain_range_and_partial_resume():
    """Drain CPUs 0-1 on both nodes; per-node partial resume gates the spanning job.

    Each node has 2 undrained CPUs. A 3-tasks-per-node job stays PENDING.
    Resuming IDX:0 on NODE_A only raises NODE_A to 3 CPUs, but NODE_B still has 2 --
    the job stays PENDING because the per-node drain bitmaps are independent.
    Resuming IDX:0 on NODE_B too gives both nodes 3 CPUs and the job starts.
    """
    atf.update_tres_drain(
        NODE_A, "cpu", "Index=0-1", reason="range-drain-a", fatal=True
    )
    atf.update_tres_drain(
        NODE_B, "cpu", "Index=0-1", reason="range-drain-b", fatal=True
    )

    for node in (NODE_A, NODE_B):
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after draining CPUs 0-1"

    # Each node has 2 undrained CPUs; job needs 3 per node -> PENDING.
    job = atf.submit_job_sbatch(
        f'--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=3 --wrap="sleep infinity"'
    )
    assert job != 0, "3-tasks-per-node job submission should succeed"
    assert atf.job_stays_pending(
        job
    ), "job must stay PENDING when only 2 CPUs are undrained per node"

    # Resume IDX:0 on NODE_A only: A has 3 CPUs, B still has 2 -> still PENDING.
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    assert atf.wait_for_node_state(
        NODE_B, "DEGRADED"
    ), "NODE_B should remain DEGRADED while IDX:0-1 are still drained"

    assert atf.job_stays_pending(
        job
    ), "job must remain PENDING after resuming only NODE_A IDX:0 -- NODE_B still has 2 CPUs"

    # Resume IDX:0 on NODE_B: both nodes now have 3 undrained CPUs -> job starts.
    atf.update_tres_drain(NODE_B, "cpu", "Index=0", action="Resume")
    assert atf.wait_for_job_state(
        job, "RUNNING"
    ), "job should start once both nodes have 3 undrained CPUs"

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "cpu", "Index=1", action="Resume")
    atf.update_tres_drain(NODE_B, "cpu", "Index=1", action="Resume")
    for node in (NODE_A, NODE_B):
        atf.wait_for_node_state(node, "DEGRADED", reverse=True)


def test_cpu_drain_refused_for_non_admin():
    """Only an administrator can drain a TRES."""
    result = atf.update_tres_drain(
        NODE_A,
        "cpu",
        "Index=0",
        reason="not_admin",
        user=atf.properties["test-user"],
    )
    assert result["exit_code"] != 0, "a non-admin drain should be refused"
    assert "Invalid user id" in result["stderr"], result

    drained = atf.get_node_tres_drained(NODE_A)
    assert drained is None, f"DrainedTRES should be unset; got {drained!r}"


def test_refused_tres_update_has_no_side_effects(clear_triggers):
    """A refused TRES update does not run the follow-up an applied one does.

    An applied update saves node state, queues the scheduler and fires the
    reconfigure triggers. A refused request must do none of that, whether it
    was refused for its user or failed in the controller. Its reconfig
    trigger is the side effect a test can see, so set one, check that the
    refused requests leave it alone, and check that an admin's update fires
    it, so a trigger that never fires cannot pass the first half.
    """
    flag_dir = atf.module_tmp_path / "reconfig_trigger"
    flag_dir.mkdir(exist_ok=True)
    flag_dir.chmod(0o777)
    flag_file = flag_dir / "fired"
    trigger_script = atf.module_tmp_path / "reconfig_trigger.sh"
    atf.make_bash_script(trigger_script, f"touch {flag_file}")
    slurm_user = atf.properties["slurm-user"]

    # Permanent, so a reconfigure still pending from an earlier test can fire
    # it without using it up. Let that happen, then start from no flag file.
    atf.run_command(
        f"strigger --set --reconfig --flags=perm --program={trigger_script}",
        user=slurm_user,
        fatal=True,
    )
    for _ in atf.timer(timeout=35, xfail=True, fatal=True):
        fired = flag_file.exists()
        if fired:
            break
    flag_file.unlink(missing_ok=True)

    result = atf.update_tres_drain(
        NODE_A,
        "cpu",
        "Index=0",
        reason="not_admin",
        user=atf.properties["test-user"],
    )
    assert result["exit_code"] != 0, "a non-admin drain should be refused"
    assert "Invalid user id" in result["stderr"], result

    # An admin's request the controller refuses, such as a mistyped node
    # name, changed nothing either.
    result = _update_tres_raw(
        "TresName=cpu",
        "NodeName=nosuchnode",
        "Index=0",
        "Action=Drain",
        'Reason="no_node"',
    )
    assert result["exit_code"] != 0, "a drain of an unknown node should fail"
    assert "Invalid node name specified" in result["stderr"], result

    # Triggers are processed every 15 seconds, so give it two passes.
    # A timeout is the pass: the refused update must not fire the trigger.
    fired = False
    for _ in atf.timer(timeout=35, xfail=True):
        fired = flag_file.exists()
        if fired:
            break
    assert not fired, "a refused TRES update fired the reconfig trigger"

    atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="admin", fatal=True)
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume", fatal=True)
    for _ in atf.timer(fatal=True):
        fired = flag_file.exists()
        if fired:
            break


def test_cpu_drain_out_of_range_index_rejected():
    """A drain with a CPU index beyond the node's CPU count is rejected."""
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=99", reason="oor-drain")
    assert result["exit_code"] != 0, "expected non-zero exit for out-of-range CPU index"
    assert "CPU count specification invalid" in result["stderr"], result

    drained = atf.get_node_tres_drained(NODE_A)
    assert drained is None, f"DrainedTRES should be unset; got {drained!r}"


def test_cpu_drain_persists_across_slurmctld_restart():
    """CPU drain on NODE_A, NODE_B, and a threaded core survives a restart.

    NODE_SMT was drained by one thread of its second core, so the restored
    report is still that core's CPU range.
    """
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="persist-a", fatal=True)
    atf.update_tres_drain(NODE_B, "cpu", "Index=0", reason="persist-b", fatal=True)
    # Index=2 names one thread of the second core; the saved range is both.
    atf.update_tres_drain(NODE_SMT, "cpu", "Index=2", reason="persist", fatal=True)
    expected = {
        NODE_A: "cpu(IDX:0)(persist-a)",
        NODE_B: "cpu(IDX:0)(persist-b)",
    }

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break

    atf.restart_slurmctld(clean=False, quiet=True)

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            if atf.get_node_tres_drained(node) == expected[node]:
                break
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should remain DEGRADED after controller restart"

    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_SMT) or ""
        if f == "cpu(IDX:2-3)(persist)":
            break
    assert atf.wait_for_node_state(
        NODE_SMT, "DEGRADED"
    ), "NODE_SMT should reach DEGRADED"

    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    atf.update_tres_drain(NODE_B, "cpu", "Index=0", action="Resume")
    atf.update_tres_drain(NODE_SMT, "cpu", "Index=2", action="Resume")


def test_cpu_noop_resume_survives_restart():
    """Resuming CPUs that were never drained must not break the state save.

    The resume is a no-op, and must not allocate the node's drain map on the
    way in and leave it behind empty. Saving state would then pack one reason
    per CPU from an array that was never allocated, taking slurmctld down.
    """
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    assert result["exit_code"] == 0, result
    assert not _drained_cpus(NODE_A), "a no-op resume must not drain anything"

    # The restart is the assertion: it writes state on the way down and reads
    # it on the way up, so a controller that survives both proves the save.
    atf.restart_slurmctld(clean=False, quiet=True)

    assert atf.wait_for_node_state(
        NODE_A, "IDLE"
    ), "NODE_A should still be IDLE after the restart"
    assert not _drained_cpus(NODE_A), "nothing should be drained after the restart"


def test_cpu_last_resume_survives_restart():
    """Draining then fully resuming leaves no drain map behind.

    Same failure as the no-op resume, reached the other way: the map is real
    while a CPU is drained and must be dropped once the last one is resumed.
    """
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="regression")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = _drained_cpus(NODE_A)
        if "cpu" in f.lower():
            break

    result = atf.update_tres_drain(NODE_A, "cpu", "Index=ALL", action="Resume")
    assert result["exit_code"] == 0, result
    assert not _drained_cpus(NODE_A), "the resume should clear the drain"

    atf.restart_slurmctld(clean=False, quiet=True)

    assert atf.wait_for_node_state(
        NODE_A, "IDLE"
    ), "NODE_A should be IDLE after resuming everything and restarting"
    assert not _drained_cpus(NODE_A), "node should have no drained CPUs"


def test_cpu_drain_exclusive_job_respects_drain():
    """An exclusive job on a drained node is allocated only undrained CPUs.

    Mirrors the single-node GRES exclusive test pattern: drain IDX:0, submit
    one exclusive job (no competing higher-priority exclusive job on the same
    node), verify it runs and its TRES allocation shows 3 CPUs, not 4.
    """
    result = atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="excl-drain")
    assert result["exit_code"] == 0, result

    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED"
    ), "NODE_A should be DEGRADED after CPU drain"

    # Exclusive job with 3 tasks: fits on the 3 undrained CPUs and must run.
    job_id = atf.submit_job_sbatch(
        f'--exclusive -w {NODE_A} --ntasks=3 --wrap="sleep infinity"'
    )
    assert job_id != 0, "exclusive 3-task job submission should succeed"
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "exclusive 3-task job should run on the 3 undrained CPUs"

    # TRES allocation must show 3 CPUs -- drained IDX:0 excluded.
    alloc_tres = atf.get_job_parameter(job_id, "AllocTRES", quiet=True)
    m = re.search(r"\bcpu=(\d+)\b", alloc_tres or "")
    assert m, f"job has no allocated cpu TRES: {alloc_tres!r}"
    assert int(m.group(1)) == 3, (
        f"exclusive job allocated {m.group(1)} CPUs; "
        f"expected 3 (drained IDX:0 must be excluded from the TRES allocation)"
    )

    atf.cancel_all_jobs(quiet=True)
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    atf.wait_for_node_state(NODE_A, "DEGRADED", reverse=True)


def test_cpu_drain_independent_drain_per_node():
    """CPU drains on two non-adjacent nodes are tracked independently.

    Different CPU indices are drained on NODE_A (IDX:0) and NODE_B (IDX:1).
    A 4-tasks-per-node spanning job stays PENDING until both drains are cleared.
    Resuming only one node is not sufficient -- the per-node bitmaps are independent.
    """
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="indep-drain-a", fatal=True)
    atf.update_tres_drain(NODE_B, "cpu", "Index=1", reason="indep-drain-b", fatal=True)

    for node in (NODE_A, NODE_B):
        assert atf.wait_for_node_state(
            node, "DEGRADED"
        ), f"{node} should be DEGRADED after CPU drain"

    job_id = atf.submit_job_sbatch(
        f"--nodes=2 -w {NODE_A},{NODE_B} --ntasks-per-node=4 --wrap='sleep infinity'",
    )
    assert job_id != 0, "job submission should succeed"
    assert atf.job_stays_pending(
        job_id
    ), "4-tasks-per-node job must stay PENDING: each node has only 3 undrained CPUs"

    # Resume only NODE_A; NODE_B still has IDX:1 drained -- job must stay PENDING.
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", action="Resume")
    assert atf.wait_for_node_state(
        NODE_A, "DEGRADED", reverse=True
    ), "NODE_A should leave DEGRADED after resume"

    assert atf.job_stays_pending(job_id), (
        "job must remain PENDING after resuming only NODE_A; "
        "NODE_B still has IDX:1 drained"
    )

    # Resume NODE_B: both nodes now have 4 undrained CPUs -- job starts.
    atf.update_tres_drain(NODE_B, "cpu", "Index=1", action="Resume")
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "job should start after both per-node drains are resumed"

    atf.cancel_all_jobs(quiet=True)
    atf.wait_for_node_state(NODE_B, "DEGRADED", reverse=True)


def test_cpu_drain_unknown_node_changes_nothing():
    """A hostlist naming a node that does not exist applies to no node.

    Applying it to the nodes that did resolve while reporting a failure would
    leave the admin unable to tell what had happened.
    """
    result = _update_tres_raw(
        "TresName=cpu",
        f"NodeName={NODE_A},nosuchnode",
        "Index=0",
        "Action=Drain",
        'Reason="should_not_apply"',
    )
    assert result["exit_code"] != 0, "an unknown node must fail the request"
    assert "Invalid node name specified" in result["stderr"], result
    assert not _drained_cpus(
        NODE_A
    ), "no node may be drained when another node in the list is unknown"


def test_cpu_drain_valid_hostlist_applies_to_every_node():
    """The atomic check must not stop a hostlist naming only real nodes."""
    result = _update_tres_raw(
        "TresName=cpu",
        f"NodeName={NODE_A},{NODE_B}",
        "Index=0",
        "Action=Drain",
        'Reason="multi_node"',
    )
    assert result["exit_code"] == 0, result

    for node in (NODE_A, NODE_B):
        for _ in atf.timer(fatal=True):
            f = _drained_cpus(node)
            if "cpu" in f.lower():
                break


def test_cpu_max_drained_tres_over_limit_drains_node(max_drained_tres):
    """MaxDrainedTres=cpu:N auto-drains the node once drained CPUs exceed N.

    Draining CPUs at or below the limit only marks the node DEGRADED; draining
    past the limit drains the node itself (recoverable via ``scontrol update
    State=RESUME``), leaving a reason that names the cpu breach. MaxDrainedTres
    is node-local: NODE_B, which has no limit, stays merely DEGRADED even with
    more CPUs drained. On NODE_SMT the same limit counts both threads of a
    core, so one core is 2 CPUs and two cores are 4.
    """
    max_drained_tres(NODE_A, "cpu:1")
    # 1 CPU drained == limit -> DEGRADED, not DRAIN.
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="within_limit")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        s = atf.get_node_parameter(NODE_A, "state") or ""
        if "DEGRADED" in s and "DRAIN" not in s:
            break

    # 2 CPUs drained > limit of 1 -> whole-node DRAIN.
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=1", reason="over_limit")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(NODE_A, "DRAIN"), (
        "NODE_A should DRAIN once drained CPUs (2) exceed MaxDrainedTres (1): "
        f"{atf.get_node_parameter(NODE_A, 'state')!r}"
    )
    assert (
        atf.get_node_parameter(NODE_A, "reason")
        == "cpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    ), atf.get_node_parameter(NODE_A, "reason")

    # The limit is node-local: NODE_B (no limit) never auto-drains, even
    # with more CPUs drained than NODE_A's limit.
    assert (
        atf.update_tres_drain(NODE_B, "cpu", "Index=0-1", reason="nodeb")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(
        NODE_B, "DEGRADED"
    ), "NODE_B should be DEGRADED after CPU drain"
    assert "DRAIN" not in (
        atf.get_node_parameter(NODE_B, "state") or ""
    ), "NODE_B must not DRAIN -- it has no MaxDrainedTres limit"

    # The limit counts threads. One core on NODE_SMT is 2 CPUs, which
    # equals cpu:2 and leaves the node DEGRADED; the second core is 4.
    max_drained_tres(NODE_SMT, "cpu:2")
    assert (
        atf.update_tres_drain(NODE_SMT, "cpu", "Index=0", reason="within_limit")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        s = atf.get_node_parameter(NODE_SMT, "state") or ""
        if "DEGRADED" in s and "DRAIN" not in s:
            break
    assert (
        atf.update_tres_drain(NODE_SMT, "cpu", "Index=2", reason="over_limit")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert atf.wait_for_node_state(NODE_SMT, "DRAIN"), (
        "two cores (4 CPUs) exceed MaxDrainedTres cpu:2: "
        f"{atf.get_node_parameter(NODE_SMT, 'state')!r}"
    )
    assert (
        atf.get_node_parameter(NODE_SMT, "reason")
        == "cpu has 4 drained, exceeding MaxDrainedTres limit of 2"
    ), atf.get_node_parameter(NODE_SMT, "reason")


def test_cpu_max_drained_tres_preserves_existing_drain_reason(max_drained_tres):
    """A later MaxDrainedTres breach does not erase why the node was drained."""
    max_drained_tres(NODE_A, "cpu:1")
    atf.run_command(
        f"scontrol update NodeName={NODE_A} State=DRAIN Reason=manual_drain",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.update_tres_drain(NODE_A, "cpu", "Index=0-1", reason="cpu_fault", fatal=True)
    for _ in atf.timer(fatal=True):
        drained = atf.get_node_tres_drained(NODE_A) or ""
        if drained == "cpu(IDX:0-1)(cpu_fault)":
            break
    assert (
        atf.get_node_parameter(NODE_A, "reason") == "manual_drain"
    ), "MaxDrainedTres must preserve the existing whole-node drain reason"
    assert "DRAIN" in (
        atf.get_node_parameter(NODE_A, "state") or ""
    ), atf.get_node_detail(NODE_A)


def _set_state(node, state):
    """Run "scontrol update NodeName=node State=state" as the slurm user."""
    return atf.run_command(
        f"scontrol update NodeName={node} State={state}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_cpu_undrain_leaves_drained_tres():
    """State=UNDRAIN clears the node drain but leaves its drained TRES alone.

    That is the documented difference from RESUME, which returns the drained
    TRES to service as well. The node stays DEGRADED until they are resumed.
    """
    atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="undrain", fatal=True)
    assert atf.wait_for_node_state(NODE_A, "DEGRADED"), "NODE_A should reach DEGRADED"
    result = atf.run_command(
        f"scontrol update NodeName={NODE_A} State=DRAIN Reason=undrain_node",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(NODE_A, "DRAIN"), "NODE_A should reach DRAIN"

    result = _set_state(NODE_A, "UNDRAIN")
    assert result["exit_code"] == 0, result
    assert atf.wait_for_node_state(
        NODE_A, "DRAIN", reverse=True
    ), "NODE_A should leave DRAIN"
    assert "DEGRADED" in (
        atf.get_node_parameter(NODE_A, "state") or ""
    ), "node should be DEGRADED"
    assert "cpu(IDX:0)(undrain)" in (
        atf.get_node_tres_drained(NODE_A) or ""
    ), atf.get_node_detail(NODE_A)


@pytest.mark.parametrize(
    "recovery",
    ["resume", "resume_tres_then_node", "resume_keep_tres", "undrain", "idle"],
)
def test_cpu_max_drained_tres_recovery(recovery, max_drained_tres):
    """Each documented way out of a MaxDrainedTres breach does what it says.

    State=RESUME resumes the offending TRES with the node. Resuming enough TRES
    and then the node also clears it. State=RESUME_KEEP_TRES, UNDRAIN and IDLE
    leave the TRES drained, so the node stays drained while it is still over
    the limit.
    """
    reason = "cpu has 2 drained, exceeding MaxDrainedTres limit of 1"
    max_drained_tres(NODE_A, "cpu:1")
    atf.update_tres_drain(NODE_A, "cpu", "Index=0-1", reason="breach", fatal=True)
    assert atf.wait_for_node_state(NODE_A, "DRAIN"), "NODE_A should reach DRAIN"
    assert (
        atf.get_node_parameter(NODE_A, "reason") == reason
    ), "node reason should be the drain reason"

    if recovery == "resume":
        assert (
            _set_state(NODE_A, "RESUME")["exit_code"] == 0
        ), "state update should succeed"
        assert atf.wait_for_node_state(
            NODE_A, "DRAIN", reverse=True
        ), "NODE_A should leave DRAIN"
        assert atf.get_node_tres_drained(NODE_A) is None, atf.get_node_detail(NODE_A)
    elif recovery == "resume_tres_then_node":
        # Back within the limit, the node can return to service with the
        # rest still drained, and the next registration leaves it there.
        atf.update_tres_drain(NODE_A, "cpu", "Index=1", action="Resume", fatal=True)
        assert (
            _set_state(NODE_A, "RESUME_KEEP_TRES")["exit_code"] == 0
        ), "state update should succeed"
        assert atf.wait_for_node_state(
            NODE_A, "DRAIN", reverse=True
        ), "NODE_A should leave DRAIN"
        atf.start_slurmd(NODE_A)
        assert not atf.wait_for_node_state(
            NODE_A, "DRAIN", timeout=15
        ), "a node back within its limit was drained again"
        assert "DEGRADED" in (
            atf.get_node_parameter(NODE_A, "state") or ""
        ), "node should be DEGRADED"
        assert "cpu(IDX:0)(breach)" in (
            atf.get_node_tres_drained(NODE_A) or ""
        ), atf.get_node_detail(NODE_A)
    else:
        assert (
            _set_state(NODE_A, recovery.upper())["exit_code"] == 0
        ), "state update should succeed"
        assert atf.wait_for_node_state(
            NODE_A, "DRAIN"
        ), "NODE_A should stay DRAIN while still over MaxDrainedTres"
        assert "cpu(IDX:0-1)(breach)" in (
            atf.get_node_tres_drained(NODE_A) or ""
        ), atf.get_node_detail(NODE_A)
        assert (
            atf.get_node_parameter(NODE_A, "reason") == reason
        ), "node reason should be the drain reason"


def test_cpu_unparsable_max_drained_tres_is_reported(max_drained_tres):
    """A MaxDrainedTres the parser cannot read is called out, not ignored.

    An unreadable specification is read as "no limit", so without a diagnostic
    a typo silently removes the protection the option was configured for.
    Each rejected form is one token, so one value exercises every rejection.
    """
    log_path = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    # "gpu:1" is missing the gres/ prefix. The rest are a missing count, an
    # empty count, a negative count, a non-numeric count, a suffix on cpu, an
    # unknown gres suffix, and a gres count that overflows.
    spec = (
        "gpu:1,cpu,cpu:,cpu:-1,cpu:abc,cpu:1K,"
        "gres/gpu:1Z,gres/gpu:18446744073709551615K"
    )
    phrases = (
        "is not a node-local TRES",
        'is missing the ":<count>" suffix',
        "has no usable count",
        "is not a number",
        "has trailing characters",
        "has an unrecognized suffix",
        "is too large",
    )

    # Set it on the node's existing NodeName line -- appending a second line
    # for a node that require_nodes() already defined is a duplicate
    # definition, which the controller rejects outright and so never reaches
    # the check this test is about.
    max_drained_tres(NODE_GOOD, spec)
    # Match the specific diagnostic for this value on this node. A bare
    # "MaxDrainedTres" would also match the valid limit another test in
    # this module sets, and so would pass without the parser saying
    # anything at all.
    needle = f"MaxDrainedTres={spec} for NodeName={NODE_GOOD}"

    def _log_text():
        return atf.run_command_output(
            f"grep -F '{needle}' {log_path} || true",
            user=atf.properties["slurm-user"],
            quiet=True,
        )

    for _ in atf.timer(fatal=True):
        text = _log_text()
        if text and all((phrase in text for phrase in phrases)):
            break

    # Ignored also means it sets no cpu limit. Three drained CPUs exceed the
    # 0 or 1 that a misread "cpu:", "cpu:abc" or "cpu:1K" would set.
    atf.update_tres_drain(NODE_GOOD, "cpu", "Index=0-2", reason="nolimit", fatal=True)
    assert atf.wait_for_node_state(
        NODE_GOOD, "DEGRADED"
    ), "NODE_GOOD should reach DEGRADED"
    assert not atf.wait_for_node_state(
        NODE_GOOD, "DRAIN", timeout=5
    ), f"an ignored MaxDrainedTres drained the node: {atf.get_node_detail(NODE_GOOD)}"


def test_cpu_drain_set_replaces_drained_set():
    """Action=SetDrain makes the drained set exactly what it names.

    Add is cumulative, so it can only grow the set; Set both drains what is
    missing and resumes what is no longer listed, in one request. A selector
    with an empty value is refused and leaves the set alone.
    """
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=0-1", reason="first")["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if "cpu(IDX:0-1)" in f:
            break

    # Replace {0,1} with {2}: CPU 2 drains, CPUs 0 and 1 come back.
    assert (
        atf.update_tres_drain(
            NODE_A, "cpu", "Index=2", action="SetDrain", reason="second"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if f == "cpu(IDX:2)(second)":
            break

    # Set is declarative, so repeating it changes nothing.
    before = atf.get_node_tres_drained(NODE_A)
    assert (
        atf.update_tres_drain(
            NODE_A, "cpu", "Index=2", action="SetDrain", reason="second"
        )["exit_code"]
        == 0
    ), "drain update should succeed"
    assert atf.get_node_tres_drained(NODE_A) == before, atf.get_node_detail(NODE_A)

    # An empty selector is most likely an unset shell variable. Reading it as
    # the empty set would resume everything for that TRES, so it is refused.
    for selector in ("Index=", "Uuid=", "Count="):
        result = atf.update_tres_drain(
            NODE_A, "cpu", selector, action="SetDrain", reason="none"
        )
        assert result["exit_code"] != 0, f"{selector} should be refused: {result}"
        assert f"{selector[:-1]} requires a value" in result["stderr"], result
        assert atf.get_node_tres_drained(NODE_A) == before, atf.get_node_detail(NODE_A)


def test_cpu_drain_keeps_per_cpu_reasons():
    """Draining more CPUs later must not overwrite the earlier reason.

    Each CPU keeps the reason it was drained with, so DrainedTRES reports one
    entry per distinct reason rather than relabelling everything with the most
    recent one. On NODE_SMT those reasons belong to cores, and naming the
    other thread of a core does not create another one.
    """
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="flaky")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=2", reason="thermal")["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_A) or ""
        if "(flaky)" in f and "(thermal)" in f:
            break

    # On a threaded node the same two reasons stay on separate cores, and
    # draining the sibling thread does not add a second reason.
    assert (
        atf.update_tres_drain(NODE_SMT, "cpu", "Index=0", reason="flaky")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(NODE_SMT, "cpu", "Index=2", reason="thermal")["exit_code"]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(NODE_SMT, "cpu", "Index=1", reason="flaky")["exit_code"]
        == 0
    ), "drain update should succeed"
    for _ in atf.timer(fatal=True):
        f = atf.get_node_tres_drained(NODE_SMT) or ""
        if "cpu(IDX:0-1)(flaky)" in f and "cpu(IDX:2-3)(thermal)" in f:
            break


def test_cpu_drain_reasons_survive_restart():
    """Each drained CPU keeps its own reason across a restart.

    The reasons are packed as a counted array beside the drain map, so a
    miscounted save loses them all while the drains themselves survive.
    """
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=0", reason="first_reason")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"
    assert (
        atf.update_tres_drain(NODE_A, "cpu", "Index=2", reason="second_reason")[
            "exit_code"
        ]
        == 0
    ), "drain update should succeed"

    atf.restart_slurmctld(clean=False, quiet=True)

    for _ in atf.timer(fatal=True):
        f = _drained_cpus(NODE_A)
        if "first_reason" in f and "second_reason" in f:
            break


def test_cpu_drain_rejects_bad_request_without_changing_node():
    """Every rejected form leaves the node untouched.

    The request is resolved before anything is applied, so a bad selector
    cannot leave the node half drained.
    """
    for fields, error in (
        ("TresName=cpu", "TresName requires Index, Uuid or Count"),
        (
            "TresName=cpu Index=0 Count=1",
            "Count cannot be combined with Index or Uuid",
        ),
        ("TresName=cpu Count=2", "CPU count specification invalid"),
        ("Index=0", "Update of this parameter is not supported: Index=0"),
    ):
        result = atf.run_command(
            f"scontrol update NodeName={NODE_A} {fields} Action=Drain Reason=bad",
            user=atf.properties["slurm-user"],
        )
        assert result["exit_code"] != 0, f"{fields} should be rejected: {result}"
        assert error in result["stderr"], result
        assert atf.get_node_tres_drained(NODE_A) is None, atf.get_node_detail(NODE_A)

    # Action is mandatory: an omitted mode must not be assumed for CPU either.
    result = atf.run_command(
        f"scontrol update TresName=cpu NodeName={NODE_A} Index=0 Reason=bad",
        user=atf.properties["slurm-user"],
    )
    assert result["exit_code"] != 0, result
    assert (
        "TresName requires Action=Drain, Resume or SetDrain" in result["stderr"]
    ), result

    # A Reason is refused when resuming.
    result = atf.update_tres_drain(
        NODE_A, "cpu", "Index=0", action="Resume", reason="unused"
    )
    assert result["exit_code"] != 0, result
    assert "Reason is not used with Action=Resume" in result["stderr"], result
    assert atf.get_node_tres_drained(NODE_A) is None, atf.get_node_detail(NODE_A)


@pytest.mark.parametrize(
    "extra",
    [
        pytest.param("TresName=mem", id="TresName"),
        pytest.param(f"NodeName={NODE_B}", id="NodeName"),
        pytest.param("Action=Resume", id="Action"),
        pytest.param('Reason="second"', id="Reason"),
    ],
)
def test_cpu_drain_repeated_fields_refused(extra):
    """A field given twice is refused rather than silently taking the last.

    Taking the last TresName= or Action= would act on something other than
    what was written first.
    """
    result = _update_tres_raw(
        "TresName=cpu",
        f"NodeName={NODE_A}",
        "Index=0",
        "Action=Drain",
        'Reason="first"',
        extra,
    )
    assert result["exit_code"] != 0, f"a repeated field must be refused: {result}"
    assert (
        "specified more than once" in result["stderr"]
    ), f"the error should name the repetition: {result['stderr']!r}"
    assert not _drained_cpus(NODE_A), "a refused request must change nothing"


def test_cpu_drain_all_reaches_every_core_when_cpus_are_sockets():
    """Index=ALL drains the whole node when its CPUs are its sockets.

    NODE_SOCKETS has two CPUs over eight cores, and CPU i names core i, so its
    CPUs reach only the first two cores. ALL still means every core: a
    one-CPU job pends while they are drained and runs once ALL is resumed.
    """
    atf.update_tres_drain(
        NODE_SOCKETS, "cpu", "Index=ALL", reason="all_cores", fatal=True
    )
    assert atf.wait_for_node_state(NODE_SOCKETS, "DEGRADED"), atf.get_node_detail(
        NODE_SOCKETS
    )

    job_id = atf.submit_job_sbatch(
        f"-w {NODE_SOCKETS} -n1 --wrap 'sleep infinity'", fatal=True
    )
    assert atf.job_stays_pending(
        job_id
    ), f"a job ran on {NODE_SOCKETS} with every core drained"

    atf.update_tres_drain(NODE_SOCKETS, "cpu", "Index=ALL", action="Resume", fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "the job should run once every core is resumed"
