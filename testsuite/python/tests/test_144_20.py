############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A step's GPU frequency setup covers every GRES record the step holds.

A step keeps one device bitmap per typed GRES record, so an untyped request
met by a later record leaves the first record all-zero. Two typed gpu
records over tty devices, each bound to one socket's cores, stage that
shape without GPU hardware: a first step takes the first type's cores, so
the frequency step gets its GPU from the second record. Since slurmd 26.11
the setup gathers every record. With GpuFreqDef unset, a --gpu-freq step in
that shape sets up its one device and a step with no request runs no setup;
with GpuFreqDef set, a step holding both GPUs sets up two.
"""

import re
from pathlib import Path

import pytest

import atf

# The first-requested type, bound to the cores the low step holds, so the
# frequency step's record for it stays all-zero.
empty_type = "ttya"

# The second-requested type, bound to the cores left for the frequency
# step, the record that satisfies its request.
used_type = "ttyb"

# The DebugFlags=Gres line gres_g_step_hardware_init() logs once it has
# gathered a step's devices. It is the only device-count observable without
# a GPU library, so a reword of that log line must be mirrored here.
setup_line = "setting up {count} gres/gpu device(s) allocated to the step on this node"

# The line gpu/generic, the plugin used without AutoDetect, prints to a
# step's stderr when the setup reaches it.
plugin_line = "GpuFreq=control_disabled"

# setup_line without its count, for the step that must never log it.
setup_line_tail = setup_line.partition("{count} ")[2]


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Gate on version before any host write, so an old slurmd skips cleanly.
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="Issue 50982: a step's hardware setup gathers every GRES record since 26.11",
    )
    # The second job needs GpuFreqDef set while the first ran without it.
    atf.require_config_parameter("GpuFreqDef", None)
    atf.require_config_parameter("SelectType", "select/cons_tres")
    # Strict, not _includes: CR_Core next to a variant's CR_CPU is invalid.
    atf.require_config_parameter("SelectTypeParameters", "CR_Core")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # The setup line is a verbose log_flag(GRES) line in the slurmd log.
    atf.require_config_parameter("SlurmdDebug", "verbose")
    atf.require_config_parameter_includes("DebugFlags", "Gres")
    atf.require_tty(0)
    atf.require_tty(1)
    # One typed record per socket, since gres.conf(5) wants all the cores of
    # a socket together under Cores=. The low step books cores 0-1, so the
    # frequency step gets cores 2-3, away from the first type's device.
    atf.require_config_file(
        "gres.conf",
        f"Name=gpu Type={empty_type} Cores=0-1 File=/dev/tty0\n"
        f"Name=gpu Type={used_type} Cores=2-3 File=/dev/tty1\n",
    )
    atf.require_nodes(
        1,
        [
            ("Gres", f"gpu:{empty_type}:1,gpu:{used_type}:1"),
            ("CPUs", 4),
            ("Sockets", 2),
            ("CoresPerSocket", 2),
            ("ThreadsPerCore", 1),
        ],
    )
    atf.require_slurm_running()


def _slurmd_logs():
    """The slurmd log files, listed as root since slurmd runs as root."""
    slurmd_log_conf = atf.get_config_parameter("SlurmdLogFile", live=False, quiet=True)
    if not slurmd_log_conf:
        pytest.fail("SlurmdLogFile is not set in slurm.conf")
    pattern = re.sub(r"%[hn]", "*", slurmd_log_conf)
    log_files = atf.run_command_output(f"ls {pattern}", user="root", quiet=True).split()
    if not log_files:
        pytest.fail(f"no slurmd log found matching SlurmdLogFile={slurmd_log_conf}")
    return log_files


def _slurmd_log_has(needle, step_id, wait=True):
    """True once a line of the given step carries the literal needle.

    slurmstepd brackets its lines with the step ID, which scopes the needle
    to one step. Without wait the log is read once, for a line that must
    never appear. False leaves the failure wording to the caller.
    """
    command = (
        f"grep -F '{needle}' {' '.join(_slurmd_logs())}"
        f" | grep -E '\\[{re.escape(step_id)}( |\\])'"
    )
    if not wait:
        return atf.run_command(command, user="root", quiet=True)["exit_code"] == 0
    for _ in atf.timer():
        if atf.run_command(command, user="root", quiet=True)["exit_code"] == 0:
            return True
    return False


def _step_id(step_files, name):
    """The <job>.<step> ID a recorded step wrote for itself, or fail."""
    step = atf.run_command_output(f"cat {step_files[f'{name}_step']}", quiet=True)
    step_id = f"{step_files[f'{name}_job']}.{step.strip()}"
    if not re.fullmatch(r"[0-9]+\.[0-9]+", step_id):
        pytest.fail(f"the {name} step left no usable step ID, got {step_id!r}")
    return step_id


@pytest.fixture(scope="module")
def step_files():
    """Run the two jobs and return the recorded steps' files and job IDs.

    Job 1 runs with GpuFreqDef unset over both GPUs and all four cores. A
    background step with no GRES books two cores first and marks a flag
    file, so the --gpu-freq step gets the other two cores and the GPU whose
    first record is empty; a step with no request then takes the same shape.
    Job 2 runs with GpuFreqDef set, one step with no request over both GPUs.
    Each recorded step writes its stderr, exit code, GPUs and step ID.
    """
    flag_file = "low_step_started"
    # What the background low-cores step writes into the flag file.
    low_step_mark = "low_step_running"
    # Absolute paths: the tests run in per-function directories.
    files = {
        f"{step}_{item}": str(Path(f"{step}_{item}").absolute())
        for step in ("one", "none", "both")
        for item in ("stderr", "rc", "gpus", "step")
    }
    # What a recorded step runs: note its GPUs and its step ID.
    record = {
        step: f"echo ${{SLURM_STEP_GPUS:-}} > {files[f'{step}_gpus']};"
        f" echo $SLURM_STEP_ID > {files[f'{step}_step']}"
        for step in ("one", "none", "both")
    }

    def run_job(batch_file, script):
        """Submit the script over both GPUs and all four cores, return its ID."""
        batch_output = f"{batch_file}.out"
        atf.make_bash_script(batch_file, script)
        job_id = atf.submit_job_sbatch(
            f"-N1 -n1 -c4 -t2 -J test_144_20 "
            f"--gres=gpu:{empty_type}:1,gpu:{used_type}:1 "
            f"-o {batch_output} {batch_file}",
            fatal=True,
        )
        # Pending plus run time; the job's two-minute limit bounds the latter.
        if not atf.wait_for_job_state(job_id, "COMPLETED", timeout=130):
            batch_log = atf.run_command_output(f"cat {batch_output}", quiet=True)
            pytest.fail(f"job {job_id} did not complete, batch output: {batch_log!r}")
        return job_id

    # The background step sleeps forever rather than a fixed time a slow step
    # could outrun; the script kills it once the exit codes are recorded, and
    # the job's time limit bounds it anyway.
    staged_job = run_job(
        "staged.sh",
        f"""srun --mpi=none --exact -n1 -c2 --gres=none bash -c 'echo {low_step_mark} > {flag_file} && sleep infinity' &
