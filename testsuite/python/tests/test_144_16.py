############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""UUID vs AutoDetect, and env_uuid fallback when no UUID is available.

test_144_15 covers UUID with AutoDetect=off and no fake_gpus.conf.
test_144_10 covers env_uuid when fake_gpus.conf already supplies UUIDs.
This file covers the merge of the two: UUID as a sanity check against
AutoDetect, UUID filling in when AutoDetect has no UUID, and env_uuid
falling back to numeric indices when neither source provides one.
"""

import re

import pytest

import atf

GPU_0 = "GPU-11111111-1111-1111-1111-111111111111"
GPU_1 = "GPU-22222222-2222-2222-2222-222222222222"
GPU_2 = "GPU-33333333-3333-3333-3333-333333333333"
GPU_WRONG = "GPU-99999999-9999-9999-9999-999999999999"

NODE = "node1"

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="MR 4271: merging gres.conf UUIDs with AutoDetect UUIDs is new in 26.11",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("SlurmdParameters", "config_overrides")
    atf.require_config_parameter("IgnoreSystemd", "yes", source="cgroup")
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_tty(2)
    atf.require_config_file(
        "gres.conf",
        "AutoDetect=off\n" "Name=gpu File=/dev/tty0\n" "Name=gpu File=/dev/tty1\n",
    )
    atf.require_nodes(1, [("Gres", "gpu:2"), ("CPUs", 2)])
    atf.require_slurm_running()

    yield

    # gpu_g_get_system_gpu_list() uses fake_gpus.conf whenever it exists, so
    # the file must not outlive this module even if a test aborts partway.
    _remove_fake_gpus()


def _write_conf(name, content):
    atf.run_command(
        f"cat > {atf.properties['slurm-config-dir']}/{name}",
        input=content,
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def _remove_fake_gpus():
    atf.run_command(
        f"rm -f {atf.properties['slurm-config-dir']}/fake_gpus.conf",
        user=atf.properties["slurm-user"],
        quiet=True,
    )


def _slurmd_pids(node_name):
    result = atf.run_command(f"pgrep -f 'slurmd -N {node_name}'", quiet=True)
    if result["exit_code"] != 0:
        return []
    return [int(pid) for pid in result["stdout"].splitlines()]


def _stop_slurmd(node_name):
    """Stop slurmd for node_name, escalating to SIGKILL if it won't exit."""

    pids = _slurmd_pids(node_name)
    if not pids:
        return
    atf.run_command(
        f"kill -TERM {' '.join(str(pid) for pid in pids)}", user="root", quiet=True
    )
    for _ in atf.timer():
        pids = _slurmd_pids(node_name)
        if not pids:
            return
    else:
        atf.run_command(
            f"kill -KILL {' '.join(str(pid) for pid in pids)}",
            user="root",
            quiet=True,
        )


def _start_slurmd(node_name):
    """Start slurmd for node_name and wait for it to actually come up."""

    _stop_slurmd(node_name)
    results = atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -N {node_name}",
        user="root",
        quiet=True,
    )
    if results["exit_code"] != 0:
        pytest.fail(
            f"Unable to start slurmd -N {node_name} (rc={results['exit_code']}): {results['stderr']}"
        )
    for _ in atf.timer():
        if _slurmd_pids(node_name):
            return
    else:
        pytest.fail(f"Slurmd -N {node_name} is not running")


