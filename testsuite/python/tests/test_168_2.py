############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Heterogeneous job + stepmgr-on-stepd: verify job-global step ids.

With --stepmgr on a heterogeneous job, each component's stepmgr asks the
leader for the next step id (REQUEST_HET_STEP_ID). Step ids must remain
unique and contiguous across the whole het job, not per-component.

Without that RPC, sequential srun --het-group calls would reuse ids
0,1,0,1,... per component instead of 0,1,2,3 across the job.
"""

import collections
import re

import pytest

import atf

pytestmark = pytest.mark.slow

# Echoed after the last srun so we can tell the batch script's whole output
# has been flushed before parsing it.
OUTPUT_MARKER = "HETJOB_STEPS_DONE"


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The client tools upgrade last, so gating on srun >= 26.11 already
    # implies slurmd and slurmctld are >= 26.11.
    atf.require_version(
        (26, 11),
        component="bin/srun",
        reason="Issue 50976: hetjob + stepmgr requires srun 26.11+",
    )
    atf.require_nodes(3)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # Het jobs are only started by backfill; don't wait for its full cycle
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))
    atf.require_config_parameter("SchedulerType", "sched/backfill")
    atf.require_slurm_running()


def _het_opts(component_nodes):
    """Build het job options with --stepmgr on the leader only."""

    return " : ".join(
        f"-N{nodes} --ntasks-per-node=1 -t2" + (" --stepmgr" if offset == 0 else "")
        for offset, nodes in enumerate(component_nodes)
    )


@pytest.mark.parametrize(
    "command, component_nodes",
    [
        ("sbatch", [1, 1]),
        ("salloc", [1, 1]),
        ("sbatch", [1, 1, 1]),
        ("sbatch", [2, 1]),
    ],
    ids=["sbatch-2comp", "salloc-2comp", "sbatch-3comp", "sbatch-multinode"],
)
def test_hetjob_stepmgr_job_global_step_ids(command, component_nodes):
    """Sequential sruns across all components yield unique, contiguous step ids."""

    test_id = f"{command}_{'-'.join(str(n) for n in component_nodes)}"
    file_in = f"hetjob_stepmgr_{test_id}.in"
    file_out = f"hetjob_stepmgr_{test_id}.out"

    # Two rounds over every component, so a per-component counter would
    # repeat ids instead of continuing the job-global sequence.
    sruns = "\n".join(
        f"srun --het-group={offset} --ntasks=1 "
        f"""bash -c 'echo "STEP=$SLURM_STEP_ID"' >> {file_out}"""
        for _ in range(2)
        for offset in range(len(component_nodes))
    )
    # sbatch also exports a bare SLURM_STEPMGR but salloc does not, so use the
    # per-component form, which both submission paths set.
    stepmgr_echos = "\n".join(
        f'echo "STEPMGR_{offset}=$SLURM_STEPMGR_HET_GROUP_{offset}" >> {file_out}'
        for offset in range(len(component_nodes))
    )
    atf.make_bash_script(
        file_in,
        f"{stepmgr_echos}\n{sruns}\necho {OUTPUT_MARKER} >> {file_out}\n",
    )

    het_opts = _het_opts(component_nodes)
    job_param = het_opts if command == "salloc" else f"-o /dev/null {het_opts}"
    atf.submit_job(command, job_param, f"./{file_in}", wrap_job=False, fatal=True)

    atf.assert_file_contents(file_out, OUTPUT_MARKER, contains=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)
    step_ids = [int(m) for m in re.findall(r"^STEP=(\d+)$", output, re.MULTILINE)]

    # A plain het job already numbers its steps job-globally, so the ids below
    # only say something about stepmgr once every component is known to have
    # one.
    stepmgr_offsets = sorted(
        int(offset)
        for offset in re.findall(r"^STEPMGR_(\d+)=\S+$", output, re.MULTILINE)
    )
    assert stepmgr_offsets == list(range(len(component_nodes))), (
        f"Every component must report a stepmgr host for the step ids to "
        f"exercise stepmgr rather than the controller, got offsets "
        f"{stepmgr_offsets}.\nRaw output:\n{output}"
    )

    expected_ids = list(range(2 * len(component_nodes)))
    assert step_ids == expected_ids, (
        f"Expected job-global step ids {expected_ids}, got {step_ids}.\n"
        f"Per-component duplicates indicate stepmgr is not allocating job-globally.\n"
        f"Raw output:\n{output}"
    )


@pytest.mark.parametrize("follower_stepmgr", [False, True])
@pytest.mark.parametrize("leader_stepmgr", [False, True])
@pytest.mark.parametrize("command", ["sbatch", "salloc"])
def test_hetjob_stepmgr_follower_tracks_leader(
    command, leader_stepmgr, follower_stepmgr
):
    """The follower's StepMgrEnabled always matches the leader's.

    Only the leader's --stepmgr decides for the whole job, so passing it on
    a non-leader component is inert. salloc and sbatch document the override
    identically, so both submission paths are covered.
    """

    # --stepmgr only enables stepmgr per-job "if it isn't enabled system wide",
    # and there is no per-job way to opt back out, so under
    # SlurmctldParameters=enable_stepmgr every component is enabled whatever
    # was submitted and leader-wins cannot be observed.
    if atf.config_parameter_includes("SlurmctldParameters", "enable_stepmgr"):
        pytest.skip(
            "Issue 50976: leader-wins is unobservable when stepmgr is enabled"
            " globally"
        )

    leader_opts = "-N1 --ntasks=1 -t2" + (" --stepmgr" if leader_stepmgr else "")
    follower_opts = "-N1 --ntasks=1 -t2" + (" --stepmgr" if follower_stepmgr else "")

    if command == "sbatch":
        file_in = f"hetjob_stepmgr_{int(leader_stepmgr)}{int(follower_stepmgr)}.in"
        atf.make_bash_script(
            file_in,
            f"""
