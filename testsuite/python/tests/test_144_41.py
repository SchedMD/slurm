############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GPUs that report no UUID are numbered when the controller builds the node.

A device without a UUID has no saved UUID order. Its index is its place in the
report, and slurmd reports its GPUs in device file order. Removing the first
GPU and lowering Gres, adding one in the middle, and moving the GPU types to
other device files at the same count all follow that order. The index is
read from a job, which is how this numbering is seen without a per-device
drain. The job's step has to run on a device of the type the job asked for,
so a controller numbering that is not slurmd's shows up as the wrong device.

Devices that report a UUID keep the order slurmctld saved. An index whose
device is absent stays empty. That order is covered by test_190_17.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

NODE = "node1"

# tty, type, uuid. The tty is not the index: after a device is removed, the
# next device keeps its tty and takes the lower index.
A100 = (0, "a100", "(null)")
V100 = (1, "v100", "(null)")
H100 = (2, "h100", "(null)")


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        component="sbin/slurmd",
        reason="Issue 50653: gres.conf Autodetect=full added in 26.05",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # A short registration has to mark the node invalid. Either of these would
    # keep it up and hide the Gres edit that follows.
    atf.require_config_parameter_excludes("SlurmctldParameters", "drain_absent_gres")
    atf.require_config_parameter_excludes("SlurmdParameters", "config_overrides")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_config_file("gres.conf", "Autodetect=full\n")
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(A100, V100, H100))
    atf.require_nodes(1, [("Gres", _gres(A100, V100, H100)), ("CPUs", 2)])
    atf.require_slurm_running()


def _gres(*devices):
    """Gres= value whose per-type counts match ``devices``."""
    counts = {}
    order = []
    for _tty, gpu_type, _uuid in devices:
        if gpu_type not in counts:
            order.append(gpu_type)
            counts[gpu_type] = 0
        counts[gpu_type] += 1
    return ",".join(f"gpu:{gpu_type}:{counts[gpu_type]}" for gpu_type in order)


def _on_tty(tty, device):
    """The same GPU type, backed by another /dev/tty."""
    return (tty, device[1], device[2])


def _detail():
    return atf.get_node_detail(NODE)


def _gres_matches(detail, devices):
    match = re.search(r"\bGres=(\S+)", detail or "")
    if not match:
        return False
    return set(match.group(1).split(",")) == set(_gres(*devices).split(","))


def _wait_gres(*devices):
    for _ in atf.timer(timeout=60, fatal=True):
        if _gres_matches(_detail(), devices):
            break


def _resume():
    atf.run_command(
        f"scontrol update NodeName={NODE} State=RESUME",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _reset(*devices):
    """Register exactly these devices, as the node's first registration.

    slurmd is stopped, then slurmctld is restarted with a clean state, so
    nothing saved from the previous device set can number these. The
    controller is stopped before the Gres edit: set_node_parameter()
    restarts with saved state while slurmctld is up.
    """
    atf.stop_slurmd(NODE, quiet=True)
    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(*devices))
    atf.stop_slurmctld(quiet=True)
    atf.set_node_parameter(NODE, "Gres", _gres(*devices))
    atf.restart_slurmctld(clean=True, quiet=True)
    atf.start_slurmd(NODE)
    _resume()
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_gres(*devices)


def _index(gpu_type, devices):
    """Index a one-GPU job of ``gpu_type`` is given.

    ``devices`` is the fake_gpus.conf slurmd runs with. slurmd numbers its
    GPUs in device file order and puts that number in SLURM_STEP_GPUS, so the
    number names the device the job's step was bound to. That device has to
    be a ``gpu_type``.
    """
    job_id = atf.submit_job_sbatch(
        f"-w {NODE} --gres=gpu:{gpu_type}:1 --wrap 'sleep infinity'",
        fatal=True,
    )
    try:
        assert atf.wait_for_job_state(job_id, "RUNNING", timeout=60), atf.get_job(
            job_id
        )
        out = atf.run_command_output(
            f"scontrol -d show job {job_id}", fatal=True, quiet=True
        )
        step_gpus = atf.run_job_output(
            f"--jobid={job_id} --overlap --gres=gpu:{gpu_type}:1 "
            "printenv SLURM_STEP_GPUS",
            fatal=True,
        ).strip()
    finally:
        atf.cancel_jobs([job_id], fatal=True, quiet=True)
    match = re.search(rf"GRES=gpu:{gpu_type}:1\(IDX:(\d+)\)", out)
    assert match, out
    tty, bound_type, _uuid = sorted(devices)[int(step_gpus)]
    assert (
        bound_type == gpu_type
    ), f"the {gpu_type} job's step was bound to /dev/tty{tty}, a {bound_type}"
    return int(match.group(1))


