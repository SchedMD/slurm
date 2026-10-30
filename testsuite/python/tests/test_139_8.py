############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Dynamic nodes must still be schedulable after delete and recreate.

Deleting a node clears its bit in share_node_bitmap. Short of the node going
DOWN or a job completing on it, the only paths that set it again are node
registration and ping replies, both of which need a slurmd. An EXTERNAL node
has none. A CLOUD node has one but cannot reach it either, because it only
boots once a job is allocated and the cleared bit is what blocks the
allocation. Either way the node is left unschedulable at the recycled node
index unless creation sets the bit.

The first node created at a given index inherits a set bit from
_build_bitmaps(), which sets every slot including ones holding no node, so
only the recreate is affected. Each test therefore runs a job, deletes and
recreates the node at the same index, and runs the job again -- the second
run is the assertion.

--exclusive is covered because it takes a different branch when the candidate
nodes are filtered (idle_node_bitmap rather than share_node_bitmap), yet is
still subject to the unconditional intersection with share_node_bitmap that
_pick_best_nodes() applies before calling the select plugin.
"""

import logging

import pytest

import atf

# The creation-time bit_set(share_node_bitmap) that makes the recreated node
# schedulable is in 26.11 and was backported to 26.05.3, so on any older
# slurmctld the "after recreate" run fails by design. xfail rather than skip
# so the first run still exercises those versions, and so the XPASS says when
# this gate can be retired.
pytestmark = [
    pytest.mark.xfail(
        atf.get_version("sbin/slurmctld") < (26, 5, 3),
        reason="Issue 51106: dynamic node creation only sets share_node_bitmap in slurmctld 26.05.3+",
    ),
]

external_node = "extnode1"
cloud_node = "cloudnode1"

# The cloud node never registers (ResumeProgram is /bin/true), so a short
# timeout just bounds how long a stuck node lingers during cleanup.
resume_timeout = 15


@pytest.fixture(scope="module", autouse=True)
def setup():
    # MaxNodeCount is required to allow creating dynamic nodes, and
    # slurmctld rejects `scontrol create nodename=` unless SelectType is
    # select/cons_tres.
    atf.require_config_parameter("MaxNodeCount", 8)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")

    # Power saving has to be configured before a node can be State=CLOUD.
    # ResumeProgram is a no-op, so the cloud node is allocated and asked to
    # power up but never registers -- enough to prove it was selected.
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_config_parameter("SuspendTime", "INFINITE")

    # The dynamic nodes join the default partition via Nodes=ALL, so a job can
    # target one with -w and no extra partition is needed.
    atf.require_config_parameter(
        "PartitionName", {"primary": {"Nodes": "ALL", "Default": "YES"}}
    )

    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def reconfigure_between_tests():
    """Reconfigure before every test.

    _build_bitmaps() re-runs bit_set_all(share_node_bitmap), so each test
    starts from the state a freshly started slurmctld would have. Without
    this, a node deleted by the previous test's teardown would leave the next
    test's *first* job failing, which points at the wrong half of the cycle.
    """
    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def node_states(node_name):
    """Node state as a list of strings, or [] if the node no longer exists.

    A single query keeps this atomic and cheap. get_node_parameter() fails the
    test outright on an unknown node, so guarding it with a separate get_nodes()
    would both double the scontrol round trips this makes -- inside a poll loop
    bounded by ResumeTimeout -- and leave a window where a teardown racing a
    deletion still errors.
    """
    return atf.get_nodes(quiet=True).get(node_name, {}).get("state", [])


class DynamicNodes:
    """Create and delete dynamic nodes of one State, tracking what exists."""

    def __init__(self, state):
        self.state = state
        self.live = set()

    def create(self, name):
        atf.run_command(
            f"scontrol create nodename={name} CPUs=4 State={self.state}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
        self.live.add(name)

    def delete(self, name, fatal=True):
        """Remove a node, waiting for it to be released first.

        _delete_node_ptr() refuses a node that is ALLOCATED or COMPLETING,
        and cancelling a job only waits on the job, not on the node
        bookkeeping that follows it.

        fatal=False is for teardown, where failing outright would leave the
        node behind and turn the next parametrization into a second, more
        confusing failure.
        """
        for _ in atf.timer():
            if not ({"ALLOCATED", "COMPLETING", "MIXED"} & set(node_states(name))):
                break
        else:
            message = f"Node {name} was never released, so cannot be deleted"
            if fatal:
                pytest.fail(message)
            logging.warning(message)
            return

        result = atf.run_command(
            f"scontrol delete nodename={name}",
            user=atf.properties["slurm-user"],
            fatal=fatal,
        )
        if result["exit_code"]:
            logging.warning(f"Could not delete node {name}: {result['stderr']}")
            return
        self.live.discard(name)


def _cleanup(nodes):
    """Cancel jobs and remove any node the test left behind.

    conftest.py function_setup also cancels jobs, but it unwinds after this
    fixture, and a node that still holds a job cannot be deleted. A failure
    here only warns: making it fatal would report the same underlying problem
    twice, once as this teardown and again as an "already exists" create in
    the next parametrization, with the second message naming the wrong cause.
    """
    atf.cancel_all_jobs(quiet=True)
    for name in sorted(nodes.live):
        nodes.delete(name, fatal=False)


@pytest.fixture
def external_nodes():
    """An EXTERNAL node manager, cleaned up after the test."""
    nodes = DynamicNodes("external")
    yield nodes
    _cleanup(nodes)


@pytest.fixture
def cloud_nodes():
    """A CLOUD node manager, cleaned up after the test."""
    nodes = DynamicNodes("cloud")
    yield nodes
    _cleanup(nodes)


def expect_running(sbatch_args, label):
    """Submit an external job and require it to reach RUNNING."""
    job_id = atf.submit_job_sbatch(sbatch_args, fatal=True)
    if not atf.wait_for_job_state(job_id, "RUNNING"):
        pytest.fail(
            f"{label}: job {job_id} never reached RUNNING. Reason="
            f"{atf.get_job_parameter(job_id, 'Reason', quiet=True)}"
        )
    return job_id


def expect_scheduled(sbatch_args, target, label):
    """Submit a cloud job and require the scheduler to select target.

    The node never registers, so the job cannot reach RUNNING; being allocated
    the node is the whole assertion. Checking the allocation rather than just
    "the job left PENDING" matters, because a job that dies for an unrelated
    reason also leaves PENDING without the node ever having been picked.

    NodeList is compared exactly rather than by substring, since the job asks
    for a single node by name and a substring would also accept a longer name
    that merely starts with target. The node reaching POWERING_UP is not a
    second acceptance condition: select_nodes() sets the job's node list at
    allocation time, before the node transitions, so NodeList always shows it
    first, and keying off the node state would also accept a power up that no
    job caused.
    """
    job_id = atf.submit_job_sbatch(sbatch_args, fatal=True)

    nodes, state, reason = None, None, None
    for _ in atf.timer():
        # One query per iteration, so NodeList and JobState describe the same
        # instant. Read separately, the job can be allocated the node and then
        # go terminal in the gap between the two, and breaking on that state
        # without rechecking NodeList reports an allocation that did happen as
        # one that never did. Reason is captured here too rather than queried
        # from inside pytest.fail(), where a job that has since been purged
        # would replace the whole diagnostic with "Job was not found".
        job = atf.get_jobs(quiet=True).get(job_id)
        if not job:
            continue
        nodes = job.get("NodeList")
        state = job.get("JobState")
        reason = job.get("Reason")
        if nodes and str(nodes).strip() == target:
            return job_id
        if state and not ({"PENDING", "CONFIGURING"} & set(str(state).split("+"))):
            break

    pytest.fail(
        f"{label}: job {job_id} was never allocated {target} "
        f"(JobState={state}, NodeList={nodes}, Reason={reason})"
    )


@pytest.mark.parametrize("exclusive", [False, True], ids=["shared", "exclusive"])
def test_external_job_after_node_recreate(external_nodes, exclusive):
    """An external job still runs after its EXTERNAL node is recreated."""
    args = f"--external -n1 -t1 -w {external_node}"
    if exclusive:
        args += " --exclusive"

    external_nodes.create(external_node)
    expect_running(args, "before recreate")

    atf.cancel_all_jobs(fatal=True)
    external_nodes.delete(external_node)
    external_nodes.create(external_node)

    expect_running(args, "after recreate")


@pytest.mark.parametrize("exclusive", [False, True], ids=["shared", "exclusive"])
def test_cloud_job_after_node_recreate(cloud_nodes, exclusive):
    """A job is still allocated to a CLOUD node after the node is recreated.

    A cloud node only powers up once a job is allocated to it, so failing to
    schedule leaves it unable to ever register and repair itself.
    """
    args = f"-n1 -t1 -w {cloud_node} --wrap 'sleep infinity'"
    if exclusive:
        args += " --exclusive"

    cloud_nodes.create(cloud_node)
    expect_scheduled(args, cloud_node, "before recreate")

    atf.cancel_all_jobs(fatal=True)
    cloud_nodes.delete(cloud_node)
    cloud_nodes.create(cloud_node)

    expect_scheduled(args, cloud_node, "after recreate")
