############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test the --mem-per-gpu option.

Ticket 11834: --mem-per-gpu must size the job and step allocations from the
allocated GPU count alone, and the resulting limit must be enforced by cgroups.
Bug 9229 was a step that requested its own --mem-per-gpu being given the job's
cgroup limit and accounted with mem=0, so steps are covered both inheriting the
job's memory and requesting their own.
"""

import re

import pytest

import atf

# Ticket 22370: before 25.11, a TLS slurmstepd sent its Slurm API calls through
# slurmd, which crashed relaying the step info for the OOM-killed step.
tls_slurmd_crash = atf.get_config_parameter(
    "TLSType", default="tls/none", live=False, quiet=True
).casefold() != "tls/none" and atf.get_version("sbin/slurmd") < (25, 11)
ticket_22370 = "Ticket 22370: slurmd crashed relaying the step info for the OOM-killed step of a TLS slurmstepd, fixed in 25.11"

pytestmark = pytest.mark.slow

# The node configuration setup() requires
GPU_CNT = 2
NODE_MEMORY = 2000

# Cluster and partition defaults exercised when --mem-per-gpu is omitted
DEF_MEM_PER_GPU = 17
PART_DEF_MEM_PER_GPU = 23

# A non-GPU GRES, used to confirm it does not perturb the memory allocation
OTHER_GRES = "custom_gres"
OTHER_GRES_COUNT = 4

# One device of each GPU type, so each node still holds GPU_CNT GPUs in total
GPU_TYPES = ("typea", "typeb")

# Memory enforcement bracket. The cgroup limit for a one-GPU job is
# MEM_PER_GPU_ENFORCED, so an allocation below it must complete and one above
# it must be OOM-killed. sbatch(1) documents that each slurmstepd is charged
# against this limit and typically takes 20MiB to 200MiB, and a job running a
# step holds more than one, so the limit has to clear MEM_UNDER_LIMIT plus
# that overhead with margin.
MEM_PER_GPU_ENFORCED = 500
MEM_UNDER_LIMIT = 50
MEM_OVER_LIMIT = 1000

# Above the limit of one GPU but below that of two, so it must survive only
# where two GPUs' worth of memory was allocated. It sits just above the lower
# limit so the higher one keeps room for the slurmstepds sharing it.
MEM_BETWEEN_LIMITS = MEM_PER_GPU_ENFORCED + 100

# Seconds the memory-consuming program holds its peak allocation
MEM_HOLD_SECONDS = 1

# Time limit of the enforcement jobs, and the state wait for them. The wait
# must outlast the limit: cgroup.conf(5) documents that a step can keep
# running after an OOM, and a job that then runs to TIMEOUT is the useful
# diagnostic, not an expired poll.
ENFORCED_TIME_LIMIT_MIN = 2
ENFORCED_STATE_TIMEOUT = 180

NON_TERMINAL_STATES = {
    "PENDING",
    "RUNNING",
    "REQUEUED",
    "RESIZING",
    "SUSPENDED",
    "COMPLETING",
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Accounting is needed to check the step and job TRES in sacct
    atf.require_accounting()

    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Set rather than include: SelectTypeParameters is not additive, so
    # appending to a select/linear config's CR_Memory yields an invalid
    # combination.
    atf.require_config_parameter(
        "SelectTypeParameters",
        ["CR_Core_Memory", "CR_CPU_Memory", "CR_Socket_Memory"],
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", OTHER_GRES)
    atf.require_config_parameter_includes("AccountingStorageTRES", "gres/gpu")
    atf.require_config_parameter_includes("AccountingStorageTRES", f"gres/{OTHER_GRES}")
    for gpu_type in GPU_TYPES:
        atf.require_config_parameter_includes(
            "AccountingStorageTRES", f"gres/gpu:{gpu_type}"
        )

    # slurm.conf(5) documents DefMemPerCPU, DefMemPerGPU and DefMemPerNode as
    # mutually exclusive, but they are parsed into separate fields rather than
    # rejected, so a base config setting either of the others would supply the
    # default instead.
    atf.require_config_parameter("DefMemPerCPU", None)
    atf.require_config_parameter("DefMemPerNode", None)
    atf.require_config_parameter("DefMemPerGPU", DEF_MEM_PER_GPU)

    atf.require_config_parameter_includes("TaskPlugin", "cgroup")
    atf.require_config_parameter("ProctrackType", "proctrack/cgroup")
    atf.require_config_parameter("ConstrainRAMSpace", "yes", source="cgroup")
    atf.require_config_parameter("AllowedRAMSpace", 100, source="cgroup")
    # Make OOM deterministic
    atf.require_config_parameter("ConstrainSwapSpace", "yes", source="cgroup")
    atf.require_config_parameter("AllowedSwapSpace", 0, source="cgroup")
    # Both clamp every job and step limit
    atf.require_config_parameter("MinRAMSpace", 30, source="cgroup")
    atf.require_config_parameter("MaxRAMPercent", 100, source="cgroup")

    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name",
        f"gpu Type={GPU_TYPES[0]} File=/dev/tty0"
        f"\nName=gpu Type={GPU_TYPES[1]} File=/dev/tty1"
        f"\nName={OTHER_GRES} Count={OTHER_GRES_COUNT}",
        source="gres",
    )
    atf.require_nodes(
        2,
        [
            ("CPUs", 4),
            ("Sockets", 2),
            (
                "Gres",
                f"gpu:{GPU_TYPES[0]}:1,gpu:{GPU_TYPES[1]}:1,"
                f"{OTHER_GRES}:{OTHER_GRES_COUNT}",
            ),
            ("RealMemory", NODE_MEMORY),
        ],
    )
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def bin_dir():
    """Slurm commands run inside a job are not necessarily in its PATH."""
    return atf.properties["slurm-bin-dir"]


def find(pattern, text):
    """Return the first group of pattern in text, failing with the text."""
    match = re.search(pattern, text)
    assert match is not None, f"{pattern} not found in: {text}"
    return match.group(1)


def parse_mem_mb(tres):
    """Return the mem= value of a TRES string, normalized to megabytes.

    TRES memory is rendered with CONVERT_NUM_UNIT_EXACT, so a value that is
    at least 1024 and a multiple of 512 comes back as G rather than M.
    """
    match = re.search(r"mem=(\d+(?:\.\d+)?)([KMGT])", tres)
    assert match is not None, f"Memory not found in TRES: {tres}"

    scale = {"K": 1 / 1024, "M": 1, "G": 1024, "T": 1024 * 1024}[match.group(2)]
    return int(float(match.group(1)) * scale)


def parse_gpus(tres):
    """Return the untyped gres/gpu= count of a TRES string.

    With typed GPUs the string also carries per-type entries such as
    gres/gpu:typea=1, so anchor on the untyped aggregate.
    """
    match = re.search(r"gres/gpu=(\d+)", tres)
    assert match is not None, f"GPU count not found in TRES: {tres}"
    return int(match.group(1))


def sacct_states(job_id, records):
    """Return a dict of sacct record id -> state, once settled.

    wait_for_job_state() only proves slurmctld saw the job end; slurmdbd is
    updated asynchronously, so poll until every expected record exists and
    none is still in a non-terminal state.
    """
    states = {}
    for _ in atf.timer(fatal=True):
        output = atf.run_command_output(
            f"sacct -j {job_id} -P --noheader -o jobid,state", fatal=True
        )
        states = dict(line.split("|", 1) for line in output.splitlines() if "|" in line)
        if all(r in states for r in records) and not (
            NON_TERMINAL_STATES & set(states.values())
        ):
            break

    return states


def poll_output(command, condition):
    """Return the output of command once condition holds for it.

    Neither a job's output file nor its accounting record is guaranteed to be
    complete when slurmctld reports the job done.
    """
    for _ in atf.timer(fatal=True):
        # Not fatal: the file may not exist yet
        output = atf.run_command_output(command)
        if condition(output):
            return output


def submit_enforced(sbatch_args, oom_kill_step):
    """Submit an enforcement job and wait for it to end.

    cgroup.conf(5) documents that under ConstrainRAMSpace an OOM event kills
    one or more processes but the step itself keeps running, so without
    --oom-kill-step=1 the step can survive to its time limit and be recorded
    as TIMEOUT rather than OUT_OF_MEMORY. The option is set either way so the
    base config's OOMKillStep does not decide it.
    """
    job_id = atf.submit_job_sbatch(
        f"-t{ENFORCED_TIME_LIMIT_MIN} --oom-kill-step={oom_kill_step} {sbatch_args}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", timeout=ENFORCED_STATE_TIMEOUT, fatal=True)
    return job_id


def alloc_case(opts, gpus, mem, case_id, nodes=1, node_mems=None, extra_tres=()):
    """Return a test_job_mem_allocation row.

    node_mems are the distinct per-node memory values, by default the whole
    allocation on one node. extra_tres are entries AllocTRES must also carry.
    """
    return pytest.param(
        opts, gpus, mem, nodes, node_mems or [mem], extra_tres, id=case_id
    )


SPREAD_MEM = NODE_MEMORY // GPU_CNT + 1

ALLOC_CASES = [
    alloc_case("--gpus=1 --mem-per-gpu=12", 1, 12, "gpus"),
    alloc_case("--gpus=1 --mem-per-gpu=1G", 1, 1024, "unit-suffix"),
    alloc_case("--gpus-per-node=1 -N1 --mem-per-gpu=120", 1, 120, "gpus-per-node"),
    alloc_case("--gpus-per-task=1 -n1 --mem-per-gpu=1200", 1, 1200, "gpus-per-task"),
    alloc_case("--gpus-per-task=2 -n1 --mem-per-gpu=13", 2, 26, "gpus-per-task-2"),
    # 3 GPUs need both nodes, one holding 2 and the other 1, so each node's
    # memory must follow its own GPU count.
    alloc_case("--gpus=3 --mem-per-gpu=13", 3, 39, "uneven-nodes", 2, [13, 26]),
    # Two GPUs' worth of memory does not fit on one node, forcing a second
    alloc_case(
        f"--gpus={GPU_CNT} --mem-per-gpu={SPREAD_MEM}",
        GPU_CNT,
        SPREAD_MEM * GPU_CNT,
        "spread-by-memory",
        2,
        [SPREAD_MEM],
    ),
    # --exclusive allocates every GPU on the node, so the memory scales by all
    # of them regardless of how many the job asked for.
    *[
        alloc_case(
            f"{gpu_opt} -N1 --exclusive --mem-per-gpu=14",
            GPU_CNT,
            14 * GPU_CNT,
            f"exclusive-{case_id}",
        )
        for gpu_opt, case_id in (
            ("--gres=gpu:1", "gres"),
            ("--gpus=1", "gpus"),
            ("--gpus-per-task=1 --ntasks-per-node=1", "gpus-per-task"),
            ("--gpus-per-socket=1 --sockets-per-node=1", "gpus-per-socket"),
        )
    ],
    # Memory must sum over the typed requests, not follow just one of them
    alloc_case(
        f"--gres=gpu:{GPU_TYPES[0]}:1,gpu:{GPU_TYPES[1]}:1 -N1 --mem-per-gpu=21",
        2,
        42,
        "both-types",
        extra_tres=[f"gres/gpu:{gpu_type}=1" for gpu_type in GPU_TYPES],
    ),
    # Memory scaling with the other GRES count would give 4x the expected value
    alloc_case(
        f"--gpus=1 --gres={OTHER_GRES}:{OTHER_GRES_COUNT} --mem-per-gpu=19",
        1,
        19,
        "other-gres",
        extra_tres=[f"gres/{OTHER_GRES}={OTHER_GRES_COUNT}"],
    ),
    alloc_case(
        f"--gpus={GPU_CNT} -N1", GPU_CNT, DEF_MEM_PER_GPU * GPU_CNT, "DefMemPerGPU"
    ),
]


@pytest.mark.parametrize("opts, gpus, mem, nodes, node_mems, extra_tres", ALLOC_CASES)
def test_job_mem_allocation(bin_dir, opts, gpus, mem, nodes, node_mems, extra_tres):
    """Verify --mem-per-gpu sizes the job allocation by the allocated GPUs."""
    output = atf.run_job_output(
        f"{opts} -t1 bash -c '{bin_dir}/scontrol show job -d $SLURM_JOB_ID'",
        fatal=True,
    )

    tres = find(r"AllocTRES=(\S+)", output)
    assert parse_gpus(tres) == gpus, f"Expected {gpus} GPUs in AllocTRES: {tres}"
    assert parse_mem_mb(tres) == mem, f"Expected mem={mem}M in AllocTRES: {tres}"
    for entry in extra_tres:
        assert entry in tres, f"Expected {entry} in AllocTRES: {tres}"

    num_nodes = int(find(r"NumNodes=(\d+)", output))
    assert num_nodes == nodes, f"Expected {nodes} nodes, got {num_nodes}"

    # Nodes allocated the same memory share one JOB_RESOURCES line, so only
    # the distinct values are comparable.
    found = sorted(
        {int(m) for m in re.findall(r"Nodes=\S+\s+CPU_IDs=\S+\s+Mem=(\d+)", output)}
    )
    assert found == node_mems, f"Expected per-node Mem={node_mems}, got {found}"


@pytest.fixture
def partition_def_mem_per_gpu():
    """Set DefMemPerGPU in the default partition's JobDefaults."""
    partition = atf.default_partition()
    prior_defaults = atf.get_partition_parameter(partition, "JobDefaults")
    update = f"scontrol update PartitionName={partition} JobDefaults="
    slurm_user = atf.properties["slurm-user"]

    atf.run_command(
        f"{update}DefMemPerGPU={PART_DEF_MEM_PER_GPU}", user=slurm_user, fatal=True
    )
    yield
    atf.run_command(f"{update}{prior_defaults or ''}", user=slurm_user, fatal=True)