def _indexes(devices, **expected):
    for gpu_type, index in expected.items():
        assert _index(gpu_type, devices) == index, _detail()


def test_remove_first_gpu_then_lower_configured_count():
    """Register three GPUs, remove the first, then set Gres one type lower.

    The short registration marks the node invalid and drains it. The Gres
    edit clears the invalid flag. The registration drain stays until
    State=RESUME. The two GPUs that remain are numbered from the new
    report, so each takes the index below the one it had.
    """
    _reset(A100, V100, H100)
    _indexes((A100, V100, H100), a100=0, v100=1, h100=2)

    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(V100, H100))
    atf.start_slurmd(NODE)
    atf.wait_for_node_state(NODE, "INVALID_REG", fatal=True)
    assert "lower than configured" in (_detail() or ""), _detail()

    atf.set_node_parameter(NODE, "Gres", _gres(V100, H100))
    atf.wait_for_node_state(NODE, ["INVALID_REG", "DOWN"], reverse=True, fatal=True)
    assert "DRAIN" in (atf.get_node_field(NODE, "State") or ""), _detail()
    _wait_gres(V100, H100)

    _resume()
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _indexes((V100, H100), v100=0, h100=1)


def test_same_count_swap_follows_the_report():
    """Two GPU types swapping device files swap indexes too.

    The configured count stays 3. Only slurmd restarts. The v100 is now on
    the first device file, so slurmd reports it first, and the controller
    numbers it 0.
    """
    # 26.05 keeps the old type on each index when the count does not change.
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="a same-count GPU type change rebuilds the topology in 26.11",
    )
    _reset(A100, V100, H100)
    _indexes((A100, V100, H100), a100=0, v100=1, h100=2)

    swapped = (_on_tty(0, V100), _on_tty(1, A100), H100)
    atf.restart_slurmd_with_fake_gpus(NODE, *swapped)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _indexes(swapped, a100=1, v100=0, h100=2)


def test_adding_a_gpu_in_the_middle_shifts_later_indexes():
    """A GPU inserted into the report takes that index and shifts the rest.

    The device that was last is registered on the same tty, at the new
    high index.
    """
    _reset(A100, H100)
    _indexes((A100, H100), a100=0, h100=1)

    atf.require_config_file("fake_gpus.conf", atf.fake_gpus_conf(A100, V100, H100))
    atf.set_node_parameter(NODE, "Gres", _gres(A100, V100, H100))
    _resume()
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _wait_gres(A100, V100, H100)
    _indexes((A100, V100, H100), a100=0, v100=1, h100=2)


def test_same_count_reorder_follows_the_report():
    """Every GPU type moving to another device file is renumbered.

    The configured count stays 3. Only slurmd restarts. Reversing the device
    files reverses the order slurmd reports the types in, and the indexes
    with it.
    """
    # 26.05 keeps the old type on each index when the count does not change.
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="a same-count GPU type change rebuilds the topology in 26.11",
    )
    _reset(A100, V100, H100)
    _indexes((A100, V100, H100), a100=0, v100=1, h100=2)

    reordered = (_on_tty(0, H100), _on_tty(1, V100), _on_tty(2, A100))
    atf.restart_slurmd_with_fake_gpus(NODE, *reordered)
    atf.wait_for_node_state(NODE, "IDLE", operator="==", fatal=True)
    _indexes(reordered, a100=2, v100=1, h100=0)