low_srun_pid=$!
for i in $(seq 1 120); do
    if [ -e {flag_file} ]; then
        break
    fi
    sleep 0.5
done
if [ ! -e {flag_file} ]; then
    echo "the background low-cores step never started" >&2
    exit 1
fi
srun --mpi=none --exact -n1 -c2 --gres=gpu:1 --gpu-freq=low bash -c '{record['one']}' 2> {files['one_stderr']}
echo $? > {files['one_rc']}
srun --mpi=none --exact -n1 -c2 --gres=gpu:1 bash -c '{record['none']}' 2> {files['none_stderr']}
echo $? > {files['none_rc']}
kill $low_srun_pid 2> /dev/null
wait
exit 0
""",
    )
    files["one_job"] = staged_job
    files["none_job"] = staged_job

    # slurmd re-reads slurm.conf on reconfigure and hands every new stepd its
    # configuration; ATF restores the file after the module.
    atf.set_config_parameter("GpuFreqDef", "low")
    files["both_job"] = run_job(
        "default.sh",
        f"""srun --mpi=none --exact -n1 -c4 --gres=gpu:2 bash -c '{record['both']}' 2> {files['both_stderr']}
echo $? > {files['both_rc']}
exit 0
""",
    )
    return files


@pytest.mark.parametrize("step", ["one", "none", "both"])
def test_steps_complete(step_files, step):
    """Every recorded step exits 0: the setup never fails a step."""
    atf.assert_file_contents(
        step_files[f"{step}_rc"], "0", message=f"the {step} step should exit 0"
    )


def test_setup_covers_the_later_record(step_files):
    """The --gpu-freq step whose first record is empty sets up its one device.

    SLURM_STEP_GPUS names the second type's device, the staging premise, and
    the slurmd log shows one gathered device for the step. GpuFreqDef is
    unset for that job, so the option alone brings the step to the setup.
    """
    atf.assert_file_contents(
        step_files["one_gpus"],
        "1",
        message="staging premise: the --gpu-freq step must get its GPU from the"
        " second gres.conf record (SLURM_STEP_GPUS=1), or the empty-first-record"
        " shape is no longer staged and this module needs re-staging",
    )
    needle = setup_line.format(count=1)
    step = _step_id(step_files, "one")
    assert _slurmd_log_has(
        needle, step
    ), f"expected {needle!r} for step {step} in the slurmd log"


def test_no_request_runs_no_setup(step_files):
    """A step with no frequency request in the same shape runs no setup.

    With GpuFreqDef unset and no --gpu-freq the setup is never called: no
    gathered-device line for the step, no gpu/generic line in its stderr. A
    line every step logs is checked first, so an absence means the setup line
    was never written, not that the log or the step ID went unread.
    """
    atf.assert_file_contents(
        step_files["none_gpus"],
        "1",
        message="staging premise: the no-request step must get its GPU from the"
        " second gres.conf record (SLURM_STEP_GPUS=1), or the empty-first-record"
        " shape is no longer staged and this module needs re-staging",
    )
    step = _step_id(step_files, "none")
    assert _slurmd_log_has(
        "starting 1 tasks", step
    ), f"no slurmstepd line for step {step} in the slurmd log"
    assert not _slurmd_log_has(
        setup_line_tail, step, wait=False
    ), f"unexpected {setup_line_tail!r} for step {step} in the slurmd log"
    stderr = atf.run_command_output(
        f"cat {step_files['none_stderr']}", quiet=True, fatal=True
    )
    assert (
        plugin_line not in stderr
    ), f"the no-request step's stderr should not carry {plugin_line}, got {stderr!r}"


def test_gpu_freq_def_covers_both_records(step_files):
    """A GpuFreqDef step holding both GPUs sets up two devices.

    The step requests no frequency and its job has GpuFreqDef set, so the
    default brings it to the setup, which gathers its two records.
    """
    atf.assert_file_contents(
        step_files["both_gpus"],
        "0,1",
        message="the GpuFreqDef step should hold both devices",
    )
    needle = setup_line.format(count=2)
    step = _step_id(step_files, "both")
    assert _slurmd_log_has(
        needle, step
    ), f"expected {needle!r} for step {step} in the slurmd log"


@pytest.mark.parametrize("step", ["one", "both"])
def test_setup_reaches_the_plugin_once(step_files, step):
    """Each frequency step's stderr carries the gpu/generic line once.

    gpu/generic prints the line once per call, so one line per step pins the
    single call over the union of the step's records, on purpose. The fixture
    waited for the job to complete, so the file is read once: polling for a
    count of one would break on the very transient a second line arrives
    after, which is the shape this pins against.
    """
    stderr = atf.run_command_output(f"cat {step_files[f'{step}_stderr']}", quiet=True)
    assert (
        stderr.count(plugin_line) == 1
    ), f"the {step} step's stderr should carry {plugin_line} exactly once, got {stderr!r}"
