############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""GRES device chunk allocation (gres.conf ChunkSizes/ChunkOrder).

Four nodes, each with a different static chunk layout, so every allocation
rule is checked without rewriting gres.conf. Every job is pinned with -w to
the node whose layout it checks.
"""

import json
import re

import pytest

import atf

# Invalid for every layout below, so every chunk rule rejects it.
BAD_COUNT = 3

# "offset": a non-chunked Type holds devices 0-2 and the chunked Type 3-6, so
# chunk positions and device indexes differ, and pairs aligned from device 0
# would give {4, 5} instead. Links on the chunked records only pairs devices 4
# and 5, across the chunk boundary, and must be ignored. Links on the plain
# records pairs devices 0 and 1, and must still count.
PLAIN_GPUS = 3
OFFSET_GPUS = 4
OFFSET_PAIRS = [{3, 4}, {5, 6}]
# "order": the same devices, as two chunked Types with their own sizes.
# ChunkOrder numbers the chunked Type from 0, so chunk positions 0-1 hold its
# devices 0 and 2, which are device indexes 3 and 5.
PLAIN_SIZES = "1,3"
CHUNK_ORDER = "0,2,1,3"
ORDER_PAIRS = [{3, 5}, {4, 6}]
# "untyped": no Type and no Cores, the gres.html example shape, split over two
# lines that must both set ChunkSizes to form one domain
UNTYPED_GPUS = 4
# "six": one Type, several sizes so best fit is observable, largest not a power
# of two. Two sockets, so a whole-node --gpus-per-socket request fits.
SIX_GPUS = 6
SIX_SIZES = "1,2,6"

node = {}


@pytest.fixture(scope="module", autouse=True)
def setup():
    # slurmd parses ChunkSizes/ChunkOrder, slurmctld allocates by them and
    # scontrol shows them
    for component in ["sbin/slurmd", "sbin/slurmctld", "bin/scontrol"]:
        atf.require_version(
            (26, 11),
            component,
            reason="Bug 51025: gres.conf ChunkSizes/ChunkOrder are new in 26.11",
        )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # CR_CPU keeps a single-task job off a whole socket, so several chunk jobs
    # fit at once. Overwrite rather than extend: the stock cr_core_memory
    # cannot be combined with CR_CPU, and scontrol rejects the pair.
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # A constrained job sees its devices renumbered from 0, which would hide a
    # ChunkOrder permutation leaking into the job environment.
    atf.require_config_parameter("ConstrainDevices", "no", source="cgroup")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # Logs which chunk rule turned a node down
    atf.require_config_parameter_includes("DebugFlags", "SelectType")
    atf.require_nodes(4, [("CPUs", 8), ("Gres", f"gpu:{SIX_GPUS}")])
    for i in range(max(SIX_GPUS, PLAIN_GPUS + OFFSET_GPUS)):
        atf.require_tty(i)

    gpu_nodes = sorted(
        name
        for name, params in atf.get_nodes(live=False, quiet=True, fatal=True).items()
        if name != "DEFAULT" and f"gpu:{SIX_GPUS}" in str(params.get("Gres", ""))
    )
    assert len(gpu_nodes) >= 4, f"Expected four GPU nodes, got {gpu_nodes}"
    node.update(zip(["offset", "order", "untyped", "six"], gpu_nodes))

    layouts = {
        "offset": (
            f"gpu:plain:{PLAIN_GPUS},gpu:chunked:{OFFSET_GPUS}",
            [
                "Name=gpu Type=plain File=/dev/tty0 Links=-1,1,0,0,0,0,0",
                "Name=gpu Type=plain File=/dev/tty1 Links=1,-1,0,0,0,0,0",
                "Name=gpu Type=plain File=/dev/tty2 Links=0,0,-1,0,0,0,0",
                "Name=gpu Type=chunked File=/dev/tty3 ChunkSizes=2 "
                "Links=0,0,0,-1,0,0,0",
                "Name=gpu Type=chunked File=/dev/tty4 ChunkSizes=2 "
                "Links=0,0,0,0,-1,1,0",
                "Name=gpu Type=chunked File=/dev/tty5 ChunkSizes=2 "
                "Links=0,0,0,0,1,-1,0",
                "Name=gpu Type=chunked File=/dev/tty6 ChunkSizes=2 "
                "Links=0,0,0,0,0,0,-1",
            ],
        ),
        "order": (
            f"gpu:plain:{PLAIN_GPUS},gpu:chunked:{OFFSET_GPUS}",
            [
                f"Name=gpu Type=plain File=/dev/tty[0-2] ChunkSizes={PLAIN_SIZES}",
                f"Name=gpu Type=chunked File=/dev/tty[3-6] ChunkSizes=2 "
                f"ChunkOrder={CHUNK_ORDER}",
            ],
        ),
        "untyped": (
            f"gpu:{UNTYPED_GPUS}",
            [
                "Name=gpu File=/dev/tty[0-1] ChunkSizes=2",
                "Name=gpu File=/dev/tty[2-3] ChunkSizes=2",
            ],
        ),
        "six": (
            f"gpu:chunked:{SIX_GPUS}",
            [f"Name=gpu Type=chunked File=/dev/tty[0-5] ChunkSizes={SIX_SIZES}"],
        ),
    }
    lines = []
    atf.set_node_parameter(node["six"], "Sockets", 2)
    atf.set_node_parameter(node["six"], "CoresPerSocket", 4)
    atf.set_node_parameter(node["six"], "ThreadsPerCore", 1)
    for role, (gres, records) in layouts.items():
        atf.set_node_parameter(node[role], "Gres", gres)
        lines += [f"{node[role]} {record}" for record in records]
    atf.require_config_parameter("NodeName", "\nNodeName=".join(lines), source="gres")
    atf.require_slurm_running()


def _parse_gres_chunks(value):
    """Return {gres: (sizes, order)} from a GresChunks value."""

    return {
        gres: (sizes, order)
        for gres, sizes, order in re.findall(
            r"([\w:]+):sizes=([^;,\s]+);order=([^;,\s]+)", value or ""
        )
    }


def _gres_chunks(name):
    """Return {gres: (sizes, order)} from scontrol -d, one entry per Type."""

    output = atf.run_command_output(
        f"scontrol show node -d {name}", fatal=True, quiet=True
    )
    return _parse_gres_chunks((re.search(r"GresChunks=(\S+)", output) or [None, ""])[1])


def _json_gres_chunks(name):
    """Return {gres: (sizes, order)} from the gres_chunks field of --json."""

    output = atf.run_command_output(
        f"scontrol --json show node -d {name}", fatal=True, quiet=True
    )
    return _parse_gres_chunks(json.loads(output)["nodes"][0].get("gres_chunks"))


def _run(job_args, state="RUNNING"):
    """Submit a job, wait for state, and return its ID."""

    job_id = atf.submit_job_sbatch(f"{job_args} --wrap 'sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id, state
    ), f"Job {job_id} ({job_args}) should reach {state}"
    return job_id


def _assert_rejected(job_args, command="sbatch"):
    if command == "sbatch":
        job_args += " --wrap 'true'"
    else:
        job_args += " true"
    error = atf.run_command_error(f"{command} {job_args}", xfail=True, fatal=True)
    assert (
        "Requested node configuration is not available" in error
    ), f"{job_args} should be rejected at submit. Got: {error}"


def _log_count(log_file, text):
    output = atf.run_command_output(
        f"grep -c '{text}' {log_file}", user="root", quiet=True
    )
    return int(output.strip() or 0)


def _assert_two_aligned_chunks(job_args, pairs):
    """Two concurrent jobs must land on distinct aligned chunks.

    Holding two at once makes the second one take the other chunk. This
    checks the chunk boundaries the jobs see, and
    test_misaligned_free_devices_are_not_a_chunk checks that free devices
    across a boundary are not taken as a chunk.
    """

    allocated = [atf.get_job_gres_indexes(_run(job_args)) for _ in range(2)]
    for indexes in allocated:
        assert indexes in pairs, f"Expected one of {pairs}, got {indexes}"
    assert allocated[0] != allocated[1], f"Two jobs share chunk {allocated[0]}"


@pytest.mark.parametrize(
    "role, chunks",
    [
        ("offset", {"gpu:chunked": ("2", "0/1/2/3")}),
        (
            "order",
            {
                "gpu:plain": (PLAIN_SIZES.replace(",", "/"), "0/1/2"),
                "gpu:chunked": ("2", CHUNK_ORDER.replace(",", "/")),
            },
        ),
        ("untyped", {"gpu": ("2", "0/1/2/3")}),
        ("six", {"gpu:chunked": (SIX_SIZES.replace(",", "/"), "0/1/2/3/4/5")}),
    ],
)
def test_chunk_config_is_reported(role, chunks):
    """Each layout registers without draining and reports its chunk state.

    The order is numbered within each Type, as ChunkOrder is written. offset:
    only the chunked Type has a domain, with Links on the record not rejected.
    order: each chunked Type has its own. untyped: records with no Type and no
    Cores still build one, and both lines join it. The JSON output carries the
    same value.
    """

    name = node[role]
    assert atf.wait_for_node_state(name, "IDLE", operator=atf.SetMatch.EQUAL), (
        f"{name} should register IDLE (state="
        f"{atf.get_node_parameter(name, 'state')}, "
        f"reason={atf.get_node_parameter(name, 'reason')})"
    )
    assert (
        _gres_chunks(name) == chunks
    ), f"Expected {name} to report GresChunks {chunks}"
    assert (
        _json_gres_chunks(name) == chunks
    ), f"Expected {name} to report gres_chunks {chunks} in its JSON output"


def test_chunk_config_is_only_shown_with_details():
    output = atf.run_command_output(
        f"scontrol show node {node['offset']}", fatal=True, quiet=True
    )
    assert "GresChunks" not in output, "GresChunks is documented as -d only"


@pytest.mark.parametrize(
    "job_args, pairs",
    [
        ("--gres=gpu:chunked:2", OFFSET_PAIRS),
        ("--gpus-per-node=chunked:2", OFFSET_PAIRS),
        ("--gpus-per-task=chunked:2 -n1", OFFSET_PAIRS),
    ],
    ids=["gres", "gpus_per_node", "gpus_per_task"],
)
def test_offset_chunks_are_aligned(job_args, pairs):
    """Chunks are aligned within the Type, not from device 0.

    Aligning from device 0, or honouring Links, which pairs devices across the
    boundary, would give {4, 5}.
    """

    _assert_two_aligned_chunks(f"-N1 -w {node['offset']} {job_args}", pairs)


def test_enforce_links_is_not_applied_to_chunks():
    """--gres-flags=enforce-links does not apply to chunked devices.

    Links on the chunked records only pairs devices 4 and 5, which is no
    aligned chunk, so enforcing them would leave no pair to run on.
    """

    _assert_two_aligned_chunks(
        f"-N1 -w {node['offset']} --gres=gpu:chunked:2 --gres-flags=enforce-links",
        OFFSET_PAIRS,
    )


def test_links_of_other_types_still_apply():
    """Links is only dropped from chunked records, not from the whole node.

    enforce-links rejects devices with no links, so the plain pair only runs
    if its Links survived.
    """

    job_id = _run(
        f"-N1 -w {node['offset']} --gres=gpu:plain:2 --gres-flags=enforce-links"
    )
    assert atf.get_job_gres_indexes(job_id) == {
        0,
        1,
    }, "The linked plain pair should be allocated"


def test_slurmd_warns_about_dropped_links():
    log_file = atf.get_config_parameter("SlurmdLogFile").replace("%n", node["offset"])
    assert (
        _log_count(
            log_file,
            "Ignoring Links for Name=gpu Type=chunked, ChunkSizes takes precedence",
        )
        >= OFFSET_GPUS
    ), f"Expected a dropped Links warning per chunked line in {log_file}"


def test_order_chunks_follow_chunk_order():
    """Chunks are aligned ranges of chunk positions, not device indexes.

    Applying ChunkOrder as raw device indexes, or ignoring it, gives a
    different pair.
    """

    _assert_two_aligned_chunks(
        f"-N1 -w {node['order']} --gres=gpu:chunked:2", ORDER_PAIRS
    )


def test_untyped_chunks_are_aligned():
    _assert_two_aligned_chunks(
        f"-N1 -w {node['untyped']} --gres=gpu:2", [{0, 1}, {2, 3}]
    )


def test_each_type_uses_its_own_sizes():
    """Two chunked Types on one node each follow their own ChunkSizes.

    1 is a size of plain only, so it runs there and is rejected for chunked.
    """

    job_id = _run(f"-N1 -w {node['order']} --gres=gpu:plain:1")
    assert atf.get_job_gres_indexes(job_id) in (
        {0},
        {1},
        {2},
    ), "A plain request should get one of the plain devices"
    _assert_rejected(f"-N1 -w {node['order']} --gres=gpu:chunked:1")


def test_untyped_request_on_a_single_type_node_follows_its_chunks():
    """An untyped request on a node of one chunked Type follows its rules."""

    job_id = _run(f"-N1 -w {node['six']} --gres=gpu:2")
    assert atf.get_job_gres_indexes(job_id) in (
        {0, 1},
        {2, 3},
        {4, 5},
    ), "An untyped request should get an aligned pair"


@pytest.mark.parametrize(
    "job_args",
    [
        "--gpus=chunked:2 --gpus-per-node=chunked:2",
        "--gpus=chunked:2 --gpus-per-task=chunked:2 -n1",
    ],
    ids=["gpus_per_node", "gpus_per_task"],
)
def test_gpus_with_a_fixed_per_node_count_runs(job_args):
    """--gpus is accepted once another option fixes the count per node."""

    job_id = _run(f"-N1 -w {node['six']} {job_args}")
    assert atf.get_job_gres_indexes(job_id) in (
        {0, 1},
        {2, 3},
        {4, 5},
    ), "The job should get an aligned pair"


def test_chunk_order_does_not_permute_the_job_environment():
    """ChunkOrder only permutes how chunks are formed, not what jobs see."""

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node['order']} --gres=gpu:chunked:2 "
        "--wrap 'echo $CUDA_VISIBLE_DEVICES > visible.out; sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), f"Expected job {job_id} to reach RUNNING"
    expected = ",".join(str(i) for i in sorted(atf.get_job_gres_indexes(job_id)))
    atf.assert_file_contents(
        "visible.out",
        expected,
        message=f"CUDA_VISIBLE_DEVICES should be {expected}",
    )


@pytest.mark.parametrize(
    "job_args",
    [
        "--gres=gpu:chunked:2",
        f"--gres=gpu:chunked:{OFFSET_GPUS}",
        "--gres=gpu:plain:1",
    ],
    ids=["chunk_size", "whole_domain", "non_chunked_type"],
)
def test_allowed_requests_run(job_args):
    """A chunk size, the whole domain, or another Type all run.

    The whole domain is allowed even though it is not a configured size, and
    the plain Type is outside the domain so 1 is fine there.
    """

    _run(f"-N1 -w {node['offset']} {job_args}")


@pytest.mark.parametrize(
    "job_args",
    [
        f"-N1 -w {{offset}} --gres=gpu:chunked:{BAD_COUNT}",
        "-N1 -w {offset} --gres=gpu:2",
        "-N1 -w {order} --gres=gpu:2",
        "-N1 -w {untyped} --gres=gpu:1",
        "-N1 -w {six} --gpus=2",
        "-N1 -w {six} --gpus-per-socket=chunked:2 --sockets-per-node=1",
        f"-N1 -w {{offset}} --exclusive=user --gres=gpu:chunked:{BAD_COUNT}",
        f"-N1 -w {{six}} --core-spec=1 --gres=gpu:chunked:{BAD_COUNT}",
        f"-N1 -w {{six}} --thread-spec=1 --gres=gpu:chunked:{BAD_COUNT}",
    ],
    ids=[
        "non_chunk_size",
        "untyped_on_mixed_node",
        "untyped_on_all_chunked_mixed_node",
        "untyped_non_chunk_size",
        "gpus",
        "gpus_per_socket",
        "exclusive_user",
        "core_spec_not_allowed",
        "thread_spec_not_allowed",
    ],
)
def test_bad_requests_are_rejected(job_args):
    """Requests that cannot be an aligned chunk are rejected at submit.

    An untyped request on a node that mixes Types could span them, although 2
    is a chunk size, whether the other Type is plain or chunked too. --gpus
    leaves the per-node split open and --gpus-per-socket asks for a socket
    shape, both on a node of one chunked Type with a valid size, so no other
    rule applies.
    --exclusive=user still shares the node, so it is not exempt, and nor are
    --core-spec and --thread-spec without AllowSpecResourcesUsage.
    """

    _assert_rejected(job_args.format(**node))


@pytest.mark.parametrize("count", [2, BAD_COUNT], ids=["chunk_size", "non_chunk_size"])
def test_exclusive_receives_every_device(count):
    """A whole-node job is exempt and receives every device on the node."""

    job_id = _run(f"--exclusive -N1 -w {node['offset']} --gres=gpu:chunked:{count}")
    assert atf.get_job_gres_indexes(job_id) == set(
        range(PLAIN_GPUS + OFFSET_GPUS)
    ), "A whole-node job should receive every device on the node"


def test_exclusive_allows_gpus_per_socket():
    """--gpus-per-socket is allowed in a whole-node allocation."""

    job_id = _run(
        f"--exclusive -N1 -w {node['six']} --gpus-per-socket=chunked:2 "
        "--sockets-per-node=1"
    )
    assert atf.get_job_gres_indexes(job_id) == set(
        range(SIX_GPUS)
    ), "A whole-node job should receive every device on the node"


@pytest.fixture
def exclusive_partition():
    name = "chunk_exclusive"
    atf.run_command(
        f"scontrol create partitionname={name} nodes={node['six']} "
        "oversubscribe=exclusive",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )
    yield name
    atf.cancel_all_jobs(quiet=True)
    atf.run_command(
        f"scontrol delete partitionname={name}",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def test_exclusive_partition_receives_every_device(exclusive_partition):
    """A partition with OverSubscribe=EXCLUSIVE is a whole-node allocation."""

    job_id = _run(f"-p {exclusive_partition} -N1 --gres=gpu:chunked:{BAD_COUNT}")
    assert atf.get_job_gres_indexes(job_id) == set(
        range(SIX_GPUS)
    ), "A whole-node job should receive every device on the node"


@pytest.mark.parametrize("spec", ["--core-spec=1", "--thread-spec=1"])
def test_spec_options_leave_the_node_shared(spec):
    """Without AllowSpecResourcesUsage these do not make the job whole-node.

    The job still follows the chunk rules and gets only its chunk.
    """

    job_id = _run(f"-N1 -w {node['six']} {spec} --gres=gpu:chunked:2")
    assert atf.get_job_gres_indexes(job_id) in (
        {0, 1},
        {2, 3},
        {4, 5},
    ), "The job should get an aligned pair, not the whole node"


def test_steps_are_not_chunk_allocated():
    """A step inside an allocation may take any subset of the job's devices."""

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node['offset']} --gres=gpu:chunked:2 "
        "--wrap 'srun --overlap --gres=gpu:chunked:1 true && "
        "srun --overlap --gres=gpu:chunked:2 true'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), "A step of 1 device should run inside a 2 device allocation"


