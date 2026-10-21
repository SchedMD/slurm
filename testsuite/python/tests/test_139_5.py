############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""PMIx steps spanning a dynamic-normal node in a heterogeneous job.

The het cases guard three separate fixes. The component order decides
which of them a case exercises, so both orderings are needed:

- Peer address lookup (Ticket 25730), both orderings. Node addresses are
  scoped per component: set_initial_job_alias_list() only builds
  node_addrs for a component that itself holds a dynamic or cloud node,
  and only for that component's own nodes. Dynamic nodes are never in
  slurm.conf, so without asking slurmctld a stepd cannot resolve a dynamic
  node in a sibling component, and mpi/pmix reports

    Can't find address for host <dyn>, check slurm.conf

- The mpi/pmix direct connection tie-break, whichever ordering differs from
  the sorted node order (DYN_NODE sorts after the static node, so the
  dynamic node in component 0). srun sorts the het node list, and the
  tie-break mixed that job-wide order with the component order, so when
  both stepds sent their first message at once, each waited for the other
  to connect and the step hung in a collective. The hang depends on that
  timing, so a regression may not fail every run.

- The het leader stepmgr address lookup (Issue 51212), covered by the
  stepmgr test: --stepmgr on the het leader, with the leader on the
  dynamic node. The sibling component's extern stepd asked slurmctld for
  the leader's address before its plugins were initialized and aborted,
  so srun failed with "Unable to confirm allocation ... Invalid job id
  specified".

The non-het test is the control. A single allocation covering both nodes
puts every address in one credential, and its node order matches the
sorted order, so it localizes a failure to the het path rather than to
PMIx on dynamic nodes in general.

Requires mpi/pmix.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

