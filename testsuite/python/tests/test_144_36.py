############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunks on GPUs that slurmd detects.

fake_gpus.conf stands in for a GPU library, so slurmd merges the detected
devices with the gres.conf records as AutoDetect does. The chunk settings can
only come from gres.conf, so they must survive that merge, and the link data
that detection adds must not decide the chunks.
"""

import pytest

import atf

GPUS = 4
# Links on the detected devices pair 1 and 2, across the boundary of the
# chunks of 2
FAKE_LINKS = ["-1,0,0,0", "0,-1,1,0", "0,1,-1,0", "0,0,0,-1"]
PAIRS = [{0, 1}, {2, 3}]

node_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name

    # slurmd parses ChunkSizes/ChunkOrder and scontrol shows them
    for component in ["sbin/slurmd", "bin/scontrol"]:
        atf.require_version(
            (26, 11),
            component,
            reason="Issue 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
        )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("SlurmdParameters", "config_overrides")
    atf.require_nodes(1, [("CPUs", GPUS), ("Gres", f"gpu:{GPUS}")])
    for i in range(GPUS):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and f"gpu:{GPUS}" in str(params.get("Gres", ""))
    )
    assert gpu_nodes, "Expected require_nodes() to configure a node with GPUs"
    node_name = gpu_nodes[0]

    atf.require_config_file(
        "fake_gpus.conf",
        "".join(
            f"(null)|{GPUS}|0-{GPUS - 1}|{links}|/dev/tty{i}|(null)|nvidia_gpu_env\n"
            for i, links in enumerate(FAKE_LINKS)
        ),
    )
    atf.require_config_file(
        "gres.conf", _gres_conf(f"File=/dev/tty[0-{GPUS - 1}] ChunkSizes=2")
    )
    atf.require_slurm_running()


def _gres_conf(record):
    return f"NodeName={node_name} Name=gpu {record}\n"


def test_chunk_config_survives_detection():
    assert atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL), (
        f"{node_name} should register IDLE (state="
        f"{atf.get_node_parameter(node_name, 'state')}, "
        f"reason={atf.get_node_parameter(node_name, 'reason')})"
    )
    log_file = atf.get_config_parameter("SlurmdLogFile").replace("%n", node_name)
    assert (
        atf.run_command_exit(
            f"grep -q 'Adding fake system GPU data' {log_file}", user="root"
        )
        == 0
    ), f"Expected slurmd to merge the fake GPUs, see {log_file}"
    output = atf.run_command_output(
        f"scontrol show node -d {node_name}", fatal=True, quiet=True
    )
    assert (
        "GresChunks=gpu:sizes=2;order=0/1/2/3" in output
    ), f"Expected the chunk settings of gres.conf to survive the merge: {output}"


@pytest.mark.parametrize("flags", ["", "--gres-flags=enforce-links"])
def test_detected_links_do_not_decide_chunks(flags):
    """Two jobs get the aligned pairs, not the linked devices 1 and 2."""

    allocated = []
    for _ in PAIRS:
        job_id = atf.submit_job_sbatch(
            f"-N1 -w {node_name} --gres=gpu:2 {flags} --wrap 'sleep infinity'",
            fatal=True,
        )
        assert atf.wait_for_job_state(
            job_id, "RUNNING"
        ), f"Job {job_id} should reach RUNNING"
        allocated.append(atf.get_job_gres_indexes(job_id))
    for indexes in allocated:
        assert indexes in PAIRS, f"Expected one of {PAIRS}, got {indexes}"
    assert allocated[0] != allocated[1], f"Two jobs share chunk {allocated[0]}"


def test_chunk_sizes_without_file_stops_slurmd():
    """A detected node still needs File on the record that sets ChunkSizes."""

    gres_conf = f"{atf.properties['slurm-config-dir']}/gres.conf"
    atf.stop_slurmd(node_name)
    atf.run_command(
        f"cat > {gres_conf}",
        input=_gres_conf(f"Count={GPUS} ChunkSizes=2"),
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    result = atf.run_command(
        f"timeout 15 {atf.properties['slurm-sbin-dir']}/slurmd -D -N {node_name}",
        user="root",
        xfail=True,
        quiet=True,
    )
    # Put the node back before asserting, so a failure does not strand it
    atf.run_command(
        f"cat > {gres_conf}",
        input=_gres_conf(f"File=/dev/tty[0-{GPUS - 1}] ChunkSizes=2"),
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    atf.start_slurmd(node_name, quiet=True)

    assert result["exit_code"] == 1, result
    assert "ChunkSizes requires File or MultipleFiles" in result["stderr"], result
