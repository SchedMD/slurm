############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A non-exclusive job must get every GPU it requested on RestrictedCoresPerGPU.

cons_tres counts the restricted cores in the job, rounds that count up by
RestrictedCoresPerGPU, and treats the result as already satisfied before it
places devices. When the pick on that socket comes up short, the count must be
reduced so the rest of selection places the remainder, or fails the node when
it cannot. Otherwise the job starts with fewer GPUs than it asked for. CPUs
are chosen earlier and stay at the request.

Layout (one node, 2 sockets x 16 cores, RestrictedCoresPerGPU=4):

  socket 0, cores 0-15:   1 GPU (IDX 3), cores 0-3 reserved, 4-15 CpuSpecList
  socket 1, cores 16-31:  3 GPUs (IDX 0-2), cores 16-27 reserved, 28-31 regular

Restricted cores are offered to a job only in complete groups of four, and
the filter keeps no more of them than four per requested GPU, trimming from
the lowest core up. Ordering is load-bearing:

  holder: one CPU, two GPUs. Socket 0 holds only one GPU, so it is not
          needed and its cores are dropped. The trim leaves the holder on
          core 20, which breaks one socket-1 group. It is charged for one GPU
          and topped up with a second, both from socket 1. Two complete
          socket-1 groups remain, but only one socket-1 GPU is free.
  victim: one task of twelve CPUs, two GPUs. One GPU is free on each socket.
          The trim drops cores 0-3, which leaves the victim exactly the two
          complete socket-1 groups plus 28-31. Its eight restricted cores
          charge it for two GPUs, the socket-1 pick finds one, and selection
          never reaches the free GPU on socket 0.

With --gres-flags=enforce-binding the victim may not take the socket-0 GPU,
since none of its cores are there, so the remainder cannot be placed. The
victim must then pend rather than start with one GPU, and start with two
once the holder ends. This holds for a per-node (--gres) and a per-job
(--gpus) request alike.

Ticket 26009.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

SOCKETS = 2
CORES_PER_SOCKET = 16
RES_CORES_PER_GPU = 4
SPEC_CORES = "4-15"
SOCKET1_RESTRICTED_CORES = set(range(16, 28))
# File order sets the indexes: tty0-2 are on socket 1, tty3 on socket 0.
SOCKET1_GPUS = {0, 1, 2}
VICTIM_CPUS = 12