def test_concurrent_single_device_steps_use_every_device():
    """One-device steps may fill a chunk side by side.

    The steps start together and hold their device for a while, so a step
    that could not get a device of its own would have to wait for another
    step's, and both would report the same one.
    """

    atf.make_bash_script(
        "steps.sh",
        f"""for i in $(seq {OFFSET_GPUS}); do
    srun --exact -n1 --gres=gpu:chunked:1 \\
        bash -c 'echo $CUDA_VISIBLE_DEVICES; sleep 5' > step.$i.out &
done
wait
touch steps.done
sleep infinity
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-N1 -n{OFFSET_GPUS} -w {node['offset']} "
        f"--gres=gpu:chunked:{OFFSET_GPUS} steps.sh",
        fatal=True,
    )
    assert atf.wait_for_file(
        "steps.done"
    ), f"Expected the steps of job {job_id} to finish"

    devices = [
        atf.run_command_output(f"cat step.{i}.out", fatal=True, quiet=True).strip()
        for i in range(1, OFFSET_GPUS + 1)
    ]
    assert all(
        d.isdigit() for d in devices
    ), f"Expected each step to get exactly one device, got {devices}"
    assert {int(d) for d in devices} == atf.get_job_gres_indexes(
        job_id, fatal=True
    ), f"Expected the steps to use every device of the job once, got {devices}"


def _run_one_six_device_job():
    job_id = _run(f"-N1 -w {node['six']} --gres=gpu:chunked:1")
    indexes = atf.get_job_gres_indexes(job_id)
    assert len(indexes) == 1, f"Expected exactly one device, got {indexes}"
    return job_id, next(iter(indexes))


def _fill_six():
    """Hold every device of the six node, one job each, keyed by device."""

    holders = {}
    for _ in range(SIX_GPUS):
        job_id, device = _run_one_six_device_job()
        holders[device] = job_id
    assert set(holders) == set(range(SIX_GPUS)), holders
    return holders


@pytest.fixture
def upper_pair_holder():
    """Fill the six node, then free everything but device 4."""

    holders = _fill_six()
    atf.cancel_jobs([holders[i] for i in (0, 1, 2, 3, 5)], fatal=True)


def test_misaligned_free_devices_are_not_a_chunk():
    """Two free neighbours that straddle a chunk boundary are not a chunk.

    With devices 1 and 2 free, first fit would run a two device job on them.
    It has to wait until a whole aligned pair is free, and then take it.
    """

    holders = _fill_six()
    atf.cancel_jobs([holders[1], holders[2]], fatal=True)

    job_id = atf.submit_job_sbatch(
        f"-N1 -w {node['six']} --gres=gpu:chunked:2 --wrap 'sleep infinity'",
        fatal=True,
    )
    assert atf.wait_for_job_state(
        job_id, "PENDING", desired_reason="Resources"
    ), "Devices 1 and 2 are free but not an aligned pair, so the job must wait"

    atf.cancel_jobs([holders[0]], fatal=True)
    assert atf.wait_for_job_state(
        job_id, "RUNNING"
    ), "Freeing device 0 completes the pair of 0 and 1"
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes == {0, 1}, f"Expected the aligned pair {{0, 1}}, got {indexes}"


def test_best_fit_completes_a_partly_used_chunk(upper_pair_holder):
    """Best fit fills device 5 and keeps the free lower pair for bigger jobs.

    First fit would take device 0 and split a pair a two device job needs.
    """

    _, device = _run_one_six_device_job()
    assert device == 5, f"Best fit should complete the pair of 4, got {device}"

    job_id = _run(f"-N1 -w {node['six']} --gres=gpu:chunked:2")
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes in (
        {0, 1},
        {2, 3},
    ), f"A two device job should get a whole free pair, got {indexes}"


def test_single_node_request_needs_no_node_count():
    """One task already pins the job to one node, so no -N1 is needed."""

    _run("--gres=gpu:chunked:2", state="RUNNING")


@pytest.mark.parametrize(
    "job_args, devices",
    [
        (
            f"--gres=gpu:chunked:{OFFSET_GPUS}",
            set(range(PLAIN_GPUS, PLAIN_GPUS + OFFSET_GPUS)),
        ),
        (
            "-n4 --ntasks-per-node=2 --gpus-per-task=chunked:2",
            set(range(PLAIN_GPUS, PLAIN_GPUS + OFFSET_GPUS)),
        ),
        ("--exclusive --gres=gpu:chunked:2", set(range(PLAIN_GPUS + OFFSET_GPUS))),
    ],
    ids=["per_node", "per_task", "exclusive"],
)
def test_multi_node_takes_every_device(job_args, devices):
    """A job spanning nodes must hold the whole domain on each of them.

    The offset and order nodes share the chunked Type's layout. An exclusive
    job is exempt and gets every device on each node.
    """

    job_id = _run(f"-N2 -w {node['offset']},{node['order']} {job_args}")
    node_cnt = int(atf.get_job_parameter(job_id, "NumNodes"))
    assert node_cnt == 2, f"Expected the job to span 2 nodes, got {node_cnt}"
    # One block per group of nodes with an identical allocation, so checking
    # every block with the node count rules out half a domain on each node
    for indexes in atf.get_job_gres_index_blocks(job_id):
        assert indexes == devices, f"Each node must give {devices}, got {indexes}"


@pytest.mark.parametrize(
    "job_args",
    [
        "-N2 --gres=gpu:chunked:2",
        "--nodes=1-2 --gres=gpu:chunked:2",
        "--gpus-per-task=2 -n2",
        "--tres-per-task=gres/gpu=2 -n2",
        "-n2 --gres=gpu:chunked:2",
        "-n2 --gpus-per-node=chunked:2",
        "-n2 --ntasks-per-node=1 --gpus-per-task=chunked:2",
        "-N2 -w {offset},{order} -n4 --gpus-per-task=chunked:2",
        "-w {offset},{order} -n3 --ntasks-per-node=2 --gpus-per-task=chunked:2",
    ],
    ids=[
        "fixed_two_nodes",
        "node_range",
        "per_task",
        "tres_per_task",
        "tasks_per_node_gres",
        "tasks_gpus_per_node",
        "one_task_per_node",
        "node_count_without_tasks_per_node",
        "uneven_tasks_per_node",
    ],
)
def test_unpinned_partial_requests_are_rejected(job_args):
    """A partial request that could span nodes is rejected at submit.

    Node filtering only knows the node bounds, so leaving it through would
    fail at allocation and retry forever. Several tasks without -N1 could be
    spread over nodes, and one task per node fixes a per-node count that
    spans nodes, which is still partial. A node count alone does not fix the
    tasks on each node, and three tasks two per node leave one node with less
    than its whole domain.
    """

    _assert_rejected(job_args.format(**node))


WHOLE_UNTYPED = [set(range(UNTYPED_GPUS))]
UNTYPED_PAIRS = [{0, 1}, {2, 3}]


@pytest.mark.parametrize(
    "job_args, expected",
    [
        ("--gpus-per-task=2 -n2 --ntasks-per-node=2", WHOLE_UNTYPED),
        ("--gpus-per-task=2 -n2 -N1", WHOLE_UNTYPED),
        ("--tres-per-task=gres/gpu=2 -n2 --ntasks-per-node=2", WHOLE_UNTYPED),
        ("--tres-per-task=gres/gpu=2 -n2 -N1", WHOLE_UNTYPED),
        ("--gpus-per-task=1 -n2 -N1", UNTYPED_PAIRS),
        ("--tres-per-task=gres/gpu=1 -n2 -N1", UNTYPED_PAIRS),
    ],
    ids=[
        "ntasks_per_node",
        "one_node",
        "tres_ntasks_per_node",
        "tres_one_node",
        "one_node_partial",
        "tres_one_node_partial",
    ],
)
def test_pinned_per_task_requests_run(job_args, expected):
    """Pinning tasks per node makes a per task request definite.

    With -N1 the total may also be less than the whole domain.
    """

    job_id = _run(f"-w {node['untyped']} {job_args}")
    indexes = atf.get_job_gres_indexes(job_id)
    assert indexes in expected, f"Expected one of {expected}, got {indexes}"


@pytest.mark.parametrize("command", ["srun", "salloc"])
def test_srun_and_salloc_follow_the_chunk_rules(command):
    """The rules hold when srun or salloc creates the allocation."""

    _assert_rejected(f"-N1 -w {node['offset']} --gres=gpu:chunked:{BAD_COUNT}", command)
    step = "printenv CUDA_VISIBLE_DEVICES"
    if command == "salloc":
        step = f"srun {step}"
    output = atf.run_command_output(
        f"{command} -N1 -w {node['offset']} --gres=gpu:chunked:2 {step}",
        fatal=True,
    )
    devices = {int(d) for d in output.strip().split(",")}
    assert devices in OFFSET_PAIRS, f"Expected one of {OFFSET_PAIRS}, got {devices}"


def test_rejected_node_is_logged():
    """DebugFlags=SelectType names the chunk rule that turned a node down."""

    log_file = atf.get_config_parameter("SlurmctldLogFile")
    text = "which is not one of the configured ChunkSizes"
    before = _log_count(log_file, text)
    _assert_rejected(f"-N1 -w {node['six']} --gres=gpu:chunked:{BAD_COUNT}")
    assert (
        _log_count(log_file, text) > before
    ), f"Expected a new chunk rule line in {log_file}"
