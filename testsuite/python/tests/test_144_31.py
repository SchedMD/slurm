############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES chunk configuration edits and validation (gres.conf ChunkSizes).

Each case rewrites gres.conf and restarts only slurmd, which is what an
administrator does after editing a node. Going through set_config_parameter()
would reconfigure the controller too, rebuilding its node state and hiding
what the registration itself decided.

The node has six devices: two of Type plain and four of Type chunked, so drain
checks can keep each Type's declared count and only fail on the chunk rule.
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow

PLAIN = "Name=gpu Type=plain File=/dev/tty[4-5]"
CHUNKED = "Name=gpu Type=chunked File=/dev/tty[0-3]"
BASELINE = [f"{CHUNKED} ChunkSizes=2", PLAIN]
# Each chunked device given as two files, for the MultipleFiles case
TTY_CNT = 10

node_name = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global node_name

    # slurmd parses ChunkSizes/ChunkOrder, slurmctld allocates by them and
    # scontrol shows them
    for component in ["sbin/slurmd", "bin/scontrol"]:
        atf.require_version(
            (26, 11),
            component,
            reason="Issue 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
        )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_nodes(1, [("CPUs", 8), ("Gres", "gpu:chunked:4,gpu:plain:2")])
    for i in range(TTY_CNT):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and "gpu:chunked:4" in str(params.get("Gres", ""))
    )
    assert gpu_nodes, "Expected require_nodes() to configure a node with GPUs"
    node_name = gpu_nodes[0]

    atf.require_config_parameter("NodeName", _body(BASELINE), source="gres")
    atf.require_slurm_running()


def _body(records):
    return "\nNodeName=".join(f"{node_name} {r}" for r in records)


def _write_gres_conf(records):
    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/gres.conf",
        input=f"NodeName={_body(records)}\n",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _stop_slurmd():
    """Stop the node's slurmd, False if none was running."""

    if not atf.slurmd_pids(node_name):
        return False
    atf.stop_slurmd(node_name)
    return True


def _reload_slurmd(records):
    """Restart slurmd on the new gres.conf, then let the caller wait.

    Each caller polls for the result it expects. Waiting for a registration
    here instead can miss one: slurmd_start_time has one second resolution,
    so a restart right after the previous start looks unchanged.
    """

    _write_gres_conf(records)
    assert atf.slurmd_pids(node_name), f"Expected a slurmd for {node_name}"
    atf.start_slurmd(node_name, wait="process", quiet=True)


def _all_gres_chunks():
    """Return {gres: (sizes, order)} from scontrol -d, one entry per Type."""

    output = atf.run_command_output(
        f"scontrol show node -d {node_name}", fatal=True, quiet=True
    )
    return {
        gres: (sizes, order)
        for gres, sizes, order in re.findall(
            r"([\w:]+):sizes=([^;,\s]+);order=([^;,\s]+)",
            (re.search(r"GresChunks=(\S+)", output) or [None, ""])[1],
        )
    }


def _gres_chunks():
    """Return the chunked Type's sizes and order, or (None, None)."""

    return _all_gres_chunks().get("gpu:chunked", (None, None))


def _wait_for_chunks(expected):
    """atf.start_slurmd() returns before the node re-registers, so poll."""

    reported = None
    for _ in atf.timer():
        reported = _gres_chunks()
        if reported == expected:
            return
    pytest.fail(f"Expected GresChunks {expected}, got {reported}")


@pytest.fixture
def restore():
    """Start from and return to a healthy baseline.

    The restore goes through a distinct layout first, so it proves a fresh
    registration was processed even if the controller kept sizes=2 through a
    rejected edit.
    """

    atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)
    _wait_for_chunks(("2", "0/1/2/3"))
    yield
    atf.cancel_all_jobs(fatal=True)
    _reload_slurmd([f"{CHUNKED} ChunkSizes=4", PLAIN])
    _wait_for_chunks(("4", "0/1/2/3"))
    _reload_slurmd(BASELINE)
    _wait_for_chunks(("2", "0/1/2/3"))
    atf.run_command(
        f"scontrol update nodename={node_name} state=resume",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)


@pytest.mark.parametrize(
    "chunk_params, expected",
    [
        ("ChunkSizes=2,4", ("2/4", "0/1/2/3")),
        ("ChunkSizes=2 ChunkOrder=0,2,1,3", ("2", "0/2/1/3")),
    ],
    ids=["sizes", "order"],
)
def test_edit_is_picked_up_on_slurmd_restart(restore, chunk_params, expected):
    """An edit to the chunk options alone must not be ignored.

    Device counts, types and record counts are unchanged, so the controller
    has to compare the chunk config itself.
    """

    _reload_slurmd([f"{CHUNKED} {chunk_params}", PLAIN])
    _wait_for_chunks(expected)


