############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Cloud node first registration binds count-only GRES jobs in place.

Submit a job while the CLOUD node is still powered down (no slurmd state on
the controller), then start slurmd via ResumeProgram for first registration.
ResumeProgram writes an AutoDetect-only gres.conf; slurmd discovers device
topology from fake_gpus.conf. The count-only GRES allocation is then bound to
concrete devices in place: the job stays CONFIGURING and proceeds straight to
RUNNING -- it is never de-allocated back to PENDING. Pre-fix slurmctld could
fail validation with "count changed and jobs are using them", draining the
node instead.

Both untyped (gpu:N) and typed (gpu:<type>:N) requests are exercised; the
deferred allocation is completed by re-running the real allocator, so typed
GRES binds in place too. Hetjobs are out of scope.

A separate test verifies that ConstrainDevices enforces exactly the bound GPUs
on a job step, when cgroup device BPF is available on the host.
"""

import logging
import re
from pathlib import Path

import pytest

import atf

pytestmark = pytest.mark.slow

node_prefix = "node"
cloud_node = f"{node_prefix}1"
cloud_feature = "f1"
gpu_count = 4
gpus_requested = 2
# Typed GPUs (declared in slurm.conf and discovered from fake_gpus.conf) so the
# count-only-then-bind path is exercised for a typed request too.
gpu_type = "gp100"
# A consumable GRES with no device File anywhere: count-only by design, so it
# must never be mistaken for a deferred bind that failed.
fileless_gres = "bandwidth"
fileless_count = 100
fileless_requested = 10
suspend_time = 10
# slurmd -b in ResumeProgram blocks until registration; AutoDetect needs headroom.
suspend_timeout = 100
resume_timeout = 100

slurmd_bin = atf.properties["slurm-sbin-dir"] + "/slurmd"


@pytest.fixture(scope="module", autouse=True)
def setup(request):
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Ticket 25123: Cloud GRES topology bind-in-place on node registration added in 26.11",
    )
    atf.get_run_dir_path()
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("TreeWidth", 65533)
    atf.require_config_parameter(
        "ResumeProgram", resume_ctld_script(f"{atf.module_tmp_path}/def_resume.sh")
    )
    atf.require_config_parameter(
        "SuspendProgram", suspend_ctld_script(f"{atf.module_tmp_path}/def_suspend.sh")
    )
    atf.require_config_parameter("SuspendTime", suspend_time)
    atf.require_config_parameter("SuspendTimeout", suspend_timeout)
    atf.require_config_parameter("ResumeTimeout", resume_timeout)
    atf.require_config_parameter_includes("SlurmctldParameters", "idle_on_node_suspend")
    # So the terminal (resume-failure) path requeues the job instead of killing
    # it, giving a deterministic PENDING outcome to assert.
    atf.require_config_parameter_includes(
        "SchedulerParameters", "requeue_on_resume_failure"
    )
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", fileless_gres)
    atf.require_config_parameter("ProctrackType", "proctrack/cgroup")
    atf.require_config_parameter_includes("TaskPlugin", "cgroup")
    atf.require_config_parameter("ConstrainDevices", "yes", source="cgroup")
    # task/cgroup reports an inactive device cgroup only under this flag; without
    # it _assert_step_device_constraint() cannot tell the operator *why* device
    # constraining is not being enforced on this host.
    atf.require_config_parameter_includes("DebugFlags", "Cgroup")

    _ensure_gres_devices()
    # Register the removal now rather than after the yield below: if anything
    # in the rest of this fixture raises (e.g. start_slurmctld), the post-yield
    # code never runs and the root-owned device nodes leak into the pytest tmp
    # tree, where ATF's cleanup cannot unlink them as the test user.
    request.addfinalizer(_remove_gres_devices)

    # Count-only on slurmctld until ResumeProgram writes AutoDetect gres.conf.
    _write_gres_conf("")
    _write_fake_gpus_conf("")

    atf.require_config_parameter(
        "NodeName",
        {
            cloud_node: {
                "Feature": cloud_feature,
                "State": "CLOUD",
                "Gres": f"gpu:{gpu_type}:{gpu_count},{fileless_gres}:{fileless_count}",
            }
        },
    )
    atf.require_config_parameter("Nodeset", {"ns1": {"Feature": cloud_feature}})
    atf.require_config_parameter(
        "PartitionName",
        {
            "primary": {"Nodes": "ALL"},
            # YES only shares CPUs with jobs that ask (--oversubscribe).
            "cloud1": {"Nodes": "ns1", "OverSubscribe": "YES"},
        },
    )

    _ensure_node_spool_dirs(cloud_node)

    atf.start_slurmctld(clean=True)

    yield

    kill_slurmds()
    kill_slurmctld()


def _gpu_dev_base():
    return f"{atf.module_tmp_path}/gpu"


def _fake_gpus_conf_lines():
    lines = []
    for i in range(gpu_count):
        path = f"{_gpu_dev_base()}{i}"
        # Fields are type|sys_cpu_count|cpu_range|links|device_file|unique_id|
        # flags (see gpu_g_get_system_gpu_list()). cpu_range is left unset so
        # the GPU is not tied to specific cores on the node.
        lines.append(f"{gpu_type}|2|(null)|(null)|{path}|(null)|nvidia_gpu_env")
    return "\n".join(lines)


def _write_fake_gpus_conf(content, fatal=True):
    path = f"{atf.properties['slurm-config-dir']}/fake_gpus.conf"
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=True,
    )


def _ensure_gres_devices():
    """
    Fake GRES device nodes. Major 1 (mem) with an unassigned minor (50+) has no
    backing driver, so a step *allowed* to open it by the device cgroup gets
    ENXIO ("No such device or address"), while a step *denied* by the cgroup
    gets EPERM ("Operation not permitted"). That errno difference is how
    _assert_step_device_constraint() tells bound devices from constrained ones
    without real GPUs.
    """
    for i in range(gpu_count):
        path = f"{_gpu_dev_base()}{i}"
        atf.run_command(
            f"rm -f {path}; mknod -m 666 {path} c 1 {50 + i}",
            user="root",
            fatal=True,
            quiet=True,
        )


def _remove_gres_devices():
    """Remove the root-owned fake device nodes.

    ATF's tmp cleanup runs as the test user and cannot unlink them.
    """
    for i in range(gpu_count):
        atf.run_command(f"rm -f {_gpu_dev_base()}{i}", user="root", quiet=True)


script_preamble = """
SCRIPT_DIR="$( cd -- "$( dirname -- "${{BASH_SOURCE[0]:-$0}}"; )" &> /dev/null && pwd 2> /dev/null; )";
exec &> >(tee -a $SCRIPT_DIR/{script_name}.log)
PS4='+ $(date "+%y-%m-%dT%H:%M:%S") ($SLURM_NODE_NAME)\011 '
echo $@
set -x
"""


def _count_only_marker():
    # When this marker exists, ResumeProgram registers count-only GRES (no
    # device File topology) to exercise the terminal/resume-failure path.
    return f"{atf.module_tmp_path}/count_only_gres"


def resume_ctld_script(path):
    gres_conf = f"{atf.properties['slurm-config-dir']}/gres.conf"
    fake_gpus_conf = f"{atf.properties['slurm-config-dir']}/fake_gpus.conf"
    fake_lines = _fake_gpus_conf_lines()
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
sleep 2 # wait for slurmctld to update node state
if [ -f {_count_only_marker()} ]; then
    # Register the configured GPU count but no device File topology, so the
    # controller cannot bind the count-only allocation to devices.
    cat > {gres_conf} <<'GRES_EOF'
Name=gpu Type={gpu_type} Count={gpu_count}
GRES_EOF
else
    cat > {fake_gpus_conf} <<'FAKE_EOF'
{fake_lines}
FAKE_EOF
    cat > {gres_conf} <<'GRES_EOF'
AutoDetect=nvidia
GRES_EOF
fi
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostname $SLURM_NODE_NAME); do
    sudo {slurmd_bin} -N $node -b --conf 'feature={cloud_feature}'
done
"""
    atf.make_bash_script(path, content)
    return path


