############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify --gres-flags=enforce-links alongside RestrictedCoresPerGPU.

RestrictedCoresPerGPU holds a GPU's cores back for jobs requesting a GPU, and
makes cons_tres pick devices over an extra pass before the request is filled
the usual way. The link requirement has to hold across both.

The node's only linked pair spans the sockets and holds the second GPU of
each, so it is reached neither by index order nor within one socket.

The jobs below hold only unreserved cores, so the restricted core pass picks
nothing for them. Covering that pass as the one making the picks needs a job
charged a reserved core on each socket, which this file does not yet do.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

SOCKETS = 2
CORES_PER_SOCKET = 6
GPUS = 4
RES_CORES_PER_GPU = 2


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_version(
        (26, 11),
        "bin/scontrol",
        reason="Issue 51169: --gres-flags=enforce-links added in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Overwrite rather than add to what a config variant set: adding CR_CORE to
    # a variant that already selected another CR_* type is rejected by Slurm.
    atf.require_config_parameter("SelectTypeParameters", "CR_CORE")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(
        1,
        [
            ("Gres", f"gpu:{GPUS}"),
            ("Sockets", SOCKETS),
            ("CoresPerSocket", CORES_PER_SOCKET),
            ("ThreadsPerCore", 1),
            ("RestrictedCoresPerGPU", RES_CORES_PER_GPU),
        ],
    )

    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(GPUS):
        atf.run_command(f"touch {gpu_file}{i}", fatal=True)

    # GPUs 0,1 on socket 0 and GPUs 2,3 on socket 1, linked only as {1,3}. The
    # one linked pair holds a device of each socket, so no pass that picked
    # devices one socket at a time could return it; and it holds the second
    # device of each socket, so no pass that took a socket's devices in index
    # order could either.
    atf.require_config_parameter(
        "NodeName",
        f"""node1 Name=gpu File={gpu_file}0 Cores=0-5 Links=-1,0,0,0
        NodeName=node1 Name=gpu File={gpu_file}1 Cores=0-5 Links=0,-1,0,1
        NodeName=node1 Name=gpu File={gpu_file}2 Cores=6-11 Links=0,0,-1,0
        NodeName=node1 Name=gpu File={gpu_file}3 Cores=6-11 Links=0,1,0,-1""",
        source="gres",
    )
    atf.require_slurm_running()


def _parse_idx(idx_str):
    """Expand an 'IDX' string like '0,2-3' into a set of ints."""
    indexes = set()
    for part in idx_str.split(","):
        if "-" in part:
            start, end = part.split("-")
            indexes.update(range(int(start), int(end) + 1))
        else:
            indexes.add(int(part))
    return indexes


def _job_gpus_and_cpus(job_id):
    """Return (set of GPU indexes, set of CPU ids) allocated to a job."""
    out = atf.run_command_output(f"scontrol show job {job_id} -d", fatal=True)
    gpus = re.search(r"GRES=gpu:[^(]*\(IDX:([0-9,\-]+)\)", out)
    cpus = re.search(r"CPU_IDs=([0-9,\-]+)", out)
    assert gpus is not None, f"No GPU IDX for job {job_id}:\n{out}"
    assert cpus is not None, f"No CPU_IDs for job {job_id}:\n{out}"
    return _parse_idx(gpus.group(1)), _parse_idx(cpus.group(1))


def test_config():
    """The node reserves the expected cores, so the sets the tests below check
    against are the ones Slurm is actually using."""
    expected = "2(0-3,6-9)"
    res_cores = atf.run_command_output(
        "scontrol show nodes node1 | awk -F 'RestrictedCoresPerGPU=' "
        "'{print $2}' | tr -d ' \n'",
        fatal=True,
    )
    assert (
        res_cores == expected
    ), f"RestrictedCoresPerGPU is {res_cores}, expected {expected}"


def test_enforce_links_gets_the_linked_pair():
    """A 2-GPU job must still get the linked pair {1,3} on a node holding cores
    back for its GPUs."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=2 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "the job should run"

    gpus, _ = _job_gpus_and_cpus(job_id)
    assert gpus == {
        1,
        3,
    }, f"enforce-links should give the linked pair {{1, 3}}, got {sorted(gpus)}"


def test_non_enforce_two_gpu_runs_unlinked():
    """Counter to the case above: without the flag the job still runs, and is
    given a pair that is not the linked one, so the case above is neither a
    shortage nor the default preference."""
    job_id = atf.submit_job_sbatch(
        '--gpus-per-node=2 -w node1 --wrap "sleep infinity"', fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "a 2-GPU job should run without the flag"

    gpus, _ = _job_gpus_and_cpus(job_id)
    assert gpus != {
        1,
        3,
    }, "without the flag the job should not need the linked pair"


def test_enforce_links_single_gpu_runs():
    """A lone device has nothing to link to, so it is unaffected."""
    job_id = atf.submit_job_sbatch(
        '--gres-flags=enforce-links --gpus-per-node=1 -w node1 --wrap "sleep infinity"',
        fatal=True,
    )
    assert atf.wait_for_job_state(job_id, "RUNNING"), "a one-GPU job should run"

    gpus, _ = _job_gpus_and_cpus(job_id)
    assert len(gpus) == 1, f"expected one GPU, got {sorted(gpus)}"