def _reload_slurmd(expect_idle=True, expect_reason=None):
    """Restart the test slurmd so it re-reads gres.conf / fake_gpus.conf."""

    slurm_user = atf.properties["slurm-user"]
    log_path = f"{atf.properties['slurm-logs-dir']}/slurmd.{NODE}.log"
    _stop_slurmd(NODE)
    atf.run_command(f": > {log_path}", user="root", fatal=True, quiet=True)
    _start_slurmd(NODE)
    # The log was just truncated, so its startup banner belongs to this reload.
    for _ in atf.timer(fatal=True):
        if "slurmd version" in (
            atf.run_command_output(f"cat {log_path}", user="root", quiet=True) or ""
        ):
            break
    if expect_idle:
        # A DRAIN left by an earlier test outlives the reload, and slurmctld
        # refuses RESUME until a clean registration clears INVALID_REG. RESUME is
        # also invalid on a node that is already IDLE, so drive on state.
        for _ in atf.timer(fatal=True):
            state = str(atf.get_node_parameter(NODE, "state") or "")
            if "DRAIN" not in state and "DOWN" not in state:
                break
            atf.run_command(
                f"scontrol update nodename={NODE} state=RESUME",
                user=slurm_user,
                xfail=True,
                quiet=True,
            )
        assert atf.wait_for_node_state(
            NODE, "IDLE", operator=atf.SetMatch.EQUAL
        ), f"{NODE} did not become IDLE after slurmd reload"
    else:
        assert atf.wait_for_node_state(
            NODE, "DRAIN"
        ), f"{NODE} should be DRAIN after UUID mismatch"
    # Node state alone is not a barrier here: a previous test can leave NODE
    # DRAIN with its own Reason, which the wait above would match immediately.
    if expect_reason is not None:
        for _ in atf.timer(fatal=True):
            if re.search(expect_reason, atf.get_node_parameter(NODE, "reason") or ""):
                break


def test_env_uuid_falls_back_to_index_without_uuid():
    """Verify env_uuid without UUID or AutoDetect UUID uses numeric indices"""

    _remove_fake_gpus()
    _write_conf(
        "gres.conf",
        "Name=gpu File=/dev/tty0 Flags=env_uuid\n"
        "Name=gpu File=/dev/tty1 Flags=env_uuid\n",
    )
    _reload_slurmd()

    output = atf.run_job_output(
        "-n1 --gpus=1 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert re.fullmatch(
        r"\d+", value
    ), f"CUDA_VISIBLE_DEVICES should fall back to an index, got: {value!r}"

    log_path = f"{atf.properties['slurm-logs-dir']}/slurmd.{NODE}.log"
    log_text = atf.run_command_output(f"cat {log_path}", user="root", quiet=True) or ""
    assert "Flags=env_uuid set but no GPU UUID available" in log_text, (
        "slurmd should warn when env_uuid has no UUID or AutoDetect UUID; "
        f"last log excerpt: {log_text[-2000:]!r}"
    )


def test_uuid_without_env_uuid_keeps_numeric_env():
    """Verify UUID alone does not rewrite CUDA_VISIBLE_DEVICES."""

    _remove_fake_gpus()
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 UUID={GPU_1}\n",
    )
    _reload_slurmd()

    output = atf.run_job_output(
        "-n1 --gpus=1 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert re.fullmatch(
        r"\d+", value
    ), f"without Flags=env_uuid, CUDA_VISIBLE_DEVICES should stay an index, got: {value!r}"


def test_uuid_fills_in_when_autodetect_has_no_uuid():
    """Verify UUID is used when AutoDetect (fake_gpus) reports no UUID"""

    _write_conf(
        "fake_gpus.conf",
        "(null)|2|0-1|(null)|/dev/tty0|(null)|nvidia_gpu_env,amd_gpu_env\n"
        "(null)|2|0-1|(null)|/dev/tty1|(null)|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_1}\n",
    )
    _reload_slurmd()

    output = atf.run_job_output(
        "-n1 --gpus=1 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        GPU_0,
        GPU_1,
    ), f"CUDA_VISIBLE_DEVICES should be the configured UUID, got: {value!r}"


def test_uuid_matches_autodetect_uuid():
    """Verify UUID that agrees with AutoDetect is accepted"""

    _write_conf(
        "fake_gpus.conf",
        f"(null)|2|0-1|(null)|/dev/tty0|{GPU_0}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{GPU_1}|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_1}\n",
    )
    _reload_slurmd()

    output = atf.run_job_output(
        "-n1 --gpus=2 printenv CUDA_VISIBLE_DEVICES",
        fatal=True,
    )
    value = output.strip()
    assert value in (
        f"{GPU_0},{GPU_1}",
        f"{GPU_1},{GPU_0}",
    ), f"CUDA_VISIBLE_DEVICES should list the matching UUIDs, got: {value!r}"