def test_edit_is_picked_up_on_reconfigure(restore):
    """scontrol reconfigure is the other way to apply an edit."""

    _write_gres_conf([f"{CHUNKED} ChunkSizes=4", PLAIN])
    atf.run_command(
        "scontrol reconfigure",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    _wait_for_chunks(("4", "0/1/2/3"))
    error = atf.run_command_error(
        f"sbatch -N1 -w {node_name} --gres=gpu:chunked:2 --wrap 'true'",
        xfail=True,
        fatal=True,
    )
    assert "Requested node configuration is not available" in error, error


def _run(job_args):
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} {job_args} --wrap 'sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Job {job_id} ({job_args}) should reach RUNNING"
    return job_id


def test_removal_turns_chunks_off(restore):
    _reload_slurmd([CHUNKED, PLAIN])
    _wait_for_chunks((None, None))
    output = atf.run_command_output(
        f"scontrol show node -d {node_name}", fatal=True, quiet=True
    )
    assert (
        "GresChunks" not in output
    ), "A node without chunks should have no GresChunks line"
    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node_name} --gres=gpu:chunked:3 --wrap 'true'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), "After removing ChunkSizes, a three device request must run"


def test_chunk_state_of_each_type_survives_slurmctld_restart(restore):
    """Two Types may each set ChunkSizes, and both survive a slurmctld restart.

    slurmd is stopped before the restart, so what scontrol reports afterwards
    has to come from the node state file. Otherwise the node schedules with
    chunks off until slurmd checks back in.
    """

    expected = {"gpu:chunked": ("2", "0/1/2/3"), "gpu:plain": ("1/2", "0/1")}
    _reload_slurmd([f"{CHUNKED} ChunkSizes=2", f"{PLAIN} ChunkSizes=1,2"])
    for _ in atf.timer():
        if _all_gres_chunks() == expected:
            break
    else:
        pytest.fail(f"Expected GresChunks {expected}, got {_all_gres_chunks()}")
    state = atf.get_node_parameter(node_name, "state")
    assert "DRAIN" not in str(state), f"{node_name} drained: {state}"

    assert _stop_slurmd(), f"Expected to stop the slurmd for {node_name}"
    atf.restart_slurmctld(quiet=True)
    restored = _all_gres_chunks()
    atf.start_slurmd(node_name, quiet=True)
    assert (
        restored == expected
    ), f"GresChunks after a slurmctld restart was {restored}, expected {expected}"


@pytest.mark.parametrize(
    "records, diagnostic",
    [
        ([f"{CHUNKED} ChunkSizes=2,3,6", PLAIN], "invalid ChunkSizes 2,3,6"),
        ([f"{CHUNKED} ChunkSizes=4,2", PLAIN], "invalid ChunkSizes 4,2"),
        ([f"{CHUNKED} ChunkSizes=abc", PLAIN], "invalid ChunkSizes abc"),
        ([f"{CHUNKED} ChunkSizes=2,2", PLAIN], "invalid ChunkSizes 2,2"),
        ([f"{CHUNKED} ChunkSizes=0", PLAIN], "invalid ChunkSizes 0"),
        ([f"{CHUNKED} ChunkSizes=-2", PLAIN], "invalid ChunkSizes -2"),
        (
            [f"{CHUNKED} ChunkSizes=2 ChunkOrder=a,b", PLAIN],
            "invalid ChunkOrder a,b",
        ),
        (
            [
                f"{CHUNKED} ChunkSizes=2 "
                f"ChunkOrder={','.join(str(i) for i in range(1025))}",
                PLAIN,
            ],
            "has too many entries",
        ),
        (
            [f"{CHUNKED} ChunkSizes=2 ChunkOrder=0,1,1,3", PLAIN],
            "invalid ChunkOrder 0,1,1,3",
        ),
        (
            [f"{CHUNKED} ChunkSizes=2 ChunkOrder=4,5,6,7", PLAIN],
            "invalid ChunkOrder 4,5,6,7",
        ),
        ([f"{CHUNKED} ChunkOrder=0,1,2,3", PLAIN], "ChunkOrder requires ChunkSizes"),
        (
            ["Name=gpu Type=chunked Count=4 ChunkSizes=2", PLAIN],
            "ChunkSizes requires File or MultipleFiles",
        ),
        (
            [
                "Name=gpu Type=chunked File=/dev/tty[0-1] ChunkSizes=2",
                "Name=gpu Type=chunked File=/dev/tty[2-3] ChunkSizes=4",
                PLAIN,
            ],
            "ChunkSizes and ChunkOrder must match",
        ),
        (
            [
                "Name=gpu Type=chunked File=/dev/tty[0-1] ChunkSizes=2",
                "Name=gpu Type=chunked File=/dev/tty[2-3]",
                PLAIN,
            ],
            "ChunkSizes and ChunkOrder must match",
        ),
        (
            [
                "Name=gpu Type=chunked File=/dev/tty[0-1] ChunkSizes=2 "
                "ChunkOrder=0,1,2,3",
                "Name=gpu Type=chunked File=/dev/tty[2-3] ChunkSizes=2 "
                "ChunkOrder=1,0,2,3",
                PLAIN,
            ],
            "ChunkSizes and ChunkOrder must match",
        ),
    ],
    ids=[
        "not_a_multiple",
        "not_ascending",
        "unparsable",
        "repeated_size",
        "zero_size",
        "negative_size",
        "order_unparsable",
        "order_too_long",
        "order_not_a_permutation",
        "order_outside_domain",
        "order_without_sizes",
        "no_file",
        "lines_disagree",
        "line_omits_sizes",
        "order_lines_disagree",
    ],
)
def test_invalid_config_stops_slurmd(records, diagnostic):
    """slurmd must log the error and exit rather than register.

    slurmd runs in the foreground, since a daemonizing slurmd forks before it
    reads gres.conf. order_outside_domain names devices the Type does not
    have. line_omits_sizes is a second line of the Type that leaves ChunkSizes
    out, which is an error rather than inherited.
    """

    _write_gres_conf(records)
    assert _stop_slurmd(), f"Expected to stop the slurmd for {node_name}"
    result = atf.run_command(
        f"timeout 15 {atf.properties['slurm-sbin-dir']}/slurmd -D -N {node_name}",
        user="root",
        xfail=True,
        quiet=True,
    )
    # Put the node back before asserting, so a failure does not strand it
    _write_gres_conf(BASELINE)
    atf.start_slurmd(node_name, quiet=True)

    assert result["exit_code"] == 1, result
    assert diagnostic in result["stderr"], result