# Per-node and per-job requests reach different GPU selection paths.
_VICTIM_GPU_OPTS = [
    pytest.param("--gres=gpu:2", id="gres"),
    pytest.param("--gpus=2", id="gpus"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Gate on version before any host write, so an old slurmctld skips cleanly.
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 26009: cons_tres under-allocated GPUs for RestrictedCoresPerGPU before 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CORE replaces any CR_* base a config variant set. Joining it with
    # CR_Core_Memory is rejected.
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    node = atf.require_nodes(
        1,
        [
            ("Gres", "gpu:4"),
            ("Sockets", SOCKETS),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
            ("RestrictedCoresPerGPU", RES_CORES_PER_GPU),
            ("CpuSpecList", SPEC_CORES),
        ],
    )[0]
    for tty in range(4):
        atf.require_tty(tty)

    atf.require_config_parameter(
        "NodeName",
        f"{node} Name=gpu Cores=16-31 File=/dev/tty[0-2]\n"
        f"NodeName={node} Name=gpu Cores=0-15 File=/dev/tty3",
        source="gres",
    )
    atf.require_slurm_running()
    return node


def _gpu_count(job):
    """Return the allocated GPU count from a job's GRES string, or 0."""
    gres = str(job.get("GRES") or "")
    for part in gres.split(","):
        part = part.strip()
        if part.startswith("gpu:"):
            return int(part.split(":")[1].split("(")[0])
    return 0


def _gpu_indexes(job):
    """Return the allocated GPU indexes from a job's GRES string."""
    match = re.search(r"gpu:[^(]*\(IDX:([0-9,\-]+)\)", str(job.get("GRES") or ""))
    if not match:
        return set()
    indexes = set()
    for part in match.group(1).split(","):
        if "-" in part:
            start, end = part.split("-")
            indexes.update(range(int(start), int(end) + 1))
        else:
            indexes.add(int(part))
    return indexes


def _submit_sleep(args):
    return atf.submit_job_sbatch(f'{args} --wrap "sleep infinity"', fatal=True)


def _submit(args):
    job_id = _submit_sleep(args)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job submitted as '{args}' should reach RUNNING: {atf.get_job(job_id)}"
    return atf.get_job(job_id)


def _node_field(node, field):
    return atf.run_command_output(
        f"scontrol show node {node} -o | grep -o '{field}=[^ ]*'",
        fatal=True,
    ).strip()


def _start_holder(node):
    """Check the node layout and start the holder that leaves the GPUs split.

    Returns the holder job record. One GPU is left free on each socket.
    """
    reserved = _node_field(node, "RestrictedCoresPerGPU")
    assert (
        reserved == "RestrictedCoresPerGPU=4(0-3,16-27)"
    ), f"Node {node} should reserve cores 0-3 and 16-27, got {reserved}"
    spec = _node_field(node, "CPUSpecList")
    assert (
        spec == f"CPUSpecList={SPEC_CORES}"
    ), f"Node {node} should specialize cores {SPEC_CORES}, got {spec}"

    holder = _submit(f"-c1 --gres=gpu:2 -w {node}")
    holder_cpus = set(atf.range_to_list(str(holder.get("CPU_IDs") or "")))
    assert holder_cpus <= SOCKET1_RESTRICTED_CORES and len(holder_cpus) == 1, (
        f"Holder should take one restricted socket-1 core, "
        f"got CPU_IDs={holder.get('CPU_IDs')}"
    )
    assert _gpu_count(holder) == 2 and _gpu_indexes(holder) <= SOCKET1_GPUS, (
        f"Holder should take two socket-1 GPUs, leaving one there and the "
        f"socket-0 GPU free, got GRES={holder.get('GRES')}"
    )
    return holder


def test_nonexclusive_job_gets_every_requested_gpu(setup):
    """A job charged for two restricted-core groups must be given two GPUs.

    The victim holds two complete socket-1 groups while only one socket-1 GPU
    is free and the other free GPU is on socket 0.
    """
    node = setup
    _start_holder(node)

    victim = _submit(f"-n1 -c{VICTIM_CPUS} --gres=gpu:2 -w {node}")
    assert (
        int(victim["NumCPUs"]) == VICTIM_CPUS
    ), f"Victim CPU count should stay at the request, got {victim['NumCPUs']}"
    assert _gpu_count(victim) == 2, (
        f"Victim requested 2 GPUs with 2 free on the node, so it should be "
        f"allocated 2, got GRES={victim.get('GRES')} "
        f"CPU_IDs={victim.get('CPU_IDs')} AllocTRES={victim.get('AllocTRES')}"
    )


@pytest.mark.parametrize("gpu_opt", _VICTIM_GPU_OPTS)
def test_enforce_binding_job_pends_until_every_gpu_fits(setup, gpu_opt):
    """A job that cannot place every GPU on its own sockets must pend.

    The only other free GPU is on socket 0, where the victim has no cores, so
    enforce-binding rules it out. Once the holder ends, the same job must
    start with every GPU it requested, each on a socket it holds CPUs on.
    """
    node = setup
    holder = _start_holder(node)

    victim_id = _submit_sleep(
        f"-n1 -c{VICTIM_CPUS} {gpu_opt} --gres-flags=enforce-binding -w {node}"
    )
    assert atf.wait_for_job_state(victim_id, "PENDING", "Resources"), (
        f"Victim cannot get 2 GPUs on its own socket while the holder runs, so "
        f"it should pend for Resources, got {atf.get_job(victim_id)}"
    )
    assert (
        _gpu_count(atf.get_job(victim_id)) == 0
    ), f"Pending victim should hold no GPUs, got {atf.get_job(victim_id)}"

    atf.cancel_jobs([holder["JobId"]], fatal=True)
    assert atf.wait_for_job_state(
        victim_id, "RUNNING"
    ), f"Victim should start once the holder ends: {atf.get_job(victim_id)}"
    victim = atf.get_job(victim_id)
    assert (
        int(victim["NumCPUs"]) == VICTIM_CPUS
    ), f"Victim CPU count should stay at the request, got {victim['NumCPUs']}"
    assert _gpu_count(victim) == 2, (
        f"Victim requested 2 GPUs with all 4 free, so it should be allocated "
        f"2, got GRES={victim.get('GRES')} CPU_IDs={victim.get('CPU_IDs')}"
    )
    cpu_sockets = {
        cpu // CORES_PER_SOCKET
        for cpu in atf.range_to_list(str(victim.get("CPU_IDs") or ""))
    }
    gpu_sockets = {1 if gpu in SOCKET1_GPUS else 0 for gpu in _gpu_indexes(victim)}
    assert gpu_sockets <= cpu_sockets, (
        f"enforce-binding allows GPUs only on sockets the victim holds CPUs "
        f"on, got GRES={victim.get('GRES')} CPU_IDs={victim.get('CPU_IDs')}"
    )
