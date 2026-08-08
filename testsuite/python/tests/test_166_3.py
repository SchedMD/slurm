############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify swait's listener rejects non-TLS connections when TLS is required.

Companion to test_166_1, which covers the slurmctld, slurmd and srun
step-launch listeners. swait needs its own module because reaching its
listener requires a stepmgr-enabled cluster and a running step, neither of
which test_166_1 asks for.

swait binds an ephemeral port from SrunPortRange and only ever tells stepmgr
about it, in the REQUEST_STEPS_DRAINED_SUBSCRIBE it sends on startup, so
there is no client interface that reports it. The address is recovered here
from swait's own --verbose output instead.

Only the reject case is probed directly. swait presents an ephemerally
generated self-signed certificate that it shares with stepmgr over the
already-established TLS connection, so an outside client has no way to trust
it and a valid-TLS control probe is not possible. The control is swait's own
completion: it exits 0 once stepmgr reaches its listener and reports the
steps drained, which a listener refusing every connection could not do.
"""

import os
import re

import pytest

import atf

REASON = "Issue 51066: conmgr client-listener reject added in 26.05.4"
# swait logs this at --verbose right before it starts listening.
LISTENING_RE = re.compile(r"listening on (\S+):(\d+)")
# Logged only after the subscribe RPC to stepmgr succeeds, so waiting for it
# too distinguishes a slow startup from a swait that never subscribed.
SUBSCRIBED_RE = re.compile(r"subscribed to stepmgr")
# Long enough that swait is still listening when the probe connects.
LONG_STEP_SECS = 60
# Short enough that swait sees the step drain within the command timeout.
SHORT_STEP_SECS = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_tls()
    atf.require_tool("gcc")
    atf.require_tool("swait")
    # Must follow require_tool(): probing swait's version on a cluster without
    # it is fatal, and a module-level skipif would run before that check.
    atf.require_version((26, 5, 4), component="bin/swait", reason=REASON)
    atf.require_nodes(1)
    atf.require_config_parameter_includes("SlurmctldParameters", "enable_stepmgr")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def non_tls_conf(module_setup):
    """Path to a slurm.conf that includes the real config but disables TLS.

    Absolute because the tests consuming it run in their own directory.
    """
    real_conf = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    path = "non_tls_slurm.conf"
    atf.run_command(
        f"printf 'include {real_conf}\\nTLSType=tls/none\\n' > {path}",
        fatal=True,
    )
    return os.path.abspath(path)


@pytest.fixture(scope="module")
def probe(module_setup):
    """Compile the non-TLS probe helper and return its absolute path."""
    source = f"{atf.properties['testsuite_scripts_dir']}/tls_reject_probe.c"
    dest = "tls_reject_probe"
    atf.compile_against_libslurm(source, dest, full=True, fatal=True)
    return os.path.abspath(dest)


@pytest.fixture
def swait_listener():
    """Start swait for a job and report the address it listens on.

    Yields a callable taking (job_id, stepmgr) so swait can be started once
    the job exists; it is terminated when the test finishes, including when
    the test fails before reaching the end.
    """
    processes = []

    def _start(job_id, stepmgr):
        log = "swait.out"
        process = atf.run_command(
            f"swait -v {job_id} > {log} 2>&1",
            env_vars=f"SLURM_STEPMGR={stepmgr}",
            background=True,
        )["process"]
        processes.append(process)

        target = None
        for _ in atf.timer(fatal=True):
            # Not fatal: the log does not exist until swait first writes to it.
            log_output = atf.run_command_output(f"cat {log}", quiet=True)
            match = LISTENING_RE.search(log_output)
            if match and SUBSCRIBED_RE.search(log_output):
                target = f"{match[1]} {match[2]}"
                break
            assert (
                process.poll() is None
            ), f"swait exited before it subscribed to stepmgr:\n{log_output}"
        return target

    yield _start

    for process in processes:
        process.terminate()


def _job_with_step(name, step_secs):
    """Submit a job running one step and return (job_id, stepmgr host)."""
    job_id = atf.submit_job_sbatch(
        f"-N1 --time=5:00 --job-name={name} --wrap 'srun -n1 sleep {step_secs}'",
        fatal=True,
    )
    atf.wait_for_step(job_id, 0, fatal=True)
    return job_id, atf.get_job_parameter(job_id, "BatchHost", fatal=True)


def test_swait_rejects_non_tls(non_tls_conf, probe, swait_listener):
    """A non-TLS client is rejected by swait's listener."""
    job_id, stepmgr = _job_with_step("test_166_3", LONG_STEP_SECS)
    target = swait_listener(job_id, stepmgr)

    result = atf.run_command(
        f"{probe} {target}",
        env_vars=f"SLURM_CONF={non_tls_conf}",
        fatal=True,
    )

    output = result["stdout"] + result["stderr"]
    assert (
        "PROBE_RESULT=TLS_REQUIRED" in output
    ), f"Expected swait to reject the non-TLS probe, got:\n{output}"


def test_swait_completes_with_tls():
    """Control: stepmgr still reaches swait's listener over TLS.

    Requiring TLS on the listener does not stop its real peer from using it:
    swait waits for steps to drain and stepmgr connects to tell it they have.
    Both are asserted from the log, since swait also exits 0 when the steps
    drained before it subscribed, having never opened the listener at all.
    """
    job_id, stepmgr = _job_with_step("test_166_3_control", SHORT_STEP_SECS)

    # Longer than the default: this blocks until the step ends by design.
    result = atf.run_command(
        f"swait -v {job_id}",
        env_vars=f"SLURM_STEPMGR={stepmgr}",
        timeout=SHORT_STEP_SECS + atf.default_command_timeout,
    )

    assert (
        result["exit_code"] == 0
    ), f"Expected swait to be notified over TLS and exit 0, got:\n{result}"

    output = result["stdout"] + result["stderr"]
    assert (
        "waiting for steps to drain" in output
    ), f"Expected swait to subscribe and open its listener, got:\n{output}"
    assert (
        "received SRUN_STEPS_DRAINED" in output
    ), f"Expected stepmgr to reach swait's listener over TLS, got:\n{output}"
