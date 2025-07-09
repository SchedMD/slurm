############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

import logging
import re


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_upgrades()
    atf.require_accounting()
    atf.require_nodes(5)
    atf.require_slurm_running()


def wait_for_jobs(jobs, state):
    for jid in jobs:
        atf.wait_for_job_state(jid, state, fatal=True)
        atf.repeat_until(
            lambda: atf.run_command_output(
                f"sacct -nPXj {jid} -o State", fatal=True
            ).strip(),
            lambda state_db: state_db == state,
            fatal=True,
        )


def assert_jobs(jobs_old_dbd, jobs_old_ctld):
    def _assert_jobs(jobs_old, jobs_new, daemon):
        assert (
            jobs_old.keys() == jobs_new.keys()
        ), f"Verify we have the same jobs in {daemon}"
        for jid in jobs_old:
            assert (
                jobs_old[jid].keys() == jobs_new[jid].keys()
            ), f"Verify JobID={jid} has the same attributes in {daemon}"
            for param in jobs_old[jid]:
                if param == "RunTime":
                    assert (
                        jobs_old[jid][param] <= jobs_new[jid][param]
                    ), f"Verify that {param} of JobID={jid} is correct"
                elif param == "MinMemoryNode":
                    # TODO: Bug 22789, MinMemoryNode is 0 after restart
                    assert (
                        jobs_new[jid][param] == jobs_old[jid][param]
                        or jobs_new[jid][param] == 0
                    ), f"Parameter {param} of JobID={jid} ({jobs_new[jid][param]}) should be 0 after upgrading {daemon}"
                elif param == "time":
                    for subparam in jobs_old[jid][param]:
                        if subparam == "elapsed":
                            assert (
                                jobs_old[jid][param]["elapsed"]
                                <= jobs_new[jid][param]["elapsed"]
                            ), f"Verify that {param} of JobID={jid} is correct in the {daemon}"
                        else:
                            jobs_old[jid][param][subparam] == jobs_new[jid][param][
                                subparam
                            ]

                else:
                    assert (
                        jobs_old[jid][param] == jobs_new[jid][param]
                    ), f"Parameter {param} of JobID={jid} ({jobs_new[jid][param]}) should have the same value in {daemon} than before upgrading ({jobs_old[jid][param]})"

    _assert_jobs(jobs_old_dbd, atf.get_jobs(dbd=True), "slurmdbd")
    _assert_jobs(jobs_old_ctld, atf.get_jobs(), "slurmctld")


def assert_resv(resv_old_dbd, resv_old_ctld):
    def _assert_resv(resv_old, resv_new, daemon):
        assert (
            resv_old.keys() == resv_new.keys()
        ), f"Verify we have the same reservations in {daemon}"
        for id in resv_old:
            assert (
                resv_old[id].keys() == resv_new[id].keys()
            ), f"Verify Reservation {id} has the same attributes in {daemon}"
            for param in resv_old[id]:
                assert (
                    resv_old[id][param] == resv_new[id][param]
                ), f"Parameter {param} of Reservation {id} ({resv_new[id][param]}) should have the same value in {daemon} than before upgrading ({resv_old[id][param]})"

    _assert_resv(resv_old_ctld, atf.get_reservations(), "slurmctld")

    resv_new_dbd = get_resv_from_dbd()
    assert resv_old_dbd == resv_new_dbd


def assert_qos(old_dbd):
    def _assert_qos(old, new, daemon):
        assert old.keys() == new.keys(), f"Verify we have the same QOSes in {daemon}"
        for id in old:
            assert (
                old[id].keys() == new[id].keys()
            ), f"Verify QOS {id} has the same attributes in {daemon}"
            for param in old[id]:
                assert (
                    old[id][param] == new[id][param]
                ), f"Parameter {param} of QOS {id} ({new[id][param]}) should have the same value in {daemon} than before upgrading ({old[id][param]})"

    _assert_qos(old_dbd, atf.get_qos(), "slurmdbd")