def suspend_ctld_script(path):
    pidfile_template = atf.get_run_dir_path() + "/slurmd.$node.pid"
    content = f"""
SLURM_NODE_NAME=$1
{script_preamble.format(script_name=Path(path).stem)}
for node in $({atf.properties["slurm-bin-dir"]}/scontrol show hostname $SLURM_NODE_NAME); do
    sudo pkill -F {pidfile_template}
done
"""
    atf.make_bash_script(path, content)
    return path


def _write_gres_conf(content, fatal=True):
    path = f"{atf.properties['slurm-config-dir']}/gres.conf"
    atf.run_command(
        f"cat > {path}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=fatal,
        quiet=True,
    )


def _ensure_node_spool_dirs(node_name):
    spool_dir = atf.properties["slurm-spool-dir"].replace("%n", node_name)
    tmpfs_dir = atf.properties["slurm-tmpfs"].replace("%n", node_name)
    atf.run_command(
        f"mkdir -p {spool_dir} {tmpfs_dir}", user="root", fatal=True, quiet=True
    )
    # conftest removes these after the module only for nodes listed here.
    if node_name not in atf.properties["nodes"]:
        atf.properties["nodes"].append(node_name)


@pytest.fixture(scope="module")
def device_cgroup_testable():
    """Skip when the fake GPU device nodes cannot be opened at all.

    _assert_step_device_constraint() distinguishes a cgroup-allowed device from
    a cgroup-denied one by errno, which only works if an *unconstrained* open of
    a fake node reaches the (absent) driver and fails with ENXIO. If the pytest
    tmp filesystem is mounted 'nodev' the kernel rejects every open with EACCES
    ("Permission denied") instead, which is indistinguishable from a cgroup
    denial and would make the errno pattern meaningless. Probe once here,
    outside of any job, rather than hard-failing on an unclassifiable errno.
    """
    probe = f"{_gpu_dev_base()}0"
    result = atf.run_command(f"cat {probe}", quiet=True)
    err = (result["stderr"] or "").strip()
    if "No such device or address" not in err:
        pytest.skip(
            f"Cannot tell cgroup-denied from cgroup-allowed GPU devices here: "
            f"opening {probe} outside of any cgroup reported {err!r} instead of "
            f"ENXIO. The filesystem holding {atf.module_tmp_path} is likely "
            f"mounted 'nodev'. Run on a host where device nodes can be opened "
            f"there."
        )


