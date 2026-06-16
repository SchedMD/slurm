############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
"""GPU selection for jobs and steps when GPUs are bound to specific cores."""

import re

import pytest

import atf

# One GPU per socket, so a single-socket step forces pass 2 of the GRES
# selection to fill more than one remaining device.
_NUM_GPUS = 4
# Spare sockets that hold no GPU of their own, so a request that cannot be
# placed on a GPU-bound socket is one --gres-flags=enforce-binding has to
# reject rather than one the node simply cannot fit.
_NUM_SOCKETS = _NUM_GPUS + 2
_ALL_GPUS = ",".join(str(i) for i in range(_NUM_GPUS))

_OVERLAP_GRES_UNFIXED = atf.get_version("sbin/slurmctld") < (26, 5, 3)
_OVERLAP_GRES_REASON = "Ticket 24862: --overlap GRES double-select fixed in 26.05.3"

node_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_Socket_Memory")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        1,
        [
            ("Sockets", _NUM_SOCKETS),
            ("CoresPerSocket", 1),
            ("ThreadsPerCore", 1),
            ("Gres", f"gpu:{_NUM_GPUS}"),
        ],
    )
    # GPU's need to point to existing device files
    for i in range(_NUM_GPUS):
        atf.require_tty(i)
    gpu_nodes = [
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and "gpu:" in str(params.get("Gres", ""))
    ]
    assert gpu_nodes, "Expected require_nodes() to configure a node with GPUs"
    node_name = gpu_nodes[0]
    # Bind GPU i to socket i (core i), so a step on one socket owns just one
    # GPU and pass 2 must supply the rest.
    gpu_lines = []
    for i in range(_NUM_GPUS):
        prefix = node_name if i == 0 else f"NodeName={node_name}"
        gpu_lines.append(f"{prefix} Name=gpu Cores={i} File=/dev/tty{i}")
    atf.require_config_parameter("NodeName", "\n".join(gpu_lines), source="gres")
    atf.require_slurm_running()