#SBATCH {leader_opts}
#SBATCH hetjob
#SBATCH {follower_opts}

true
""",
        )
        leader_job_id = atf.submit_job_sbatch(f"-o /dev/null {file_in}", fatal=True)
    else:
        # --no-shell leaves the allocation in place so StepMgrEnabled is still
        # readable for every component.
        leader_job_id = atf.submit_job_salloc(
            f"--no-shell {leader_opts} : {follower_opts}", fatal=True
        )

    jobs = atf.get_jobs(leader_job_id, fatal=True)
    components = atf.range_to_list(jobs[leader_job_id]["HetJobIdSet"])
    follower_id = next(c for c in components if jobs[c]["HetJobOffset"] == 1)

    # scontrol prints StepMgrEnabled only when the bit is set, so a disabled
    # component has no such key at all.
    leader_flag = jobs[leader_job_id].get("StepMgrEnabled")
    follower_flag = jobs[follower_id].get("StepMgrEnabled")

    assert follower_flag == leader_flag, (
        f"JobId {follower_id}: follower StepMgrEnabled must match the leader "
        f"(leader-wins), got leader={leader_flag!r}, follower={follower_flag!r}"
    )

    expected_flag = "Yes" if leader_stepmgr else None
    assert leader_flag == expected_flag, (
        f"JobId {leader_job_id}: leader submitted with --stepmgr="
        f"{leader_stepmgr} should have StepMgrEnabled={expected_flag!r}, "
        f"got {leader_flag!r}"
    )


def test_hetjob_stepmgr_multi_component_step_shares_id():
    """A step spanning both components shares one id; skipped ids leave gaps.

    Mirrors the worked example in the heterogeneous_jobs documentation: a
    step on both components, then one on the leader only, then both again.
    The follower must jump straight from id 0 to id 2.
    """

    file_in = "hetjob_stepmgr_multi.in"
    file_out = "hetjob_stepmgr_multi.out"
    atf.make_bash_script(
        file_in,
        f"""
#SBATCH -N1 --ntasks=1 -t2 --stepmgr
#SBATCH hetjob
#SBATCH -N1 --ntasks=1 -t2

echo "STEPMGR_0=$SLURM_STEPMGR_HET_GROUP_0"
echo "STEPMGR_1=$SLURM_STEPMGR_HET_GROUP_1"
srun --het-group=0,1 bash -c 'echo "STEP=$SLURM_STEP_ID"'
srun --het-group=0 bash -c 'echo "STEP=$SLURM_STEP_ID"'
srun --het-group=0,1 bash -c 'echo "STEP=$SLURM_STEP_ID"'
echo {OUTPUT_MARKER}
""",
    )

    leader_job_id = atf.submit_job_sbatch(f"-o {file_out} {file_in}", fatal=True)
    atf.wait_for_job_state(leader_job_id, "DONE", fatal=True)
    atf.assert_file_contents(file_out, OUTPUT_MARKER, contains=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)
    step_ids = [int(m) for m in re.findall(r"^STEP=(\d+)$", output, re.MULTILINE)]

    # A plain het job already numbers its steps job-globally, so the ids below
    # only say something about stepmgr once every component is known to have
    # one.
    stepmgr_offsets = sorted(
        int(offset)
        for offset in re.findall(r"^STEPMGR_(\d+)=\S+$", output, re.MULTILINE)
    )
    assert stepmgr_offsets == [0, 1], (
        f"Every component must report a stepmgr host for the step ids to "
        f"exercise stepmgr rather than the controller, got offsets "
        f"{stepmgr_offsets}.\nRaw output:\n{output}"
    )

    # One task reports per participating component, so the number of lines
    # carrying a step id is how many components ran that step. The middle
    # step is leader-only, which is what leaves the follower a gap at id 1.
    assert collections.Counter(step_ids) == {0: 2, 1: 1, 2: 2}, (
        f"Expected the both-component steps to share ids 0 and 2 (2 lines "
        f"each) and the leader-only step to take id 1 (1 line), leaving the "
        f"follower a gap at 1; got {sorted(step_ids)}.\nRaw output:\n{output}"
    )


def test_hetjob_stepmgr_env_vars():
    """Each component exports its own SLURM_STEPMGR_HET_GROUP_<N>.

    --exclusive forces the components onto different nodes so that a
    regression broadcasting the leader's host to every suffix is caught.
    """

    file_in = "hetjob_stepmgr_env.in"
    file_out = "hetjob_stepmgr_env.out"
    atf.make_bash_script(
        file_in,
        f"""