def test_configured_uuid_duplicate_is_fatal():
    """Verify a configured cross-device UUID collision is fatal to slurmd.

    A configured UUID that collides with the UUID AutoDetect found on a
    different device is a configuration error. Neither device can be named
    unambiguously, so slurmd refuses to start rather than identifying either
    one by numeric index.
    """

    _write_conf(
        "fake_gpus.conf",
        "(null)|2|0-1|(null)|/dev/tty0|(null)|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{GPU_0}|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        "Name=gpu File=/dev/tty1 Flags=env_uuid\n",
    )

    _stop_slurmd(NODE)
    log_path = f"{atf.properties['slurm-logs-dir']}/slurmd.{NODE}.log"
    atf.run_command(f": > {log_path}", user="root", fatal=True, quiet=True)

    # slurmd forks before loading GRES, so the parent exits 0 either way.
    # The fatal shows up as the daemon not surviving, and in its log.
    atf.run_command(
        f"{atf.properties['slurm-sbin-dir']}/slurmd -N {NODE}",
        user="root",
        quiet=True,
    )
    for _ in atf.timer():
        log_text = (
            atf.run_command_output(f"cat {log_path}", user="root", quiet=True) or ""
        )
        if "Duplicate UUID=" in log_text:
            break
    else:
        pytest.fail(
            "slurmd should log the cross-device UUID collision; "
            f"last log excerpt: {log_text[-2000:]!r}"
        )

    assert "after merging" in log_text, (
        "the collision should be reported as a post-merge duplicate; "
        f"last log excerpt: {log_text[-2000:]!r}"
    )
    # The error is logged before the process unwinds, so the pid outlives it.
    for _ in atf.timer(fatal=True):
        if not _slurmd_pids(NODE):
            break


def test_uuid_partial_mismatch_excludes_only_that_device():
    """Verify only the mismatching UUID is dropped; the other GPU remains."""

    _write_conf(
        "fake_gpus.conf",
        f"(null)|2|0-1|(null)|/dev/tty0|{GPU_0}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{GPU_1}|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_WRONG}\n",
    )
    _reload_slurmd(expect_idle=False, expect_reason=r"\(1 < 2\)")

    log_path = f"{atf.properties['slurm-logs-dir']}/slurmd.{NODE}.log"
    log_text = atf.run_command_output(f"cat {log_path}", user="root", quiet=True) or ""
    assert "mismatching Cores, Links or UUID" in log_text, (
        "slurmd log should report the UUID mismatch; "
        f"last log excerpt: {log_text[-2000:]!r}"
    )
    reason = atf.get_node_parameter(NODE, "reason") or ""
    assert re.search(r"\(1 < 2\)", reason), (
        "the matching GPU should still be reported (1 of 2); " f"got Reason={reason!r}"
    )


def test_uuid_mismatch_excludes_device():
    """Verify UUID that disagrees with AutoDetect excludes the device.

    slurm.conf still advertises gpu:2, so a slurmd that drops both devices
    reports a lower count and the node is drained.
    """

    _write_conf(
        "fake_gpus.conf",
        f"(null)|2|0-1|(null)|/dev/tty0|{GPU_0}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{GPU_1}|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_WRONG}\n"
        f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_WRONG}x\n",
    )
    _reload_slurmd(expect_idle=False, expect_reason=r"\(0 < 2\)")

    log_path = f"{atf.properties['slurm-logs-dir']}/slurmd.{NODE}.log"
    log_text = atf.run_command_output(f"cat {log_path}", user="root", quiet=True) or ""
    assert "mismatching Cores, Links or UUID" in log_text, (
        "slurmd log should report the UUID mismatch; "
        f"last log excerpt: {log_text[-2000:]!r}"
    )
    reason = atf.get_node_parameter(NODE, "reason") or ""
    assert re.search(r"\(0 < 2\)", reason), (
        "both mismatching GPUs should be excluded (0 of 2); " f"got Reason={reason!r}"
    )