@pytest.fixture(scope="function")
def single_socket_gpu_job():
    """Job holding every GPU, with its cores confined to a single socket.

    The preconditions are asserted here so that every test sharing the
    fixture checks them in one place, and so that a topology that does not
    reproduce the condition says which precondition it failed rather than
    only that the step came up short on GPUs.
    """

    job_id = atf.submit_job_sbatch(
        f"-N1 -n1 --gpus-per-node={_NUM_GPUS} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    job_gres = atf.get_job_parameter(job_id, "GRES", fatal=True, quiet=True)
    assert job_gres and f"gpu:{_NUM_GPUS}(IDX:" in job_gres, (
        f"Expected job to hold every GPU (gpu:{_NUM_GPUS}(IDX:...)), "
        f"got GRES={job_gres!r} - topology did not produce the "
        f"expected no-enforce-binding allocation"
    )

    num_cpus = atf.get_job_parameter(job_id, "NumCPUs", fatal=True, quiet=True)
    assert num_cpus == 1, (
        f"Expected job to hold a single socket (1 CPU with CoresPerSocket=1), "
        f"got NumCPUs={num_cpus} - topology did not confine the job to one "
        f"socket, so the step already covers every GPU-bound socket"
    )

    return job_id


def test_gpu_socket_sharing():
    """Test allocating multiple core-bound gpus with enforce-binding"""

    output = atf.run_command_output(
        f"srun --gres-flags=enforce-binding --ntasks-per-socket=1 \
                    --cpus-per-task=1 --ntasks-per-node=2 -N1 \
                    --gpus-per-task=1 scontrol show nodes {node_name} -d",
        fatal=True,
    )
    assert (
        re.search(r"GresUsed=gpu:(?:[^:]*:)?2\(", output) is not None
    ), "Verify that job allocated 2 gpus"


def test_gpu_socket_sharing_no_alloc():
    """Test allocating multiple core-bound gpus with enforce-binding without enough resources"""

    output = atf.run_command(
        f"srun --gres-flags=enforce-binding --ntasks-per-socket=1 \
                    --cpus-per-task=2 --ntasks-per-node=2 -N1 \
                    --gpus-per-task=1 scontrol show nodes {node_name} -d",
        xfail=True,
        fatal=True,
    )
    assert output["exit_code"] != 0, "Verify that srun command failed"

    expected_msg = r"srun: error: .+ Requested node configuration is not available"
    assert (
        re.search(
            expected_msg,
            str(output["stderr"]),
        )
        is not None
    ), "Verify that job is rejected with the right reason."


@pytest.mark.xfail(_OVERLAP_GRES_UNFIXED, reason=_OVERLAP_GRES_REASON)
def test_overlap_step_gets_all_gpus(single_socket_gpu_job):
    """An --overlap step must receive every GPU it requests.

    srun --overlap shares all of the job's GRES with the step.  The batch
    job requests -n1 so CR_Socket_Memory confines its cores to one socket,
    while --gpus-per-node (no enforce-binding) allocates every GPU across
    all sockets.  The --overlap step lands on that single socket but, per the
    srun default, inherits every GPU the job holds, so it must be granted
    all of them even though most are bound to sockets its cores do not
    cover.  A short allocation shows up as a denied device in the step's
    cgroup if task/cgroup and ConstrainDevices=yes is configured
    (ticket 24862 regression).
    """

    job_id = single_socket_gpu_job

    result = atf.run_job(
        f"--jobid={job_id} --overlap -n1 --mem=0 "
        f"bash -c 'echo $SLURM_STEP_GPUS; echo $CUDA_VISIBLE_DEVICES'",
        fatal=True,
        quiet=True,
    )
    output = result["stdout"].splitlines()
    assert len(output) >= 2, (
        f"Expected two output lines (SLURM_STEP_GPUS and "
        f"CUDA_VISIBLE_DEVICES), got {result['stdout']!r}"
    )
    step_gpus = output[0].strip()
    visible_devices = output[1].strip()

    assert step_gpus == _ALL_GPUS, (
        f"Step SLURM_STEP_GPUS should be {_ALL_GPUS!r} (every GPU), "
        f"got {step_gpus!r} - the --overlap step was not granted all of "
        f"the job's GPUs (ticket 24862 regression)"
    )
    assert visible_devices == _ALL_GPUS, (
        f"Step CUDA_VISIBLE_DEVICES should be exported and agree with "
        f"SLURM_STEP_GPUS ({_ALL_GPUS}), got {visible_devices!r} - the "
        f"step's GPU allocation is not rendered consistently across both "
        f"variables (ticket 24862 regression)"
    )


@pytest.mark.xfail(_OVERLAP_GRES_UNFIXED, reason=_OVERLAP_GRES_REASON)
@pytest.mark.parametrize("first_step_overlaps", [True, False])
def test_concurrent_steps_each_get_all_gpus(first_step_overlaps, single_socket_gpu_job):
    """Concurrent steps sharing an allocation each receive every GPU.

    srun --overlap shares all resources with all other steps, even those
    that did not specify --overlap.  Two steps running at the same time
    against the same allocation must therefore both be granted every GPU
    the job holds, whichever of them asked to overlap.
    """

    job_id = single_socket_gpu_job

    first_step_gpus_file = "first_step_gpus"
    overlap = "--overlap " if first_step_overlaps else ""
    atf.run_command(
        f"srun --jobid={job_id} {overlap}-n1 --mem=0 bash -c "
        f"'echo $SLURM_STEP_GPUS > {first_step_gpus_file}; sleep infinity'",
        background=True,
    )
    atf.assert_file_contents(
        first_step_gpus_file,
        _ALL_GPUS,
        message=(
            f"First step SLURM_STEP_GPUS should be {_ALL_GPUS!r} (every GPU) "
            f"- the step was not granted all of the job's GPUs"
        ),
    )

    result = atf.run_job(
        f"--jobid={job_id} --overlap -n1 --mem=0 bash -c 'echo $SLURM_STEP_GPUS'",
        fatal=True,
        quiet=True,
    )
    second_step_gpus = result["stdout"].strip()

    assert second_step_gpus == _ALL_GPUS, (
        f"Second step SLURM_STEP_GPUS should be {_ALL_GPUS!r} (every GPU), "
        f"got {second_step_gpus!r} - an --overlap step running alongside "
        f"another step was not granted all of the job's GPUs"
    )


@pytest.mark.xfail(_OVERLAP_GRES_UNFIXED, reason=_OVERLAP_GRES_REASON)
@pytest.mark.parametrize("requested_gpus", range(2, _NUM_GPUS))
def test_overlap_step_gets_requested_gpu_subset(requested_gpus, single_socket_gpu_job):
    """An --overlap step requesting a subset gets that many distinct GPUs.

    A job step may explicitly request a different generic resource count
    than the job, so an --overlap step asking for fewer GPUs than the job
    holds must be granted exactly that many, and no device twice.
    """

    job_id = single_socket_gpu_job

    result = atf.run_job(
        f"--jobid={job_id} --overlap -n1 --mem=0 --gres=gpu:{requested_gpus} "
        f"bash -c 'echo $SLURM_STEP_GPUS'",
        fatal=True,
        quiet=True,
    )
    step_gpus = result["stdout"].strip()
    indices = step_gpus.split(",") if step_gpus else []

    assert len(indices) == requested_gpus, (
        f"Step requesting gpu:{requested_gpus} should be granted "
        f"{requested_gpus} GPUs, got SLURM_STEP_GPUS={step_gpus!r}"
    )
    assert len(set(indices)) == len(indices), (
        f"Step requesting gpu:{requested_gpus} should be granted distinct "
        f"GPUs, got SLURM_STEP_GPUS={step_gpus!r}"
    )


def test_non_overlap_step_gets_all_gpus():
    """A default (non --overlap) step also receives every GPU it requests.

    This is not a reproducer of ticket 24862 (that bug was specific to
    --overlap); it guards the shared non-overlap path against collateral
    breakage from the fix.  srun's own step lands on a single socket
    (CR_Socket_Memory with -n1) while requesting every GPU across all
    sockets, so it must still be granted all of them.
    """

    result = atf.run_job(
        f"-N1 -n1 --gpus-per-node={_NUM_GPUS} bash -c "
        f"'echo $SLURM_CPUS_ON_NODE; echo $SLURM_STEP_GPUS'",
        fatal=True,
        quiet=True,
    )
    output = result["stdout"].splitlines()
    assert len(output) >= 2, (
        f"Expected two output lines (SLURM_CPUS_ON_NODE and "
        f"SLURM_STEP_GPUS), got {result['stdout']!r}"
    )
    cpus_on_node = output[0].strip()
    step_gpus = output[1].strip()

    assert cpus_on_node == "1", (
        f"Expected the step to hold a single socket (1 CPU with "
        f"CoresPerSocket=1), got SLURM_CPUS_ON_NODE={cpus_on_node!r} - "
        f"topology did not confine the step to one socket, so it already "
        f"covers every GPU-bound socket"
    )
    assert step_gpus == _ALL_GPUS, (
        f"Non-overlap step SLURM_STEP_GPUS should be {_ALL_GPUS!r} "
        f"(every GPU), got {step_gpus!r}"
    )