@pytest.fixture(scope="function", autouse=True)
def cloud_state():
    """Stop all jobs and reset cloud node state around each test."""
    # Pre-test gate: make sure slurmctld is responsive and the cloud node is
    # powered down before the test issues its first RPC, so a slow (or failed)
    # previous-test teardown restart cannot race this test's submit/queries.
    assert atf.repeat_until(
        atf.is_slurmctld_running,
        lambda up: up,
        timeout=60,
    ), "slurmctld did not become responsive before the test"
    atf.wait_for_node_state(cloud_node, "POWERED_DOWN", timeout=60, fatal=False)

    yield

    atf.cancel_all_jobs()
    kill_slurmds()
    atf.run_command(f"rm -f {_count_only_marker()}", quiet=True)
    # Not fatal: a failed write must not skip the clean restart below, or the
    # controller keeps the devices this test registered and the next test's
    # jobs are bound at scheduling instead of at registration.
    _write_gres_conf("", fatal=False)
    _write_fake_gpus_conf("", fatal=False)
    atf.restart_slurmctld(clean=True)
    # Wait for the cloud node to settle back to POWERED_DOWN instead of a blind
    # sleep, so the next test starts from a known state on both fast and slow
    # hosts.
    atf.wait_for_node_state(cloud_node, "POWERED_DOWN", timeout=60, fatal=False)


def _kill_by_pidof(binary):
    pids = (atf.run_command_output(f"pidof {binary}", quiet=True) or "").split()
    for pid in pids:
        if pid:
            # Not fatal: the pid may exit (e.g. SuspendProgram) between the
            # pidof sample and the kill; the repeat_until below confirms exit.
            atf.run_command(f"kill {pid}", user="root", fatal=False, quiet=True)
    # Confirm the processes actually exited before the caller rewrites config or
    # restarts; a lingering daemon would poison the next test. Escalate to
    # SIGKILL if they do not exit in time.
    if not atf.repeat_until(
        lambda: (atf.run_command_output(f"pidof {binary}", quiet=True) or "").strip(),
        lambda out: out == "",
        timeout=30,
    ):
        # Re-sample and SIGKILL by pid rather than by name: a bare
        # "pkill -9 -x slurmd" would also kill daemons from another install
        # prefix on the same host.
        pids = (atf.run_command_output(f"pidof {binary}", quiet=True) or "").split()
        for pid in pids:
            if pid:
                atf.run_command(f"kill -9 {pid}", user="root", quiet=True)


def kill_slurmds():
    _kill_by_pidof(f"{atf.properties['slurm-sbin-dir']}/slurmd")


def kill_slurmctld():
    _kill_by_pidof(f"{atf.properties['slurm-sbin-dir']}/slurmctld")