# It must sort after the static node.
DYN_NODE = "node200"
NODE_CPUS = 2
# Multi-slurmd puts every "node" on one host, where the OpenMPI shared
# memory BTL is not usable.
MPI_EXPORT = "ALL,OMPI_MCA_btl=^sm"
ADDR_ERROR = "Can't find address for host"
# Deliberately longer than STEP_TIMEOUT, so a step that is merely slow on a
# loaded machine is not killed and reported as TIMEOUT. The wait below is
# what bounds a genuinely hung step.
TIME_LIMIT = "5:00"
# Two-node MPI init plus the PMIx fence, on a host running every slurmd,
# takes longer than the default polling timeout.
STEP_TIMEOUT = 120
# Rank[1] on node2 just received msg from Rank 0 on node1
RANK_LINE = re.compile(
    r"Rank\[(\d+)\] on (\S+) just received msg from Rank (\d+) on (\S+)"
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # One static node carries the component that holds no dynamic node,
    # which is the component whose stepd cannot resolve the peer.
    atf.require_nodes(1, [("CPUs", NODE_CPUS)])
    # Must cover the configured nodes plus the dynamic one, with headroom
    # for a baseline that configures more nodes than requested.
    atf.require_config_parameter(
        "MaxNodeCount", len(atf.get_nodes(live=False, quiet=True, fatal=True)) + 2
    )
    # --stepmgr needs the extern step.
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CPU is a base and is mutually exclusive with the other CR_* bases,
    # so this must replace the value rather than join it.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("MpiDefault", "pmix")
    atf.require_lmod()
    atf.module_load("openmpi")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def nodes(setup, request):
    """Register a dynamic-normal slurmd; yield the static and dynamic names."""
    static_nodes = sorted(
        name
        for name in atf.get_nodes(live=False, quiet=True, fatal=True)
        if name != "DEFAULT"
    )
    assert static_nodes, "Need a configured static node for the other component"

    # slurmd writes its spool dir per node name, and mpi/pmix puts a stepd
    # socket there, so the dynamic node needs its own rather than sharing
    # a configured node's. start_slurmd() creates it. Only a per-node path
    # belongs to this node. Without %n the path is shared (TmpFS defaults
    # to /tmp), so teardown must leave it alone.
    templates = [atf.properties[name] for name in ("slurm-spool-dir", "slurm-tmpfs")]
    dirs = [t.replace("%n", DYN_NODE) for t in templates if "%n" in t]

    # Register the finalizer before starting, so a failed registration wait
    # still tears the slurmd down instead of leaking it into the next module.
    request.addfinalizer(lambda: _stop_dynamic_slurmd(DYN_NODE, dirs))
    atf.start_slurmd(DYN_NODE, f"-Z -b --conf 'CPUs={NODE_CPUS}'")
    # Poll get_nodes() rather than get_node_parameter(), which pytest.fail()s
    # while the node is still arriving in the controller's table.
    for _ in atf.timer(fatal=True):
        state = atf.get_nodes(quiet=True).get(DYN_NODE, {}).get("state", [])
        if "IDLE" in state and "DRAIN" not in state:
            break

    yield {"static": static_nodes[0], "dynamic": DYN_NODE}


def _stop_dynamic_slurmd(node_name, dirs):
    """Stop the dynamic slurmd, delete the node it registered and its dirs."""
    errors = []

    # A cancelled job can leave the node COMPLETING, which only this slurmd
    # can finish, and the controller refuses to delete a node in use.
    if node_name in atf.get_nodes(quiet=True):
        for _ in atf.timer(quiet=True):
            state = atf.get_nodes(quiet=True).get(node_name, {}).get("state", [])
            if not {"ALLOCATED", "MIXED", "COMPLETING"} & set(state):
                break
        else:
            errors.append(f"Node {node_name} never left ALLOCATED/MIXED/COMPLETING")

    atf.stop_slurmd(node_name, quiet=True)

    if node_name in atf.get_nodes(quiet=True):
        result = atf.run_command(
            f"scontrol delete NodeName={node_name}",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        if result["exit_code"] != 0:
            errors.append(f"Failed to delete node {node_name}: {result['stderr']}")

    # The spool dir holds this node's stepd and PMIx sockets, which a later
    # module registering the same node name would otherwise inherit.
    for path in dirs:
        atf.run_command(f"rm -rf {path}", user="root", quiet=True)

    if errors:
        pytest.fail("; ".join(errors))


def _job_detail(job_id):
    """Describe a job well enough to tell "never ran" from "hung"."""
    fields = []
    for name in ("JobState", "Reason", "NodeList", "StartTime"):
        fields.append(
            f"{name}={atf.get_job_parameter(job_id, name, default='?', quiet=True)}"
        )
    return " ".join(fields)


def _submit(sbatch_args, tag):
    """Submit, bound the run, and return (job_id, state, stdout, stderr)."""
    job_id = atf.submit_job_sbatch(
        f"-t{TIME_LIMIT} --output=out_{tag} --error=err_{tag} {sbatch_args}",
        fatal=True,
    )
    finished = atf.wait_for_job_state(job_id, "DONE", timeout=STEP_TIMEOUT)
    stdout = atf.run_command_output(f"cat out_{tag}", quiet=True)
    stderr = atf.run_command_output(f"cat err_{tag}", quiet=True)
    if not finished:
        # A step blocked in the PMIx collective stays RUNNING until function
        # teardown cancels it, so the job record read here still shows it.
        # Empty stderr looks identical for "never started" and "hung", so
        # report the job record too.
        pytest.fail(
            f"Job {job_id} ({tag}) did not finish within {STEP_TIMEOUT}s. "
            f"{_job_detail(job_id)}. stderr:\n{stderr}"
        )

    state = atf.get_job_parameter(job_id, "JobState", default="?", quiet=True)
    return job_id, state, stdout, stderr


def _run_het_step(mpi_program, group_nodes, tag, stepmgr=False):
    """Run one PMIx step across both components of a 2-component het job.

    With stepmgr, --stepmgr goes on the leader, which applies it to the
    whole job.
    """
    components = "#SBATCH hetjob\n".join(
        f"#SBATCH --nodes=1\n#SBATCH --ntasks=1\n#SBATCH --nodelist={node}\n"
        + ("#SBATCH --stepmgr\n" if stepmgr and offset == 0 else "")
        for offset, node in enumerate(group_nodes)
    )
    script = f"het_{tag}.sh"
    atf.make_bash_script(
        script,
        f"{components}"
        f"srun --mpi=pmix --het-group=0,1 --export={MPI_EXPORT} {mpi_program}\n",
    )
    return _submit(script, tag)


def _assert_pmix_exchange(state, stdout, stderr, context, hosts):
    """Assert the step completed and both ranks exchanged across both nodes."""
    detail = f"stdout:\n{stdout}\nstderr:\n{stderr}"
    assert (
        ADDR_ERROR not in stderr
    ), f"{context}: mpi/pmix could not resolve a peer address. {detail}"
    assert state == "COMPLETED", f"{context}: expected COMPLETED, got {state}. {detail}"

    matches = RANK_LINE.findall(stdout)
    assert matches, f"{context}: expected MPI rank output. {detail}"

    expected_ranks = {0, 1}
    printed = {int(m[0]) for m in matches}
    assert printed == expected_ranks, (
        f"{context}: expected ranks {sorted(expected_ranks)} to report, got "
        f"{sorted(printed)}. The components may have formed separate "
        f"MPI_COMM_WORLDs. {detail}"
    )

    senders = {int(m[2]) for m in matches}
    assert (
        senders == expected_ranks
    ), f"{context}: expected a message from each rank, got {sorted(senders)}. {detail}"

    seen = {m[1] for m in matches} | {m[3] for m in matches}
    assert seen == set(hosts), (
        f"{context}: expected the exchange to span {sorted(set(hosts))}, saw "
        f"{sorted(seen)}. {detail}"
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (25, 11),
    reason="Two-node PMIx steps hang on 25.05 here, even on static nodes",
)
def test_pmix_nonhet_spans_static_and_dynamic(mpi_program, nodes):
    """A non-het PMIx step over a static and a dynamic node succeeds.

    One allocation holds both nodes, so the single credential carries both
    addresses. This separates the het scoping problem from PMIx being
    unable to reach a dynamic node at all.
    """
    static, dynamic = nodes["static"], nodes["dynamic"]
    job_id, state, stdout, stderr = _submit(
        f"-N2 -n2 --nodelist={static},{dynamic} "
        f"--wrap 'srun --mpi=pmix --export={MPI_EXPORT} {mpi_program}'",
        "nonhet",
    )
    _assert_pmix_exchange(
        state,
        stdout,
        stderr,
        f"job {job_id} non-het static+dynamic",
        hosts=[static, dynamic],
    )


@pytest.mark.skipif(
    atf.get_version("sbin/slurmd") < (26, 5, 6),
    reason="Ticket 25730: het PMIx steps spanning a dynamic node fail to "
    "resolve peers or hang in the direct connection tie-break. Fixed in 26.05.6",
)
@pytest.mark.parametrize(
    "dynamic_group", [0, 1], ids=["dynamic_in_group0", "dynamic_in_group1"]
)
def test_pmix_het_spans_dynamic_node(mpi_program, nodes, dynamic_group):
    """A het PMIx step spanning a dynamic node completes.

    Peers in the other component must resolve the dynamic NodeName, which
    is in no component's credential but its own and is never in slurm.conf.
    """
    static, dynamic = nodes["static"], nodes["dynamic"]
    group_nodes = [dynamic, static] if dynamic_group == 0 else [static, dynamic]

    job_id, state, stdout, stderr = _run_het_step(
        mpi_program, group_nodes, tag=f"het_group{dynamic_group}"
    )
    _assert_pmix_exchange(
        state,
        stdout,
        stderr,
        f"job {job_id} het with the dynamic node in group {dynamic_group}",
        hosts=group_nodes,
    )


# The client tools upgrade last, so gating on srun also covers slurmd.
@pytest.mark.skipif(
    atf.get_version("bin/srun") < (26, 11),
    reason="Issue 50976: hetjob + stepmgr requires srun 26.11+",
)
def test_pmix_het_stepmgr_leader_on_dynamic_node(mpi_program, nodes):
    """A het PMIx step with --stepmgr and the leader on a dynamic node completes.

    The sibling component's extern stepd is given the leader's NodeName in
    its credential and must resolve it through slurmctld, since the dynamic
    node is not in slurm.conf.
    """
    static, dynamic = nodes["static"], nodes["dynamic"]
    group_nodes = [dynamic, static]

    job_id, state, stdout, stderr = _run_het_step(
        mpi_program, group_nodes, tag="het_stepmgr", stepmgr=True
    )
    assert (
        atf.get_job_parameter(job_id, "StepMgrEnabled", default="", quiet=True) == "Yes"
    ), f"Job {job_id} must be stepmgr-enabled to reproduce the bug"
    _assert_pmix_exchange(
        state,
        stdout,
        stderr,
        f"job {job_id} het with --stepmgr and the leader on the dynamic node",
        hosts=group_nodes,
    )