def assert_assoc_ctld(old_assoc_ctld):
    new_assoc_ctld = get_assoc_from_ctld()

    old_lines = old_assoc_ctld.splitlines()
    new_lines = new_assoc_ctld.splitlines()

    assert len(old_lines) == len(new_lines)

    old_users = []
    new_users = []
    for i in range(len(old_lines)):
        old_line = old_lines[i]
        new_line = new_lines[i]

        # Remove known values that may change after restart
        if re.search(r"UsageRaw/Norm/Efctv=\d+\.\d+/\d+\.\d+/\d+\.\d+", old_line):
            logging.info(
                "Removing UsageRaw/Norm/Efctv values as they may change after restart"
            )
            old_line = re.sub(
                r"UsageRaw/Norm/Efctv=\d+\.\d+/\d+\.\d+/\d+\.\d+", "", old_line
            )
            new_line = re.sub(
                r"UsageRaw/Norm/Efctv=\d+\.\d+/\d+\.\d+/\d+\.\d+", "", new_line
            )
        if re.search(
            r"SharesRaw/Norm/Level/Factor=\d+/\d+\.\d+/\d+/\d+\.\d+", old_line
        ):
            logging.info(
                "Removing SharesRaw/Norm/Level/Factor= values as they may change after restart"
            )
            old_line = re.sub(
                r"SharesRaw/Norm/Level/Factor=\d+/\d+\.\d+/\d+/\d+\.\d+", "", old_line
            )
            new_line = re.sub(
                r"SharesRaw/Norm/Level/Factor=\d+/\d+\.\d+/\d+/\d+\.\d+", "", new_line
            )
        if re.search(r"GrpWall=N\(\d+\.\d+\)", old_line):
            logging.info("Removing GrpWall values as they may change after restart")
            old_line = re.sub(r"GrpWall=N\(\d+\.\d+\)", "", old_line)
            new_line = re.sub(r"GrpWall=N\(\d+\.\d+\)", "", new_line)
        if re.search(r"UsageRaw=\d+\.\d+", old_line):
            logging.info("Removing UsageRaw values as they may change after restart")
            old_line = re.sub(r"UsageRaw=\d+\.\d+", "", old_line)
            new_line = re.sub(r"UsageRaw=\d+\.\d+", "", new_line)

        # Save UserName lines as they can be in different order
        if re.search(r"UserName=\S+\(\d+\) DefAccount=", old_line):
            old_users.append(re.sub(r"DefWckey=\(null\)", "DefWckey=", old_line))
            new_users.append(re.sub(r"DefWckey=\(null\)", "DefWckey=", new_line))
            continue

        # TODO: Remove once t22851 is fixed in old versions
        if atf.get_version(slurm_prefix=atf.properties["old-slurm-prefix"]) < (25, 5):
            if re.search(r"ParentAccount=root\(1\) \S+ DefAssoc=No", old_line):
                logging.warning("Removing DefAssoc in account assoc due t22851")
                old_line = re.sub(r"DefAssoc=\S+", "", old_line)
                new_line = re.sub(r"DefAssoc=\S+", "", new_line)

        assert new_line == old_line

    for user in old_users:
        assert user in new_users


# TODO: Use --json once available in i50265
def get_resv_from_dbd():
    return atf.run_command_output("sacctmgr -Pn show reservations", fatal=True)


# TODO: Use --json once available in i50265
def get_assoc_from_ctld():
    return atf.run_command_output("scontrol show assoc", fatal=True)