def test_uuid_mismatch_above_count_keeps_node_idle():
    """Verify an excluded device only drains the node if the count drops.

    gres.conf(5) makes the drain conditional: on a mismatch "the device is
    excluded and, if that drops the node below its configured GRES count, the
    node's state is set to invalid and the node is drained". With three
    detected devices and gpu:2 configured, excluding one still satisfies the
    count, so the node must stay usable.
    """

    _write_conf(
        "fake_gpus.conf",
        f"(null)|2|0-1|(null)|/dev/tty0|{GPU_0}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty1|{GPU_1}|nvidia_gpu_env,amd_gpu_env\n"
        f"(null)|2|0-1|(null)|/dev/tty2|{GPU_2}|nvidia_gpu_env,amd_gpu_env\n",
    )
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        f"Name=gpu File=/dev/tty1 Flags=env_uuid UUID={GPU_1}\n"
        f"Name=gpu File=/dev/tty2 Flags=env_uuid UUID={GPU_WRONG}\n",
    )
    _reload_slurmd()

    gres = ""
    for _ in atf.timer(fatal=True):
        gres = atf.get_node_parameter(NODE, "gres") or ""
        if gres:
            break
    assert re.search(r"gpu:2\b", gres, re.IGNORECASE), (
        f"{NODE} should still register its configured gpu:2 when only the "
        f"UUID-mismatched device is excluded; got Gres={gres!r}"
    )
    output = atf.run_job_output(
        "-n1 --gpus=2 printenv CUDA_VISIBLE_DEVICES", fatal=True
    )
    assert set(output.strip().split(",")) == {GPU_0, GPU_1}, (
        "the two matching GPUs should still be allocatable; "
        f"got CUDA_VISIBLE_DEVICES={output.strip()!r}"
    )


@pytest.mark.parametrize("var", ["CUDA_VISIBLE_DEVICES", "ROCR_VISIBLE_DEVICES"])
def test_env_uuid_mixed_uuid_falls_back_for_both_vars(var):
    """Verify the no-mixing rule covers both documented variables.

    gres.conf(5) names CUDA_VISIBLE_DEVICES and ROCR_VISIBLE_DEVICES together,
    and says that if any allocated device has no UUID then all devices in that
    variable use numeric indices.
    """

    _remove_fake_gpus()
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        "Name=gpu File=/dev/tty1 Flags=env_uuid\n",
    )
    _reload_slurmd()

    output = atf.run_job_output(f"-n1 --gpus=2 printenv {var}", fatal=True)
    value = output.strip()
    assert re.fullmatch(
        r"\d+,\d+", value
    ), f"{var} should fall back to indices for a mixed allocation, got: {value!r}"


def test_env_uuid_step_scope_uses_uuid_when_step_devices_have_one():
    """Verify the no-mixing rule is scoped to what the step is allocated.

    gres.conf(5) scopes the fallback to the devices "allocated to a job or
    step", so a step given only the UUID-bearing device has no UUID-less
    device of its own and must see the UUID.
    """

    _remove_fake_gpus()
    _write_conf(
        "gres.conf",
        f"Name=gpu File=/dev/tty0 Flags=env_uuid UUID={GPU_0}\n"
        "Name=gpu File=/dev/tty1 Flags=env_uuid\n",
    )
    _reload_slurmd()

    job_script = "step_scope.sh"
    job_output = "step_scope.out"
    atf.make_bash_script(
        job_script,
        "srun --exact -n1 --gpus=1 --mem=0 printenv CUDA_VISIBLE_DEVICES",
    )
    job_id = atf.submit_job_sbatch(
        f"--cpus-per-gpu=1 --gpus=2 -N1 -n1 -t1 -o {job_output} {job_script}",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(job_output, GPU_0)
