############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test when the step-complete RPC re-sends a job's TRES string.

The step-complete RPC re-sends the job's TRES string only after folding the
steps' energy into it. With JobAcctGatherType unset the job's energy is never
known, so the string must reach slurmdbd only on the job-complete RPC. With a
JobAcctGather plugin loaded it is sent again. Ticket 25886: the guard that
gates the re-send read the node slot instead of the energy slot, so with the
plugin unset the step-complete RPC re-sent the string and slurmdbd updated
the job row a second time.
"""

import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter_includes("DebugFlags", "DB_STEP", source="slurmdbd")
    atf.require_config_parameter("DebugLevel", "debug", source="slurmdbd")
    atf.require_slurm_running()


@pytest.mark.parametrize(
    "job_acct_gather_type,",
    [
        pytest.param(
            None,
            marks=pytest.mark.xfail(
                (
                    (atf.get_version("sbin/slurmd") < (26, 11))
                    if atf.config_parameter_includes(
                        "SlurmctldParameters", "enable_stepmgr", live=False
                    )
                    else (atf.get_version("sbin/slurmctld") < (26, 11))
                ),
                reason="Ticket 25886: tres_alloc_cnt[] was indexed with the database TRES id",
            ),
        ),
        "jobacct_gather/linux",
    ],
    ids=["not resent", "resent"],
)
def test_when_job_tres_alloc_is_sent(job_acct_gather_type):
    """Verify the job TRES string is re-sent only with JobAcctGatherType set"""

    slurmdbd_log = atf.get_config_parameter(
        "LogFile", live=False, quiet=True, source="slurmdbd"
    )
    if not slurmdbd_log:
        pytest.skip(
            "Ticket 25886: LogFile not configured in slurmdbd.conf; "
            "cannot scan the queries slurmdbd logs"
        )

    # Unset leaves the job's energy unknown, so the string is not re-sent. The
    # linux plugin gathers energy, so the step-complete RPC sends it again.
    atf.set_config_parameter("JobAcctGatherType", job_acct_gather_type)

    # remember where the log ends so the scan only sees queries from this test
    log_line_start = int(
        atf.run_command_output(
            f"wc -l < {slurmdbd_log}", user="root", quiet=True, fatal=True
        ).strip()
    )

    # The job's srun step, cancelled while running, completes after the job
    job_id = atf.submit_job_srun("-n1 sleep infinity", background=True, fatal=True)
    atf.wait_for_step(job_id, 0, fatal=True)
    # The step list below comes from sacct, so make sure slurmdbd already has
    # the srun step before the job is cancelled
    atf.wait_for_step_accounted(job_id, 0, fatal=True)

    atf.run_command(f"scancel {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # slurmdbd's step table ids: "<job>.0" is 0, ".batch" -5, ".extern" -4,
    # ".interactive" -6. Wait until the DB_STEP log holds the step-complete
    # update for each step sacct lists; as_mysql_step_complete() logs that
    # line right before any job-row update it issues for the same step.
    special = {"batch": -5, "extern": -4, "interactive": -6}
    step_ids = []
    for row in atf.run_command_output(
        f"sacct -j {job_id} -n -P -o JobID", fatal=True
    ).split():
        if "." in row:
            suffix = row.split(".", 1)[1]
            step_ids.append(special.get(suffix, suffix))
    assert step_ids, f"sacct lists no steps for job {job_id}"

    def _read_log():
        return atf.run_command_output(
            f"tail -n +{log_line_start + 1} {slurmdbd_log}",
            user="root",
            quiet=True,
            fatal=True,
        )

    for _ in atf.timer(fatal=True):
        log_text = _read_log()
        if all(
            re.search(
                rf'update "[^"]+_step_table" set time_end=.* and id_step={sid} and',
                log_text,
            )
            for sid in step_ids
        ):
            break

    # The job-row update, if any, is logged a few ms after the step line we
    # waited for. Re-read after each poll interval until the log is stable.
    # A 1 s interval keeps this settle cheap; the default interval would add
    # several seconds to every run.
    first = True
    for _ in atf.timer(timeout=10, poll_interval=1):
        if first:
            first = False
            continue
        again = _read_log()
        if again == log_text:
            break
        log_text = again

    # as_mysql_step_complete()'s "set the energy for the entire job" update,
    # issued only when the step-complete RPC carried the job's TRES string
    updates = re.findall(r'update "[^"]+_job_table" set tres_alloc=', log_text)

    expected_cnt = 0
    invert_assert_msg = ""
    if job_acct_gather_type is not None:
        expected_cnt = 1
        invert_assert_msg = "not "

    assert (
        len(updates) == expected_cnt
    ), f"job row was {invert_assert_msg}updated again from the step-complete path: {updates}"
