############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify a synchronous step pending on exhausted reserved ports gets a real step ID at submission.

Ports-busy counterpart to test_116_61's id-at-submit/reuse coverage
(issue 50938). A step can be queued because a node is busy or because the
reserved-port pool is exhausted; test_116_61 covers the first. Here a tiny
MpiParams pool is exhausted by one step so a second step pends on ports
instead, and must show the same real-id-while-pending / reuse-on-launch
behavior.

The pending id is read from srun's own stderr announcement, which is the
step's own evidence and needs no polling window.
"""

import logging
import re
from pathlib import Path

import pytest

import atf

# A 1-task step auto-reserves (max tasks-per-node + 1) = 2 ports, so a 2-port
# pool is exhausted by one step and the next must pend on ports.
port_range_size = 2


@pytest.fixture(scope="module", autouse=True)
def setup(safe_port_range):
    # Daemons are always >= every client command, so requiring srun >= 26.11
    # already implies the daemon serving the step create is new enough too.
    atf.require_version(
        (26, 11),
        "bin/srun",
        reason="Issue 50938: pending-step id-at-submit requires 26.11+",
    )
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    # Under stepmgr the whole job pre-reserves ports at admission (2 *
    # tasks-per-node + 1 = 5 for this job), which both exceeds the 2-port
    # pool below (the job would never be admitted) and, if the pool were
    # widened to fit, would already cover both steps -- never exhausting.
    # This test targets _step_create()'s controller-side ESLURM_PORTS_BUSY
    # path, so pin the job to the non-stepmgr port model.
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")
    # A pool sized so one 1-task step exhausts it outright and a second must
    # pend on ESLURM_PORTS_BUSY rather than ESLURM_NODES_BUSY. The range is
    # probed rather than hardcoded: slurmstepd must be able to bind every
    # port in it on every compute node, or the test's premise collapses.
    # Tuple form: it replaces any existing "ports=<other>" token instead of
    # appending a second one, which the reserved-port parser would ignore.
    lo, hi = safe_port_range
    atf.require_config_parameter_includes("MpiParams", ("ports", f"{lo}-{hi}"))
    # Enough CPUs for the two 1-task steps to run concurrently so CPU
    # contention is never what makes the second step pend -- only the port
    # pool should be exhausted.
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def safe_port_range():
    """Returns (lo, hi) for a free port range outside ip_local_port_range.

    Placing the range outside the ephemeral range and ensuring that none is
    blocked avoids future interferences and ensures current availability.
    """
    range_str = atf.run_command_output(
        "cat /proc/sys/net/ipv4/ip_local_port_range", fatal=True, quiet=True
    )
    ephem_lo, ephem_hi = (int(x) for x in range_str.split())

    # Prefer just above the ephemeral range; fall back to just below.
    candidates = (ephem_hi + 100, ephem_lo - 100 - port_range_size)
    for lo in candidates:
        hi = lo + port_range_size - 1
        if not (1024 <= lo and hi <= 65535 and (hi < ephem_lo or lo > ephem_hi)):
            continue
        if is_port_range_available(lo, hi):
            return lo, hi
        logging.debug(f"[PORT_PICK] Skipping candidate {lo}-{hi}: already blocked")

    pytest.fail(
        f"Cannot find a {port_range_size}-port range outside ephemeral ({ephem_lo}-{ephem_hi}) with no active binders"
    )


def is_port_range_available(lo, hi):
    """Return True if every port in [lo, hi] is available/bindable.

    Any TCP socket on a local port in the range whose state is NOT TIME-WAIT
    will make the reserved-port bind() fail with EADDRINUSE, even with
    SO_REUSEADDR set.
    """
    logging.debug(f"[PORT_MONITOR] Checking ports {lo}-{hi}")

    ss_result = atf.run_command(
        f'ss -tanH "sport >= :{lo} and sport <= :{hi}"',
        timeout=10,
        quiet=True,
    )
    if ss_result["exit_code"] != 0:
        logging.debug(f"[PORT_MONITOR] ss command failed: {ss_result['stderr']}")
        return False

    # ss -tanH columns: State Recv-Q Send-Q Local-Addr:Port Peer-Addr:Port
    for line in ss_result["stdout"].splitlines():
        line = line.strip()
        if not line:
            continue
        state = line.split()[0]
        if state == "TIME-WAIT":
            continue
        logging.debug(f"[PORT_MONITOR] Blocking socket ({state}): {line}")
        return False

    return True


def test_pending_sync_step_shows_real_id_and_reuses_it_ports_busy():
    """A synchronous step that pends because the reserved-port pool is exhausted
    (not a busy node/CPU) shows a real StepId while PENDING and launches
    once the port-holding step frees the pool, reusing (not reassigning)
    that id -- a duplicate-id rejection would otherwise leave it hung."""

    ready = Path("port_ready")
    pend_err = Path("port_pend.err")
    done = Path("port_done")
    out = Path("port_hog_and_pending.out")
    script = Path("port_hog_and_pending.sh")
    atf.make_bash_script(
        script,
        f"""# --exact caps each step to its 1 task's CPU instead of the whole 2-CPU
