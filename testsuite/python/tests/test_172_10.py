############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct reading jobcomp/filetxt records and the SIGUSR2 log reopen."""

import pytest

import atf

jobcomp_log = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global jobcomp_log

    # slurmctld needs an absolute path
    jobcomp_log = atf.module_tmp_path / "jobcomp.log"

    atf.require_config_parameter("JobCompType", "jobcomp/filetxt")
    atf.require_config_parameter("JobCompLoc", str(jobcomp_log))
    atf.require_slurm_running()


def run_job(name, command="true"):
    """Run a batch job until it is done and return its id."""
    job_id = atf.submit_job_sbatch(
        f"-J {name} -o /dev/null --wrap '{command}'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


def get_records(job_ids, source="-c"):
    """Return the sacct records of job_ids as {job_id: [name, state, exit code]}."""
    # sacct opens the jobcomp file for writing, so run it as SlurmUser
    output = atf.run_command_output(
        f"sacct {source} -j {','.join(map(str, job_ids))} -n -P "
        "-o jobid,jobname,state,exitcode",
        user=atf.properties["slurm-user"],
    )
    rows = [line.split("|") for line in output.splitlines()]
    return {int(row[0]): row[1:] for row in rows}


def wait_for_records(job_ids):
    """Poll sacct -c until all job_ids have a record, then return the records."""
    for _ in atf.timer():
        records = get_records(job_ids)
        if set(job_ids) <= records.keys():
            break

    assert set(job_ids) <= records.keys(), f"sacct -c should list jobs {job_ids}"
    return records


def test_sacct_completion():
    """sacct -c reads the job records from the jobcomp file."""
    ok_id = run_job("jobcomp_ok")
    failed_id = run_job("jobcomp_failed", "exit 3")

    records = wait_for_records([ok_id, failed_id])
    assert records[ok_id] == [
        "jobcomp_ok",
        "COMPLETED",
        "0:0",
    ], f"Job {ok_id} should be COMPLETED with exit code 0:0"
    assert records[failed_id] == [
        "jobcomp_failed",
        "FAILED",
        "3:0",
    ], f"Job {failed_id} should be FAILED with exit code 3:0"


def test_sacct_file():
    """sacct -f reads the named file instead of JobCompLoc and implies -c."""
    snapshot = atf.module_tmp_path / "snapshot.log"

    old_id = run_job("jobcomp_old")
    wait_for_records([old_id])
    # sacct -f also opens the copy for writing
    atf.run_command(
        f"cp {jobcomp_log} {snapshot}", user=atf.properties["slurm-user"], fatal=True
    )
    new_id = run_job("jobcomp_new")
    wait_for_records([old_id, new_id])

    assert get_records([old_id, new_id], f"-f {snapshot}").keys() == {
        old_id
    }, f"sacct -f should list only job {old_id}"


@pytest.mark.parametrize("pre_create", [False, True])
def test_sigusr2_reopen(pre_create):
    """After JobCompLoc is moved, SIGUSR2 makes slurmctld log to a new file."""
    rotated = atf.module_tmp_path / f"rotated_{pre_create}.log"

    old_id = run_job("jobcomp_old")
    wait_for_records([old_id])
    atf.run_command(
        f"mv {jobcomp_log} {rotated}", user=atf.properties["slurm-user"], fatal=True
    )
    if pre_create:
        # Same as the logrotate "create 640 slurm root" option
        atf.run_command(
            f"install -m 640 /dev/null {jobcomp_log}",
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    # This also signals slurmscriptd, which ignores SIGUSR2
    pids = atf.pids_from_exe(f"{atf.properties['slurm-sbin-dir']}/slurmctld")
    atf.run_command(f"kill -USR2 {' '.join(map(str, pids))}", user="root", fatal=True)
    if pre_create:
        # The file already exists, so wait until slurmctld has it open
        fds = " ".join(f"/proc/{pid}/fd/*" for pid in pids)
        for _ in atf.timer():
            open_files = atf.run_command_output(f"readlink {fds}", user="root")
            is_open = str(jobcomp_log) in open_files.splitlines()
            if is_open:
                break
        assert is_open, f"SIGUSR2 should make slurmctld open {jobcomp_log}"
    else:
        assert atf.wait_for_file(
            str(jobcomp_log)
        ), f"SIGUSR2 should create a new {jobcomp_log}"

    new_id = run_job("jobcomp_new")
    wait_for_records([new_id])
    assert get_records([old_id, new_id]).keys() == {
        new_id
    }, f"The new file should have only job {new_id}"
    assert get_records([old_id, new_id], f"-f {rotated}").keys() == {
        old_id
    }, f"The moved file should have only job {old_id}"