def _job_gres_line(job_id):
    output = atf.run_command_output(f"scontrol show job -d {job_id}", fatal=True)
    match = re.search(r" GRES=([^\s]+)", output)
    return match.group(1) if match else ""


def _wait_for_node_power_up_complete():
    atf.wait_for_node_state(
        cloud_node,
        "POWERING_UP",
        reverse=True,
        timeout=resume_timeout + 5,
        fatal=True,
    )


def _wait_for_cloud_power_up():
    atf.wait_for_node_state(cloud_node, "ALLOCATED", timeout=30, fatal=True)
    state = atf.get_node_parameter(cloud_node, "state") or ""
    if "POWERING_UP" not in state:
        # Observing POWERING_UP is best-effort: on a fast power-up the flag may
        # already be cleared, so do not hard-fail if it never reappears -- just
        # proceed to wait for power-up completion.
        atf.wait_for_node_state(cloud_node, "POWERING_UP", timeout=45, fatal=False)
    _wait_for_node_power_up_complete()


def _wait_for_cloud_power_up_after_slurmctld_restart():
    """Resume may already be in progress when state is restored after restart."""
    state = atf.get_node_parameter(cloud_node, "state") or ""
    if "POWERED_DOWN" in state:
        _wait_for_cloud_power_up()
    else:
        _wait_for_node_power_up_complete()


def _slurmctld_log_size():
    """Current byte size of the slurmctld log, used as a search marker."""
    log_file = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    output = atf.run_command_output(f"wc -c < {log_file}", user="root", quiet=True)
    return int(output.strip()) if output and output.strip() else 0


def _slurmctld_log_text(since=0):
    log_file = atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)
    # If the log was rotated/truncated below the mark, the byte offset is stale
    # and would over-skip; fall back to reading the whole file in that case.
    if since > 0 and _slurmctld_log_size() >= since:
        cmd = f"tail -c +{since + 1} {log_file}"
    else:
        cmd = f"cat {log_file}"
    return atf.run_command_output(cmd, user="root", quiet=True) or ""


def _slurmd_log_text():
    log_file = atf.get_config_parameter("SlurmdLogFile", live=False, quiet=True)
    log_file = log_file.replace("%n", cloud_node)
    return atf.run_command_output(f"cat {log_file}", user="root", quiet=True) or ""


def _assert_count_only_before_registration(job_id, gres_type=None):
    """The job holds a count-only GRES reservation until slurmd registers.

    Observing the count-only state is inherently racy: ResumeProgram may have
    already started slurmd and the controller may have already bound the
    allocation by the time this runs. Accept an already-bound (IDX:) allocation
    rather than fail -- _assert_gres_bound() still proves the bind happened.
    What must never appear here is neither form.
    """
    gres_line = _job_gres_line(job_id)

    assert "(CNT:" in gres_line or "(IDX:" in gres_line, (
        "expected a count-only (CNT:) or already-bound (IDX:) GRES allocation "
        f"while the cloud node powers up, got {gres_line!r}"
    )

    if gres_type is not None:
        assert (
            gres_type in gres_line
        ), f"expected type {gres_type} in the GRES allocation, got {gres_line!r}"

    if "(CNT:" not in gres_line:
        logging.info(
            f"Cloud node registered before the count-only GRES of job {job_id} "
            f"could be observed; GRES is already {gres_line!r}"
        )


def _assert_bound_in_place_logged(job_id, since=0):
    """slurmctld logged the in-place bind of this job's GRES.

    Search only the log written after `since` so a clean restart (which resets
    the JobId counter while the shared log keeps growing) cannot match a line
    left by an earlier test that reused the same JobId. Poll rather than read
    once, since leaving POWERING_UP and flushing the log line are not ordered.
    """
    bound_pat = rf"Bound JobId={job_id} GRES in place on node {cloud_node}"

    assert atf.repeat_until(
        lambda: _slurmctld_log_text(since),
        lambda text: re.search(bound_pat, text) is not None,
        timeout=30,
    ), (
        "Job GRES should be bound in place after cloud node topology "
        f"registration; slurmctld log excerpt:\n{_slurmctld_log_text(since)[-4000:]}"
    )