@pytest.mark.parametrize(
    "records, diagnostic",
    [
        (
            [f"{CHUNKED} ChunkSizes=2,8", PLAIN],
            r"ChunkSizes value \(2,8\) is larger than the 4 devices configured",
        ),
        (
            [f"{CHUNKED} ChunkSizes=3", PLAIN],
            r"(?i)chunk.*divide",
        ),
        (
            [f"{CHUNKED} ChunkSizes=2 ChunkOrder=0,1", PLAIN],
            r"ChunkOrder value \(0,1\) has 2 entries but 4 devices",
        ),
    ],
    ids=[
        "size_above_domain",
        "size_does_not_divide",
        "order_length_mismatch",
    ],
)
def test_unhonourable_config_drains_node(restore, records, diagnostic):
    """The controller drains a node whose chunk layout does not fit its devices.

    slurmd only sees gres.conf text, so it starts on all of these.
    """

    _reload_slurmd(records)
    atf.wait_for_node_state(node_name, "DRAIN", fatal=True)
    reason = str(atf.get_node_parameter(node_name, "reason"))
    assert re.search(diagnostic, reason), f"Expected {diagnostic!r}, got {reason!r}"
    assert (
        _all_gres_chunks() == {}
    ), "A rejected layout should not be reported as GresChunks"


def test_multiple_files_satisfy_the_file_requirement(restore):
    """MultipleFiles gives a chunked device its files as well as File does."""

    _reload_slurmd(
        [
            f"Name=gpu Type=chunked MultipleFiles=/dev/tty{i},/dev/tty{i + 6} "
            "ChunkSizes=2,4"
            for i in range(4)
        ]
        + [PLAIN]
    )
    _wait_for_chunks(("2/4", "0/1/2/3"))
    state = atf.get_node_parameter(node_name, "state")
    assert "DRAIN" not in str(state), f"{node_name} drained: {state}"


def test_explicit_gres_in_a_whole_node_job(restore):
    """Flags=explicit still only gives what was asked for, in any count.

    A whole-node job is exempt from the chunk sizes, so 3 is allowed, and it
    gets exactly the devices it requested rather than every device.
    """

    _reload_slurmd(
        [f"{CHUNKED} ChunkSizes=2,4 Flags=explicit", f"{PLAIN} Flags=explicit"]
    )
    _wait_for_chunks(("2/4", "0/1/2/3"))
    atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)

    job_id = _run("--exclusive --gres=gpu:chunked:3")
    indexes = atf.get_job_gres_indexes(job_id)
    assert len(indexes) == 3 and indexes <= {
        0,
        1,
        2,
        3,
    }, f"Expected three chunked devices, got {indexes}"
    atf.cancel_jobs([job_id], fatal=True)

    job_id = _run("--exclusive")
    output = atf.run_command_output(
        f"scontrol show job -d {job_id}", fatal=True, quiet=True
    )
    assert "IDX:" not in output, "A job that asks for no GPU should get none"


@pytest.mark.parametrize("spec", ["--core-spec=1", "--thread-spec=1"])
def test_spec_options_make_a_whole_node_job(spec):
    """With AllowSpecResourcesUsage, --core-spec and --thread-spec are exempt.

    The job holds the node to itself, so it gets every device on it, even for
    a count that is not a chunk size.
    """

    atf.set_config_parameter("AllowSpecResourcesUsage", "YES")
    atf.wait_for_node_state(node_name, "IDLE", operator=atf.SetMatch.EQUAL, fatal=True)
    job_id = _run(f"{spec} --gres=gpu:chunked:3")
    assert atf.get_job_gres_indexes(job_id) == set(
        range(6)
    ), "A whole-node job should receive every device on the node"