# allocation, so the second step's CPU stays free and only the reserved-port
# pool -- auto-reserved as (tasks-per-node + 1) = 2 ports from the probed
# MpiParams ports= pool, since resv_port_cnt is left unset and MpiParams
# alone makes resv_ports_present true -- is what makes it pend.
srun --exact -n1 sh -c 'touch "{ready}"; sleep infinity' &
holder=$!
for k in $(seq 1 60); do [ -f '{ready}' ] && break; sleep 0.5; done
[ -f '{ready}' ] || {{ echo PORT_HOLDER_NOT_READY; exit 1; }}
srun --exact -n1 sh -c 'touch "{done}"' 2>'{pend_err}' &
pend=$!
for k in $(seq 1 60); do
    grep -q 'StepId=.* queued' '{pend_err}' 2>/dev/null && break
    sleep 0.5
done
scancel $SLURM_JOB_ID.0
wait "$pend"
echo DONE
""",
    )
    job_id = atf.submit_job_sbatch(
        f"-N1 -n2 -t5 --output={out} --error={out} {script}", fatal=True
    )
    assert job_id != 0, "sbatch should submit the job"
    assert atf.wait_for_job_state(
        job_id, "DONE", timeout=90
    ), f"job {job_id} should reach DONE"
    # Assert the precondition ahead of the generic "script finished" check,
    # which would otherwise report a slow runner as a product failure.
    assert "PORT_HOLDER_NOT_READY" not in atf.run_command_output(
        f"cat {out} 2>/dev/null", quiet=True
    ), "the port-holding step never took the reserved-port pool"
    atf.assert_file_contents(out, "DONE", contains=True)

    # The queued step announced a real, numeric StepId (not TBD), exactly as
    # for a step queued behind a busy node. Poll for content, not just
    # existence: the file is created empty by the shell redirect, so a bare
    # wait_for_file()+cat can read it before srun's write lands.
    atf.assert_file_contents(pend_err, "queued", contains=True)
    pend_err_text = atf.run_command_output(f"cat {pend_err}", fatal=True)
    match = re.search(r"StepId=(\S+) queued", pend_err_text)
    assert match, (
        "the pending step should announce a real StepId while queued, got: "
        f"{pend_err_text!r}"
    )
    assert "TBD" not in match.group(
        1
    ), f"the pending step should show a real id, not TBD, got {match.group(1)!r}"

    # _step_create() assigns an id before it tries the ports, so the
    # placeholder must reuse that id instead of consuming a second one.
    assert match.group(1) == f"{job_id}.1", (
        f"ports-busy queuing skipped a StepId: expected {job_id}.1, got "
        f"{match.group(1)!r}"
    )

    # A re-send rejected as a duplicate is what keeps the step from launching,
    # so assert it before the launch check, which would otherwise always fire
    # first and report the generic symptom instead of this cause. srun renders
    # errnos through slurm_strerror(), so match the message text rather than
    # the ESLURM_DUPLICATE_STEP_ID symbol, against the pending srun's own
    # stderr, which is redirected away from the job output.
    assert "Duplicate job step id" not in pend_err_text, (
        "the re-send carrying the assigned id was rejected as a duplicate "
        f"instead of reusing it, got: {pend_err_text!r}"
    )

    assert atf.wait_for_file(
        done
    ), "the pending step did not launch once the port-holding step freed the pool"