def _assert_gres_bound(job_id, since=0):
    # With in-place binding the job never leaves CONFIGURING: at registration
    # the count-only reservation is bound to concrete devices and the job
    # proceeds straight to RUNNING (it is never de-allocated back to PENDING).
    # The in-place path is confirmed by the "Bound ... in place" log plus the
    # per-device IDX allocation; a bounce/de-allocation would not emit that log
    # and would not yield an IDX allocation.
    _assert_bound_in_place_logged(job_id, since)

    atf.wait_for_job_state(job_id, "RUNNING", timeout=90, fatal=True)

    gres_line = _job_gres_line(job_id)
    assert (
        "(IDX:" in gres_line
    ), f"expected per-device GRES after in-place bind, got {gres_line!r}"
    assert (
        "(CNT:" not in gres_line
    ), f"count-only GRES should not remain after bind, got {gres_line!r}"


def _bound_gpu_indices(job_id):
    """The device indices in the job's bound GRES, e.g. {0, 1} for IDX:0-1."""
    match = re.search(r"\(IDX:([^)]*)\)", _job_gres_line(job_id))
    assert match, f"expected per-device GRES, got {_job_gres_line(job_id)!r}"
    indices = set()
    for part in match.group(1).split(","):
        first, _, last = part.partition("-")
        indices.update(range(int(first), int(last or first) + 1))
    return indices


def _assert_node_healthy_after_gres_registration():
    """Node must not be drained or down after GRES topology registration."""
    node_show = atf.run_command_output(f"scontrol show node {cloud_node}", fatal=True)
    state = atf.get_node_parameter(cloud_node, "state") or ""
    assert (
        "DOWN" not in state
    ), f"cloud node should not be DOWN after registration: {node_show}"
    assert (
        "DRAIN" not in state
    ), f"cloud node should not be DRAIN after registration: {node_show}"
    assert "count changed and jobs are using them" not in node_show, (
        "node Reason should not report GRES count changed while jobs are " "allocated"
    )

    gres = atf.get_node_parameter(cloud_node, "gres") or ""
    assert re.search(
        r"gpu(:|\b)", gres, re.I
    ), f"expected gpu in node Gres after registration, got {gres!r}"


def _assert_step_device_constraint(job_id, gres_type=None):
    """
    Verify ConstrainDevices actually enforces the bound GPUs on a job step.

    Each fake GPU node is major 1 with an unassigned minor, so opening it yields
    a cgroup-dependent errno: a device the step is *allowed* returns ENXIO ("No
    such device or address"); a device the step is *denied* returns EPERM
    ("Operation not permitted"). A step with gpus_requested GPUs must therefore
    see exactly gpus_requested devices report ENXIO ("A") and the rest report
    EPERM ("D").
    """
    dev_paths = " ".join(f'"{_gpu_dev_base()}{i}"' for i in range(gpu_count))
    check_script = f"{atf.module_tmp_path}/check_gres_devices.sh"
    atf.make_bash_script(
        check_script,
        f"""
        echo "CUDA_VISIBLE_DEVICES=${{CUDA_VISIBLE_DEVICES}}"
        access=""
        for f in {dev_paths}; do
            err=$(cat "$f" 2>&1 >/dev/null)
            if [[ $? -eq 0 ]]; then
                access="${{access}}R"                       # readable (unconstrained)
            elif [[ "$err" == *"Operation not permitted"* ]]; then
                access="${{access}}D"                       # cgroup denied (EPERM)
            elif [[ "$err" == *"No such device or address"* ]]; then
                access="${{access}}A"                       # cgroup allowed (ENXIO)
            else
                access="${{access}}?"
            fi
        done
        echo "Dev_Access: ${{access}}"
        """,
    )

    gres_spec = (
        f"gpu:{gres_type}:{gpus_requested}" if gres_type else f"gpu:{gpus_requested}"
    )
    # --overlap lets this verification step share the CPUs, memory and GRES that
    # the job's own step may already hold; without it step creation would block
    # until the job ended and time out. --overlap has been available since 20.11,
    # well below the versions this test is gated to.
    step_out = atf.run_command_output(
        f"srun --overlap --jobid={job_id} --gres={gres_spec} {check_script}",
        fatal=True,
    )

    cuda_match = re.search(r"CUDA_VISIBLE_DEVICES=(.*)", step_out)
    assert cuda_match, f"step output missing CUDA_VISIBLE_DEVICES: {step_out!r}"
    cuda_indices = cuda_match.group(1).strip().split(",")
    assert len(cuda_indices) == gpus_requested, (
        f"expected {gpus_requested} GPU indices in CUDA_VISIBLE_DEVICES, "
        f"got {cuda_indices!r}; output={step_out!r}"
    )

    access_match = re.search(r"Dev_Access:\s*([RDA?]+)", step_out)
    assert access_match, f"step output missing Dev_Access: {step_out!r}"
    access = access_match.group(1)
    assert (
        len(access) == gpu_count
    ), f"expected {gpu_count} device checks, got {access!r}; output={step_out!r}"

    assert "?" not in access, (
        f"unexpected device open errno (not EPERM/ENXIO) in pattern {access!r}; "
        f"output={step_out!r}"
    )

    allowed = access.count("A")
    denied = access.count("D")

    # These fake GPU nodes are driverless (major 1, unassigned minor), so a
    # device the cgroup *allows* opens through to the driver and fails with
    # ENXIO ("A"), never exit 0 -- an enforcing cgroup denies the rest with
    # EPERM ("D"). Since the tests always request fewer GPUs than the node has,
    # an enforcing cgroup denies at least one device; zero denials therefore
    # means the device cgroup BPF is not restricting access here (e.g. slurmd
    # in a userns without a BPF token, common in SQA Docker). Nothing to
    # enforce; skip.
    if denied == 0:
        slurmd_log = _slurmd_log_text()
        if re.search(r"Skipping .*device constrain", slurmd_log):
            pytest.skip(
                "ConstrainDevices is configured but inactive here: slurmd runs "
                "in a user namespace without a cgroup BPF token (common in SQA "
                "Docker). Run on bare metal or a privileged host to enforce this."
            )
        pytest.fail(
            "ConstrainDevices is active but no GPU device was denied to the "
            f"step; pattern {access!r}; output={step_out!r}"
        )

    assert allowed == gpus_requested, (
        f"cgroup should allow exactly {gpus_requested} bound GPU device(s) "
        f"(ENXIO), got pattern {access!r} (A=allowed/ENXIO, D=denied/EPERM, "
        f"R=unconstrained); output={step_out!r}"
    )
    assert allowed + denied == gpu_count, (
        f"every GPU device should be either cgroup-allowed or -denied, got "
        f"{access!r}; output={step_out!r}"
    )


