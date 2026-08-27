############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""gres.conf Cores= is a scheduling preference, not a constraint.

gres.conf(5) promises that the cores named by Cores= are *preferred* when
scheduling a generic resource, and that only --gres-flags=enforce-binding
turns that preference into a constraint. A job that fits by total CPUs and
GPUs must therefore be accepted even when no socket-local packing of its
tasks exists, and a job that does not fit by totals must still be refused.

Topology: two nodes, each 2 sockets x 3 cores, with 2 GPUs bound per socket.
With --cpus-per-task=2 only one task fits on a socket alongside its GPU, so
the socket-local packing tops out at 4 CPUs per node -- 8 across the cluster
-- while the nodes really offer 12. Requests above that line are the ones
that can only be placed by relaxing the preference.

Ticket 25596.
"""

import pytest

import atf

# Populated by setup() from the nodes require_nodes() actually configured.
gpu_nodes = []

# The two documented spellings of a per-task GPU request. The ticket and its
# reproducer use --tres-per-task; --gpus-per-task is the shorthand.
_PER_TASK_GPU_OPTS = [
    pytest.param("--gpus-per-task=1", id="gpus_per_task"),
    pytest.param("--tres-per-task=gres/gpu=1", id="tres_per_task"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Core_Memory")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        2,
        [
            ("Sockets", 2),
            ("CoresPerSocket", 3),
            ("ThreadsPerCore", 1),
            ("Gres", "gpu:4"),
        ],
    )
    # GPUs need to point to existing device files
    for i in range(4):
        atf.require_tty(i)

    # Scope gres.conf to the nodes require_nodes() created rather than assuming
    # their names: if they were ever addressed wrongly the GPUs would come up
    # with no Cores= binding and every test here would pass without exercising
    # the binding at all.
    global gpu_nodes
    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and "gpu:" in str(params.get("Gres", ""))
    )
    assert (
        len(gpu_nodes) == 2
    ), f"Expected require_nodes() to configure 2 GPU nodes, got {gpu_nodes}"
    node_list = ",".join(gpu_nodes)

    # Bind 2 GPUs to socket 0 (cores 0-2) and 2 GPUs to socket 1 (cores 3-5).
    atf.require_config_parameter(
        "NodeName",
        f"{node_list} Name=gpu Cores=0-2 File=/dev/tty[0-1]\n"
        f"NodeName={node_list} Name=gpu Cores=3-5 File=/dev/tty[2-3]",
        source="gres",
    )
    atf.require_slurm_running()


def _submit_and_run(job_args):
    """Submit a job that must be accepted and run, returning its scontrol dict.

    A request the scheduler will not place is refused by sbatch outright rather
    than left pending, so the submission is checked before the state is. That
    refusal is the behavior under test, not setup noise, which is why it is
    asserted on here instead of passing fatal=True.
    """

    job_id = atf.submit_job_sbatch(f'{job_args} --wrap "sleep infinity"')
    assert job_id != 0, (
        "Job should be accepted at submit: gres.conf Cores= is a preference, "
        "so a request that fits by total CPUs and GPUs must be placed even "
        "when no socket-local packing of its tasks exists"
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), (
        f"Job {job_id} should run: gres.conf Cores= is a preference, so a "
        "request that fits by total CPUs and GPUs must be placed even when no "
        "socket-local packing of its tasks exists"
    )

    return atf.get_job(job_id)


@pytest.mark.parametrize("gpu_opt", _PER_TASK_GPU_OPTS)
def test_job_beyond_socket_local_packing_runs(gpu_opt):
    """A request that only fits by cluster totals must still be placed.

    5 tasks x (2 CPUs + 1 GPU) needs 10 CPUs and 5 GPUs. That fits across the
    two nodes (12 CPUs, 8 GPUs) but exceeds the 8 CPUs the socket-local
    packing allows, so it can only be placed by relaxing the preference. Every
    task still lands on a socket holding one of the job's GPUs; the tasks and
    their GPUs are simply no longer paired socket by socket.
    """

    job = _submit_and_run(f"-n5 -c2 {gpu_opt}")

    assert (
        job["NumNodes"] == 2
    ), f"Job should span both nodes, got NumNodes={job['NumNodes']}"
    assert (
        job["NumCPUs"] == 10
    ), f"Job should hold 10 CPUs, got NumCPUs={job['NumCPUs']}"
    assert (
        job["JOB_GRES"] == "gpu:5"
    ), f"Job should hold 5 GPUs, got JOB_GRES={job['JOB_GRES']}"


def test_cpus_may_exceed_the_cores_bound_to_the_gres():
    """Without enforce-binding a job may use CPUs its GRES is not bound to.

    2 tasks x 2 CPUs against a single GPU needs 4 CPUs, but only the 3 cores of
    that GPU's socket are bound to it. Because Cores= is a preference, the
    allocation is free to reach into the other socket. Paired with
    test_enforce_binding_confines_job_to_its_gres_socket, which is the same
    request under the constraint.
    """

    job = _submit_and_run("-N1 -n2 -c2 --gres=gpu:1")

    # 4 CPUs cannot fit on one 3-core socket, so this count is itself the
    # proof that the allocation crossed sockets.
    assert job["NumCPUs"] == 4, (
        "Job should hold 4 CPUs, reaching past the 3 cores bound to its GPU, "
        f"got NumCPUs={job['NumCPUs']}"
    )


def test_disable_binding_allows_cross_socket_cpus():
    """--gres-flags=disable-binding must not prevent that cross-socket use.

    gres.conf(5) presents disable-binding as the way to use more CPUs than are
    bound to a GRES, so the same request must be placed with the flag as
    without it.
    """

    job = _submit_and_run("-N1 -n2 -c2 --gres=gpu:1 --gres-flags=disable-binding")

    assert job["NumCPUs"] == 4, (
        "Job should hold 4 CPUs, reaching past the 3 cores bound to its GPU, "
        f"got NumCPUs={job['NumCPUs']}"
    )


def test_enforce_binding_confines_job_to_its_gres_socket():
    """--gres-flags=enforce-binding turns the preference into a constraint.

    The contrast half of test_cpus_may_exceed_the_cores_bound_to_the_gres: the
    identical request must be refused, because sbatch(1) promises the only
    CPUs available are those bound to the selected GRES -- here the 3 cores of
    one socket, one short of the 4 the job needs.
    """

    result = atf.run_job(
        "-N1 -n2 -c2 --gres=gpu:1 --gres-flags=enforce-binding true",
        xfail=True,
        fatal=True,
    )

    assert "Requested node configuration is not available" in result["stderr"], (
        "srun should be refused because enforce-binding confines the job to "
        f"the 3 cores bound to its GPU, got stderr={result['stderr']!r}"
    )


def test_gres_request_beyond_cluster_totals_refused():
    """Relaxing the preference must not conjure GRES the cluster lacks.

    9 tasks x (1 CPU + 1 GPU) wants 9 GPUs where the cluster has 8. The CPU
    demand is satisfiable, so the GRES count is the only thing that can refuse
    this -- which makes it the sharper of the two totals checks.
    """

    result = atf.run_job("-n9 -c1 --gpus-per-task=1 true", xfail=True, fatal=True)

    assert "Requested node configuration is not available" in result["stderr"], (
        "srun should be refused for wanting 9 GPUs where the cluster has 8, "
        f"got stderr={result['stderr']!r}"
    )


def test_cpu_request_beyond_cluster_totals_never_runs():
    """Relaxing the preference must not overcommit the cluster's CPUs.

    7 tasks x 2 CPUs wants 14 CPUs where the cluster has 12. Unlike the GRES
    case this is not refused at submit -- the controller leaves it queued --
    so what must hold is that it never starts.
    """

    job_id = atf.submit_job_sbatch(
        '-n7 -c2 --gpus-per-task=1 --wrap "sleep infinity"', fatal=True
    )

    # Proving a negative costs the whole timeout, so keep it short: the other
    # jobs in this module all reach RUNNING within a couple of seconds, and no
    # amount of extra waiting makes a 14-CPU job fit on 12 CPUs. fatal=True is
    # deliberately omitted -- not reaching RUNNING is the expected outcome.
    assert not atf.wait_for_job_state(job_id, "RUNNING", timeout=15), (
        "Job wanting 14 CPUs should never start on a 12-CPU cluster, but it "
        f"reached RUNNING with "
        f"NumCPUs={atf.get_job_parameter(job_id, 'NumCPUs', fatal=True)}"
    )
    assert (
        atf.get_job_parameter(job_id, "JobState", fatal=True) == "PENDING"
    ), "Job wanting more CPUs than the cluster has should stay pending"