def test_upgrade():
    """Verify that running cluster can be upgraded without distortion"""

    # Create assocs and wait for them
    atf.run_command(
        "sacctmgr -i create account acct1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        "sacctmgr -i create account acct2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i create user user={atf.properties['test-user']} account=acct1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i create user user={atf.properties['slurm-user']} account=acct1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i create user user={atf.properties['slurm-user']} account=acct2",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.run_command_output("scontrol show assoc"),
        lambda out: re.search("acct1", out) and re.search("acct2", out),
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.run_command_output("scontrol show assoc"),
        lambda out: re.search(atf.properties["slurm-user"], out)
        and re.search(atf.properties["test-user"], out),
        fatal=True,
    )

    # Create QOS and wait for them
    atf.run_command(
        "sacctmgr -i create qos qos1 flags=DenyOnLimit GrpJobs=100 GrpSubmitJobs=10",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        "sacctmgr -i create qos qos2 flags=DenyOnLimit,NoDecay GrpJobs=50 GrpSubmitJobs=5",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.run_command_output("scontrol show assoc"),
        lambda out: re.search("qos1", out) and re.search("qos2", out),
        fatal=True,
    )

    # Create reservations and wait for them
    atf.run_command(
        f"scontrol create reservation reservationname=resv1 nodecnt=1 user={atf.properties['test-user']} start=now duration=100",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.run_command(
        f"scontrol create reservation reservationname=resv2 nodecnt=2 user={atf.properties['slurm-user']} start=tomorrow duration=200",
        fatal=True,
        user=atf.properties["slurm-user"],
    )
    atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv1", "State"),
        lambda state: state == "ACTIVE",
        fatal=True,
    )
    atf.repeat_until(
        lambda: atf.get_reservation_parameter("resv2", "State"),
        lambda state: state == "INACTIVE",
        fatal=True,
    )
    # TODO: Are inactive reservations expected to appear in dbd?
    atf.repeat_until(
        lambda: get_resv_from_dbd(),
        lambda out: re.search("resv1", out),
        fatal=True,
    )

    # Submit jobs and wait for being DONE in ctld and dbd
    # TODO: Submit in reservations
    jobs = []
    jobs.append(atf.submit_job("sbatch", "-N1 --qos=qos1", "hostname", fatal=True))
    jobs.append(atf.submit_job("sbatch", "-N2 --qos=qos2", "hostname", fatal=True))
    wait_for_jobs(jobs, "COMPLETED")

    # Submit jobs and wait for being FAIL in ctld and dbd
    jobs = []
    jobs.append(
        atf.submit_job(
            "sbatch",
            "-N1 --qos=qos1",
            "false",
            fatal=True,
            user=atf.properties["test-user"],
        )
    )
    jobs.append(atf.submit_job("sbatch", "-N2 --qos=qos2", "false", fatal=True))
    wait_for_jobs(jobs, "FAILED")

    # Submit jobs and wait for being RUNNING
    jobs = []
    jobs.append(
        atf.submit_job(
            "sbatch",
            "-N1 --qos=qos1",
            "sleep 300",
            fatal=True,
            user=atf.properties["test-user"],
        )
    )
    jobs.append(atf.submit_job("sbatch", "-N2 --qos=qos2", "sleep 300", fatal=True))
    wait_for_jobs(jobs, "RUNNING")

    # Save jobs and reservations from ctld and dbd before upgrades
    jobs_old_ctld = atf.get_jobs()
    jobs_old_dbd = atf.get_jobs(dbd=True)
    resv_old_ctld = atf.get_reservations()
    resv_old_dbd = get_resv_from_dbd()
    qos_old_dbd = atf.get_qos()

    assoc_old_ctld = get_assoc_from_ctld()

    logging.info("Testing upgrading slurmdbd")
    atf.upgrade_component("sbin/slurmdbd")
    assert_jobs(jobs_old_dbd, jobs_old_ctld)
    assert_resv(resv_old_dbd, resv_old_ctld)
    assert_qos(qos_old_dbd)
    assert_assoc_ctld(assoc_old_ctld)

    logging.info("Testing upgrading slurmctld")
    atf.upgrade_component("sbin/slurmctld")
    assert_jobs(jobs_old_dbd, jobs_old_ctld)
    assert_resv(resv_old_dbd, resv_old_ctld)
    assert_qos(qos_old_dbd)
    assert_assoc_ctld(assoc_old_ctld)

    logging.info("Testing upgrading slurmd")
    atf.upgrade_component("sbin/slurmd")
    assert_jobs(jobs_old_dbd, jobs_old_ctld)
    assert_resv(resv_old_dbd, resv_old_ctld)
    assert_qos(qos_old_dbd)
    assert_assoc_ctld(assoc_old_ctld)

    logging.info("Testing upgrading slurmstepd")
    atf.upgrade_component("sbin/slurmstepd")
    assert_jobs(jobs_old_dbd, jobs_old_ctld)
    assert_resv(resv_old_dbd, resv_old_ctld)
    assert_qos(qos_old_dbd)
    assert_assoc_ctld(assoc_old_ctld)