def test_cloud_resume_sanity():
    """ResumeProgram starts slurmd and a non-GRES job completes."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    job_id = atf.submit_job_sbatch(
        "-p cloud1 --wrap 'srun hostname'",
        fatal=True,
    )

    _wait_for_cloud_power_up()

    atf.wait_for_job_state(
        job_id, "COMPLETED", timeout=atf.PERIODIC_TIMEOUT + 15, fatal=True
    )


def test_cloud_first_registration_gres_bind_sbatch():
    """Batch job binds GRES in place after register and gets indexed GRES."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    # Mark the log before submitting: ResumeProgram may start slurmd, and the
    # controller may emit the bind message, before the checks below complete.
    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres=gpu:{gpus_requested} -t 300 " "--wrap='/bin/sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id)

    _wait_for_cloud_power_up()
    _assert_gres_bound(job_id, log_mark)
    _assert_node_healthy_after_gres_registration()


def test_cloud_first_registration_gres_bind_typed():
    """A typed GRES request (gpu:<type>:N) also binds in place at registration."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres=gpu:{gpu_type}:{gpus_requested} -t 300 "
        "--wrap='/bin/sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id, gres_type=gpu_type)

    _wait_for_cloud_power_up()
    _assert_gres_bound(job_id, log_mark)
    assert gpu_type in _job_gres_line(
        job_id
    ), f"expected type {gpu_type} in bound GRES, got {_job_gres_line(job_id)!r}"
    _assert_node_healthy_after_gres_registration()


def test_cloud_first_registration_gres_bind_srun():
    """Interactive allocation binds GRES in place after register and gets indexed GRES."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_srun(
        f"-p cloud1 --gres=gpu:{gpus_requested} -t 300 /bin/sleep infinity",
        background=True,
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id)

    _wait_for_cloud_power_up()
    _assert_gres_bound(job_id, log_mark)
    _assert_node_healthy_after_gres_registration()


