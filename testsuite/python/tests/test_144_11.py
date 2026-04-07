############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import logging

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("needs to add a future node to slurm.conf and reconfigure")
    atf.require_accounting()
    atf.require_config_parameter("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(2, [("Gres", "gpu:2")])
    gpu_file = f"{atf.module_tmp_path}/gpu"
    for i in range(2):
        atf.run_command(f"touch {gpu_file}{i}")
    atf.require_config_parameter(
        "NodeName",
        f"node[1-2] Name=gpu File={gpu_file}[0-1]",
        source="gres",
    )
    atf.require_version(
        (26, 5),
        "sbin/slurmctld",
        reason="Bug 24936 reservation / node reindex coverage requires Slurm 26.05+",
    )
    atf.require_slurm_running()


def test_resv_gres_survives_node_reindex():
    """Bug 24936: Verify slurmctld doesn't crash and GRES reservations
    remain functional after a reconfigure that reorders the node table.

    Reservation gres_list_alloc arrays are indexed by global node table
    position. When a node that sorts earlier is added, every existing node
    shifts to a higher index. The persisted arrays still use the old
    indices. Without the fix the scheduler would access out-of-bounds
    memory and segfault.
    """

    resv_name = "test144_11_resv"
    slurm_user = atf.properties["slurm-user"]
    test_user = atf.properties["test-user"]

    nodes = atf.get_nodes(live=False, quiet=True)
    node_names = sorted(n for n in nodes if n != "DEFAULT")
    assert len(node_names) >= 2, "Need at least 2 nodes"

    # Reserve the last node (highest index) so the reindex shifts it
    target_node = node_names[-1]

    try:
        atf.run_command(
            f"scontrol create reservation reservationname={resv_name} "
            f"user={test_user} start=now duration=120 "
            f"nodes={target_node} TRES=gres/gpu=2",
            user=slurm_user,
            fatal=True,
        )

        tres = atf.get_reservation_parameter(resv_name, "TRES")
        assert tres and "gres/gpu" in tres, f"Reservation should have GRES TRES: {tres}"
        logging.info(f"Created reservation {resv_name} on {target_node}")

        # Add a node whose name sorts before every existing node, forcing
        # all current nodes to shift to higher indices. Append a minimal
        # FUTURE line to slurm.conf, then scontrol reconfigure. The state
        # file still has the old indices; the fix remaps the reservation
        # GRES arrays.
        future_node = "aaa1"
        config_file = f"{atf.properties['slurm-config-dir']}/slurm.conf"
        atf.run_command(
            f"echo 'NodeName={future_node} State=FUTURE' >> {config_file}",
            user=slurm_user,
            fatal=True,
            quiet=True,
        )
        atf.run_command("scontrol reconfigure", user=slurm_user, fatal=True)

        assert (
            atf.is_slurmctld_running()
        ), "slurmctld must survive reconfigure with reordered node table"
        atf.wait_for_node_state(target_node, "IDLE", timeout=30)

        # Verify the reservation survived with its GRES intact
        tres = atf.get_reservation_parameter(resv_name, "TRES")
        assert (
            tres and "gres/gpu" in tres
        ), f"Reservation should still have GRES after reconfigure: {tres}"
        resv_nodes = atf.get_reservation_parameter(resv_name, "Nodes")
        assert target_node in str(
            resv_nodes
        ), f"Reservation should still include {target_node}: {resv_nodes}"

        # Submit a job through the reservation requesting GRES. The
        # scheduler walks the reservation's per-node GRES arrays using
        # current node indices -- stale arrays would segfault.
        job_id = atf.submit_job_sbatch(
            f"--reservation={resv_name} -w {target_node} "
            f"--gres=gpu:1 --wrap 'hostname'",
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "COMPLETED", timeout=30, fatal=True)

        alloc_tres = atf.get_job_parameter(job_id, "AllocTRES")
        assert (
            alloc_tres and "gres/gpu=1" in alloc_tres
        ), f"Job should have GRES allocation: {alloc_tres}"

    finally:
        if atf.is_slurmctld_running(quiet=True):
            atf.run_command(
                f"scontrol delete reservation {resv_name}",
                user=slurm_user,
            )