#SBATCH -N1 --ntasks=1 -t2 --exclusive --stepmgr
#SBATCH hetjob
#SBATCH -N1 --ntasks=1 -t2 --exclusive

env
echo {OUTPUT_MARKER}
sleep infinity
""",
    )

    leader_job_id = atf.submit_job_sbatch(f"-o {file_out} {file_in}", fatal=True)

    # The script stays alive after dumping env so that BatchHost is still
    # readable for every component once the dump is complete.
    atf.assert_file_contents(file_out, OUTPUT_MARKER, contains=True)

    jobs = atf.get_jobs(leader_job_id, fatal=True)
    components = sorted(
        atf.range_to_list(jobs[leader_job_id]["HetJobIdSet"]),
        key=lambda job_id: jobs[job_id]["HetJobOffset"],
    )
    batch_hosts = [jobs[job_id]["BatchHost"] for job_id in components]

    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    if batch_hosts[0] == batch_hosts[1]:
        pytest.fail(
            f"The components should be on different nodes for this test to be "
            f"meaningful, got {batch_hosts}"
        )

    for offset, host in enumerate(batch_hosts):
        for group in ("HET_GROUP", "PACK_GROUP"):
            var = f"SLURM_STEPMGR_{group}_{offset}"
            assert (
                re.search(rf"^{var}={host}$", output, re.MULTILINE) is not None
            ), f"Missing {var}={host} in batch environment"

    assert (
        re.search(r"^SLURM_STEPMGR=\S+$", output, re.MULTILINE) is not None
    ), "Missing bare SLURM_STEPMGR, which older clients still rely on"

    double_suffixed = re.findall(
        r"^(SLURM_\w+_(?:HET|PACK)_GROUP_\d+_(?:HET|PACK)_GROUP_\d+)=",
        output,
        re.MULTILINE,
    )
    assert (
        double_suffixed == []
    ), f"Env vars must be suffixed once per component, got {double_suffixed}"


def test_hetjob_stepmgr_srun_task_env():
    """srun exports the per-component stepmgr vars in the task environment.

    srun builds the task environment itself, so the batch environment checked
    above comes from a different producer and does not cover it.
    """

    file_in = "hetjob_stepmgr_srun_env.in"
    file_out = "hetjob_stepmgr_srun_env.out"
    atf.make_bash_script(
        file_in,
        f"""
#SBATCH -N1 --ntasks=1 -t2 --exclusive --stepmgr
#SBATCH hetjob
#SBATCH -N1 --ntasks=1 -t2 --exclusive

srun --het-group=0,1 env >> {file_out}
echo {OUTPUT_MARKER} >> {file_out}
""",
    )

    leader_job_id = atf.submit_job_sbatch(f"-o /dev/null {file_in}", fatal=True)
    atf.wait_for_job_state(leader_job_id, "DONE", fatal=True)
    atf.assert_file_contents(file_out, OUTPUT_MARKER, contains=True)

    output = atf.run_command_output(f"cat {file_out}", fatal=True)

    for offset in (0, 1):
        for group in ("HET_GROUP", "PACK_GROUP"):
            var = f"SLURM_STEPMGR_{group}_{offset}"
            assert (
                re.search(rf"^{var}=\S+$", output, re.MULTILINE) is not None
            ), f"Missing {var} in the srun task environment"

    double_suffixed = re.findall(
        r"^(SLURM_\w+_(?:HET|PACK)_GROUP_\d+_(?:HET|PACK)_GROUP_\d+)=",
        output,
        re.MULTILINE,
    )
    assert (
        double_suffixed == []
    ), f"Task env vars must be suffixed once per component, got {double_suffixed}"