def test_cloud_gres_bind_after_slurmctld_restart():
    """CONFIGURING count-only job binds in place after slurmctld restart."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres=gpu:{gpus_requested} -t 300 --wrap='/bin/sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id)

    atf.restart_slurmctld(clean=False)

    _wait_for_cloud_power_up_after_slurmctld_restart()
    _assert_gres_bound(job_id, log_mark)
    _assert_node_healthy_after_gres_registration()


def test_cloud_registration_with_zero_gpus_drains():
    """Terminal path: a cloud node that powers up without usable GPU device
    topology cannot have its count-only allocation bound, so the controller
    drains the node and requeues the job instead of leaving it stuck
    CONFIGURING.

    A gres.conf that lists the GPU count with no device File (and no AutoDetect)
    makes slurmd register zero usable GPUs, so _node_config_validate() rejects
    the registration on the pre-existing "count reported lower than configured"
    check and drains the node before any deferred bind is attempted. The job is
    then requeued to PENDING rather than left CONFIGURING forever.

    NOTE: this exercises the GRES count-validation terminal path, NOT the
    bind-failure path in node_mgr_bind_jobs_on_gres_ready(). That one needs a
    node that registers the configured count but still cannot be bound, which
    this fixture cannot produce for gpu: a File-less gpu record registers as
    zero, not as a count-only gpu.
    """

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    # A drain left over from an earlier test would satisfy the wait below
    # without this registration ever happening, so require a clean node first.
    start_state = atf.get_node_parameter(cloud_node, "state") or ""
    assert (
        "DRAIN" not in start_state
    ), f"cloud node {cloud_node} was already drained at test start: {start_state!r}"

    # Make ResumeProgram register a GPU count with no device File topology.
    atf.run_command(f"touch {_count_only_marker()}", fatal=True, quiet=True)

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres=gpu:{gpu_type}:{gpus_requested} -t 300 "
        "--wrap='/bin/sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id, gres_type=gpu_type)

    # The node powers up and registers no usable GPUs, so the controller
    # rejects the registration and drains the node.
    atf.wait_for_node_state(
        cloud_node, "DRAIN", timeout=resume_timeout + 60, fatal=True
    )
    reason = atf.get_node_parameter(cloud_node, "reason") or ""
    assert re.search(
        r"gres/gpu count reported lower than configured \(0 < \d+\)", reason
    ), (
        "expected the GRES count-validation drain reason set by "
        f"_node_config_validate(), got {reason!r}"
    )

    # Pin down that the drain really came from this registration rather than
    # from leftover state: the node must have been healthy when the test began,
    # and slurmctld must log the rejection after the job was submitted.
    assert atf.repeat_until(
        lambda: _slurmctld_log_text(log_mark),
        lambda text: "count reported lower than configured" in text,
        timeout=30,
    ), (
        "slurmctld should log the GRES count rejection for this registration; "
        f"log excerpt:\n{_slurmctld_log_text(log_mark)[-4000:]}"
    )

    # Because the node was powering up, the job is requeued rather than killed
    # (requeue_on_resume_failure), so it returns to PENDING instead of hanging
    # in CONFIGURING.
    atf.wait_for_job_state(job_id, "PENDING", timeout=60, fatal=True)


def test_cloud_first_registration_gpus_flag_bind_srun():
    """Job-level --gpus (not --gres=gpu:N) also binds count-only GRES in place on
    first cloud registration, and steps can allocate the bound GPUs.

    Regression: --gpus / --gpus-per-* leave gres_per_node == 0 and carry the
    count in gres_cnt_node_select, so the count-only allocation must take its
    per-node count from the derived count rather than gres_per_node. Otherwise
    the cloud node registers a 0-count typed GRES that is never recognized as
    count-only (so it never binds) and steps fail to allocate it with
    "Invalid generic resource (gres) specification" (ESLURM_INVALID_GRES).
    """

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    # The srun's own step is created right after the deferred in-place bind, so
    # a failure to bind would fail step creation and the job would never run.
    job_id = atf.submit_job_srun(
        f"-p cloud1 --gpus={gpus_requested} -t 300 /bin/sleep infinity",
        background=True,
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id)

    _wait_for_cloud_power_up()
    _assert_gres_bound(job_id, log_mark)
    _assert_node_healthy_after_gres_registration()

    # An srun job is RUNNING once its allocation is granted, so that alone does
    # not show its steps can use the bound GPUs. Create one and check.
    result = atf.run_command(
        f"srun --overlap --jobid={job_id} --gpus={gpus_requested} true"
    )
    assert result["exit_code"] == 0, (
        f"a step should allocate the {gpus_requested} bound GPUs, got "
        f"rc={result['exit_code']}: {result['stderr']!r}"
    )


def test_cloud_first_registration_binds_two_jobs():
    """Two count-only jobs on one cloud node bind to different GPUs."""

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    # node1 has one CPU, so the second job shares it (--oversubscribe).
    # GRES is never shared, so each job still needs GPUs of its own.
    job_ids = [
        atf.submit_job_sbatch(
            f"-p cloud1 --oversubscribe --gres=gpu:{gpus_requested} -t 300 "
            "--wrap='sleep infinity'",
            fatal=True,
        )
        for _ in range(2)
    ]
    for job_id in job_ids:
        atf.wait_for_job_state(job_id, "CONFIGURING", fatal=True)
        _assert_count_only_before_registration(job_id)

    # Two jobs share the node, so it reports MIXED, never ALLOCATED.
    _wait_for_node_power_up_complete()
    for job_id in job_ids:
        _assert_gres_bound(job_id, log_mark)

    first, second = (_bound_gpu_indices(job_id) for job_id in job_ids)
    assert (
        len(first) == gpus_requested and len(second) == gpus_requested
    ), f"each job should bind {gpus_requested} GPUs, got {first} and {second}"
    assert not first & second, f"the jobs were bound to the same GPUs: {first & second}"
    _assert_node_healthy_after_gres_registration()


@pytest.mark.parametrize(
    "gres_spec, expect_gpu_bind",
    [
        (f"{fileless_gres}:{fileless_requested}", False),
        (f"gpu:{gpus_requested},{fileless_gres}:{fileless_requested}", True),
    ],
    ids=["fileless_only", "gpu_and_fileless"],
)
def test_cloud_registration_fileless_gres_not_drained(gres_spec, expect_gpu_bind):
    """A GRES with no device File is left count-only, not failed, at registration.

    A GRES with no File topology anywhere (here bandwidth, declared only in
    slurm.conf) is count-only by design: there are no devices to bind it to.
    First cloud registration must launch such a job as it is, rather than read
    its count-only allocation as a failed bind, drain the node and fail the job.
    When the job also requests GPUs, the GPUs still bind in place and the
    fileless GRES stays count-only.
    """

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres={gres_spec} -t 300 --wrap='sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", fatal=True)
    _wait_for_cloud_power_up()

    # Treating the fileless GRES as a failed bind drains the node and fails the
    # job back to PENDING, so it would never reach RUNNING.
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    _assert_node_healthy_after_gres_registration()

    log_text = _slurmctld_log_text(log_mark)
    assert "could not bind GRES" not in log_text, (
        "a fileless GRES must not be treated as a failed bind; slurmctld log "
        f"excerpt:\n{log_text[-4000:]}"
    )

    gres_line = _job_gres_line(job_id)
    assert re.search(
        rf"\b{fileless_gres}\(CNT:{fileless_requested}\)", gres_line
    ), f"expected the fileless GRES to stay count-only, got {gres_line!r}"

    if expect_gpu_bind:
        _assert_bound_in_place_logged(job_id, log_mark)
        assert re.search(
            r"\bgpu[^,]*\(IDX:", gres_line
        ), f"expected per-device GPU GRES after in-place bind, got {gres_line!r}"


@pytest.mark.parametrize("gres_type", [None, gpu_type], ids=["untyped", "typed"])
def test_cloud_bound_gres_device_constraint(device_cgroup_testable, gres_type):
    """ConstrainDevices enforces exactly the GPUs bound in place at registration.

    Deliberately kept separate from the bind tests above: whether the device
    cgroup actually enforces anything depends on the host (slurmd in a user
    namespace without a BPF token cannot program the device BPF map), so this
    check may legitimately skip. Folding it into the bind tests would mark those
    SKIPPED too and silently erase their count-only-to-bound coverage.
    """

    assert "POWERED_DOWN" in atf.get_node_parameter(
        cloud_node, "state"
    ), f"cloud node {cloud_node} was not POWERED_DOWN at test start: {atf.get_node_parameter(cloud_node, 'state')!r}"

    gres_spec = (
        f"gpu:{gres_type}:{gpus_requested}" if gres_type else f"gpu:{gpus_requested}"
    )

    log_mark = _slurmctld_log_size()
    job_id = atf.submit_job_sbatch(
        f"-p cloud1 --gres={gres_spec} -t 300 --wrap='/bin/sleep infinity'",
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "CONFIGURING", timeout=45, fatal=True)
    _assert_count_only_before_registration(job_id, gres_type=gres_type)

    _wait_for_cloud_power_up()
    _assert_gres_bound(job_id, log_mark)
    _assert_node_healthy_after_gres_registration()
    _assert_step_device_constraint(job_id, gres_type=gres_type)