@pytest.mark.xfail(
    reason="Issue 51374: with a cluster DefMemPerGPU also set, the partition "
    "value reaches ReqTRES but the allocation uses the cluster one",
)
def test_partition_def_mem_per_gpu(bin_dir, partition_def_mem_per_gpu):
    """Verify a partition's DefMemPerGPU supplies the default ahead of the
    cluster one."""
    output = atf.run_job_output(
        f"--gpus={GPU_CNT} -N1 -t1 bash -c "
        f"'{bin_dir}/scontrol show job -d $SLURM_JOB_ID'",
        fatal=True,
    )

    mem = parse_mem_mb(find(r"AllocTRES=(\S+)", output))
    expected = PART_DEF_MEM_PER_GPU * GPU_CNT
    assert mem == expected, f"Expected mem={expected}M from the partition, got {mem}M"


@pytest.mark.parametrize(
    "step_opts, step_gpus, step_mem",
    [
        pytest.param("", GPU_CNT, 40 * GPU_CNT, id="env"),
        # Fewer GPUs than the job, so only the inherited SLURM_MEM_PER_GPU can
        # size it below the job's memory.
        pytest.param("--gpus=1", 1, 40, id="env-gpu-subset"),
        pytest.param("--gpus=1 --mem-per-gpu=15", 1, 15, id="cli"),
        pytest.param("--mem=25", GPU_CNT, 25, id="cli-mem-over-env"),
    ],
)
def test_step_mem_allocation(bin_dir, step_opts, step_gpus, step_mem):
    """Verify a step's memory in 'scontrol show step' and in sacct.

    sbatch(1) exports SLURM_GPUS and SLURM_MEM_PER_GPU, which srun(1) reads as
    --gpus and --mem-per-gpu, so a step's memory follows its own GPU count at
    the job's per-GPU rate. srun(1) documents that a memory option on the
    command line takes precedence over the environment, even one of the other
    mutually exclusive options.
    """
    job_id = atf.submit_job_sbatch(
        f"-N1 --gpus={GPU_CNT} --mem-per-gpu=40 -t1 -o step-%j.out "
        f'--wrap="{bin_dir}/srun {step_opts} bash -c '
        f"'{bin_dir}/scontrol show step \\$SLURM_JOB_ID.\\$SLURM_STEP_ID'\"",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    output = poll_output(f"cat step-{job_id}.out", lambda out: "TRES=" in out)
    tres = find(r"\bTRES=(\S+)", output)
    assert (
        parse_gpus(tres) == step_gpus
    ), f"Expected {step_gpus} GPUs in step TRES: {tres}"
    assert (
        parse_mem_mb(tres) == step_mem
    ), f"Expected mem={step_mem}M in step TRES: {tres}"

    # -X restricts the job query to the allocation record, which otherwise
    # returns the step records too and lets the wrong mem= match first.
    for record, opts, mem in (
        (str(job_id), "-X", 40 * GPU_CNT),
        (f"{job_id}.0", "", step_mem),
    ):
        tres = poll_output(
            f"sacct -nPj {record} {opts} -o AllocTRES", lambda out: "mem=" in out
        )
        assert parse_mem_mb(tres) == mem, f"Expected mem={mem}M for {record}: {tres}"


# Each row: job options, the command run with {srun} and {hog} substituted,
# then the suffix of the sacct record and the state it must end in.
ENFORCED_CASES = [
    pytest.param(
        "--gpus=1", f"{{hog}} {MEM_UNDER_LIMIT}", "", "COMPLETED", id="batch-under"
    ),
    pytest.param(
        "--gpus=1",
        f"{{hog}} {MEM_OVER_LIMIT}",
        ".batch",
        "OUT_OF_MEMORY",
        id="batch-over",
    ),
    pytest.param(
        "--gpus=1",
        f"{{srun}} {{hog}} {MEM_UNDER_LIMIT}",
        "",
        "COMPLETED",
        id="step-under",
    ),
    pytest.param(
        "--gpus=1",
        f"{{srun}} {{hog}} {MEM_OVER_LIMIT}",
        ".0",
        "OUT_OF_MEMORY",
        id="step-over",
        marks=pytest.mark.skipif(tls_slurmd_crash, reason=ticket_22370),
    ),
    pytest.param(
        f"-N1 --gpus={GPU_CNT}",
        f"{{srun}} --gpus=1 --mem-per-gpu={MEM_PER_GPU_ENFORCED} {{hog}} "
        f"{MEM_UNDER_LIMIT}",
        "",
        "COMPLETED",
        id="step-requested-under",
    ),
    # The job's two-GPU limit holds the hog with the slurmstepd ceiling to
    # spare, but the step's own request is half of one GPU's worth, so only an
    # enforced step limit kills it (bug 9229).
    pytest.param(
        f"-N1 --gpus={GPU_CNT}",
        f"{{srun}} --gpus=1 --mem-per-gpu={MEM_PER_GPU_ENFORCED // 2} {{hog}} "
        f"{MEM_PER_GPU_ENFORCED}",
        ".0",
        "OUT_OF_MEMORY",
        id="step-requested-over",
        marks=pytest.mark.skipif(tls_slurmd_crash, reason=ticket_22370),
    ),
]


@pytest.mark.parametrize("job_opts, command, record, state", ENFORCED_CASES)
def test_mem_enforced(bin_dir, use_memory_program, job_opts, command, record, state):
    """Verify cgroups enforce the --mem-per-gpu limit of batch and normal steps.

    sacct(1) defines OUT_OF_MEMORY as "Job experienced out of memory error", so
    the record that exceeded its limit is the documented promise. Nothing
    documents how that state propagates to the other records -- ticket 11834
    comment 9 records the distribution changing between versions -- so only
    that one record is pinned.
    """
    command = command.format(srun=f"{bin_dir}/srun", hog=use_memory_program)
    job_id = submit_enforced(
        f"{job_opts} --mem-per-gpu={MEM_PER_GPU_ENFORCED} -o /dev/null "
        f"--wrap='{command} {MEM_HOLD_SECONDS}'",
        oom_kill_step=1,
    )

    record = f"{job_id}{record}"
    states = sacct_states(job_id, [record])
    assert (
        states.get(record) == state
    ), f'{record} state="{states.get(record)}", expected "{state}" (sacct: {states})'


def test_mem_enforced_per_node(bin_dir, use_memory_program):
    """Verify the enforced limit follows each node's own GPU count."""
    # The task OOM-killed on one node must not take down the task on the
    # other, so neither the OOM nor its bad exit may kill the whole step.
    job_id = submit_enforced(
        f"--gpus=3 -N2 -n2 --mem-per-gpu={MEM_PER_GPU_ENFORCED} -o per-node-%j.out "
        f'--wrap="{bin_dir}/scontrol show job -d \\$SLURM_JOB_ID; '
        f"{bin_dir}/srun --kill-on-bad-exit=0 bash -c "
        f"'{use_memory_program} {MEM_BETWEEN_LIMITS} "
        f"{MEM_HOLD_SECONDS} && echo SURVIVED_ON \\$SLURMD_NODENAME'; echo BATCH_DONE\"",
        oom_kill_step=0,
    )

    # BATCH_DONE is the last line the script writes, so its presence means the
    # job record and every task's output are already in the file.
    file_out = f"per-node-{job_id}.out"
    atf.assert_file_contents(file_out, "BATCH_DONE", contains=True)
    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    per_node = {
        node: int(mem)
        for node, mem in re.findall(r"Nodes=(\S+)\s+CPU_IDs=\S+\s+Mem=(\d+)", output)
    }
    assert sorted(per_node.values()) == [
        MEM_PER_GPU_ENFORCED,
        MEM_PER_GPU_ENFORCED * 2,
    ], f"Per-node memory {per_node}, expected one node at twice the other"

    roomy_node = max(per_node, key=per_node.get)
    survivors = re.findall(r"^SURVIVED_ON (\S+)$", output, re.MULTILINE)
    assert survivors == [roomy_node], (
        f"Tasks surviving {MEM_BETWEEN_LIMITS}MB: {survivors}, expected only "
        f"{roomy_node} which was allocated {per_node} (job output: {output})"
    )
