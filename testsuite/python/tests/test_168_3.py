############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Client tooling against a heterogeneous job running with --stepmgr.

sbcast has to resolve --jobid=<leader> and --jobid=<leader>+<offset> through
the controller and send each component its own job_id, because the
<leader>+<offset> form is not resolvable on the target slurmd after reroute.
squeue --only-job-state has to report every component of the hetjob.

Both already have coverage against a plain hetjob elsewhere in the suite,
so this only adds the stepmgr combination.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

HET_OPTS = "--stepmgr -N1 --ntasks=1 -t2 --exclusive : -N1 --ntasks=1 -t2 --exclusive"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The client tools upgrade last, so gating on sbcast >= 26.11 already
    # implies slurmd and slurmctld are >= 26.11.
    atf.require_version(
        (26, 11),
        component="bin/sbcast",
        reason="Issue 50976: hetjob + stepmgr requires sbcast 26.11+",
    )
    atf.require_nodes(2)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_slurm_running()

    if atf.get_config_parameter("SlurmdUser") != "root(0)":
        pytest.skip("Issue 50976: sbcast requires SlurmdUser to be root")


def _start_hetjob():
    """Submit a running 2-component stepmgr hetjob and return its leader id."""

    leader_job_id = atf.submit_job_sbatch(
        f"-o /dev/null {HET_OPTS} --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(leader_job_id, "RUNNING", fatal=True)
    return leader_job_id


def test_sbcast_hetjob_stepmgr():
    """sbcast reaches the leader, and each component by +<offset>."""

    leader_job_id = _start_hetjob()
    bcast_dir = f"bcast_{leader_job_id}"

    payload = "sbcast_payload"
    atf.make_bash_script(payload, "true\n")

    # A relative destination resolves against BcastParameters' directory when
    # one is configured, and against the invoking working directory otherwise.
    atf.run_command(f"mkdir -p {bcast_dir}", fatal=True)

    # The bare leader id fans out to every component; the +<offset> form
    # targets one component and is the form that is not resolvable on the
    # target slurmd once the request has been rerouted.
    atf.run_command(
        f"sbcast -f --jobid={leader_job_id} {payload} {bcast_dir}/file", fatal=True
    )

    jobs = atf.get_jobs(leader_job_id, fatal=True)
    components = sorted(
        atf.range_to_list(jobs[leader_job_id]["HetJobIdSet"]),
        key=lambda job_id: jobs[job_id]["HetJobOffset"],
    )

    # --exclusive puts the components on different nodes, so the resolved job
    # id and node list tell apart a request that reached its own component
    # from one that collapsed onto the leader.
    nodes = [jobs[job_id]["NodeList"] for job_id in components]
    if nodes[0] == nodes[1]:
        pytest.fail(
            f"The components should be on different nodes for the routing"
            f" assertions to be meaningful, got {nodes}"
        )

    for offset, job_id in enumerate(components):
        result = atf.run_command(
            f"sbcast -vv -f --jobid={leader_job_id}+{offset} {payload}"
            f" {bcast_dir}/file_comp{offset}",
            fatal=True,
        )
        assert re.search(rf"^sbcast: jobid\s+= {job_id}$", result["stderr"], re.M), (
            f"sbcast --jobid={leader_job_id}+{offset} must resolve to component"
            f" job {job_id}.\nstderr:\n{result['stderr']}"
        )
        node = jobs[job_id]["NodeList"]
        assert re.search(rf"^sbcast: node_list\s+= {node}$", result["stderr"], re.M), (
            f"sbcast --jobid={leader_job_id}+{offset} must target component"
            f" job {job_id}'s node {node}.\nstderr:\n{result['stderr']}"
        )

    for name in ("file", "file_comp0", "file_comp1"):
        atf.assert_file_contents(f"{bcast_dir}/{name}", "true", contains=True)


def test_squeue_only_job_state_hetjob_stepmgr():
    """squeue --only-job-state reports every component of a stepmgr hetjob."""

    leader_job_id = _start_hetjob()

    output = (
        atf.run_command_output(
            f"squeue --noheader --only-job-state --format='%i=%T' -j {leader_job_id}",
            fatal=True,
        )
        .strip()
        .splitlines()
    )

    assert f"{leader_job_id}+0=RUNNING" in output, (
        f"Expected component 0 of hetjob {leader_job_id} in --only-job-state "
        f"output, got {output}"
    )
    assert f"{leader_job_id}+1=RUNNING" in output, (
        f"Expected component 1 of hetjob {leader_job_id} in --only-job-state "
        f"output, got {output}"
    )
    assert (
        len(output) == 2
    ), f"Expected exactly 2 hetjob components, got {len(output)}: {output}"
