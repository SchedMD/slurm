############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test sacct -c reading the jobcomp/mysql records of finished jobs."""

import random

import pytest

import atf

# Job names unique to this run, as the database can keep rows of old runs
token = random.randrange(0, 999999999)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_build_config("HAVE_MYSQL")
    atf.require_accounting(modify=True)

    atf.require_config_parameter("JobCompType", "jobcomp/mysql")
    # The database user may only have access to the accounting database.
    # set_config_parameter() keeps the case of the values.
    slurmdbd_conf = atf.get_config(live=False, source="slurmdbd")
    for name, default in (
        ("Loc", "slurm_acct_db"),
        ("User", atf.properties["slurm-user"]),
        ("Host", None),
        ("Port", None),
        ("Pass", None),
    ):
        atf.set_config_parameter(
            f"JobComp{name}", slurmdbd_conf.get(f"Storage{name}", default)
        )
    atf.require_slurm_running()


def run_job(name, command="true"):
    """Run a batch job until it is done and return its id."""
    job_id = atf.submit_job_sbatch(
        f"-J {name} -o /dev/null --wrap '{command}'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


def get_records(job_ids):
    """Return the sacct -c records of job_ids as {(id, name): [state, user, nodes]}."""
    output = atf.run_command_output(
        f"sacct -c -j {','.join(map(str, job_ids))} -n -P "
        "-o jobid,jobname,state,user,nodelist"
    )
    rows = [line.split("|") for line in output.splitlines()]
    return {(int(row[0]), row[1]): row[2:] for row in rows}


def test_sacct_completion():
    """sacct -c lists finished jobs from the jobcomp database, not pending ones."""
    held_name = f"jobcomp_held_{token}"
    ok_name = f"jobcomp_ok_{token}"
    failed_name = f"jobcomp_failed_{token}"

    held_id = atf.submit_job_sbatch(
        f"-H -J {held_name} -o /dev/null --wrap true", fatal=True
    )
    # Accounting lists the pending job, so sacct -c must not read accounting
    for _ in atf.timer(fatal=True):
        if atf.run_command_output(f"sacct -j {held_id} -n -X -o jobid").strip():
            break
    # Old sacct versions can change the table, so read it after the jobs end
    ok_id = run_job(ok_name)
    failed_id = run_job(failed_name, "exit 3")
    user = atf.properties["test-user"]
    ok_nodes = atf.get_job_parameter(ok_id, "NodeList", fatal=True)
    failed_nodes = atf.get_job_parameter(failed_id, "NodeList", fatal=True)

    for _ in atf.timer():
        records = get_records([ok_id, failed_id, held_id])
        if {(ok_id, ok_name), (failed_id, failed_name)} <= records.keys():
            break

    assert records.get((ok_id, ok_name)) == [
        "COMPLETED",
        user,
        ok_nodes,
    ], f"sacct -c should list job {ok_id} as COMPLETED by {user} on {ok_nodes}"
    assert records.get((failed_id, failed_name)) == [
        "FAILED",
        user,
        failed_nodes,
    ], f"sacct -c should list job {failed_id} as FAILED by {user} on {failed_nodes}"
    assert (
        held_id,
        held_name,
    ) not in records, f"sacct -c should not list pending job {held_id}"
