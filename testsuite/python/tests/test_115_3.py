############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test 115.3: sreport cluster reports with partition associations.

Tests that sreport cluster reports correctly account for users whose
associations include partitions (Ticket 23099).

A user's partition-based associations have their usage rolled up into the
user's non-partition (partition='') association through the lineage, so a
user that has both must be counted exactly once, while a user whose only
associations are partition-based must still be reported.

This test covers the six interesting association layouts:
  user_part_only:  only partition-based associations (dropped pre-fix)
  user_both:       partition='' and partition-based associations in
                   account1 (must not be double counted), but only a
                   partition-based association in account2 (must still
                   be reported for account2)
  user_plain:      only a partition='' association (control)
  user_two_parts:  two partition-based associations in account1 and no
                   partition='' one (must be one row per report, summed)
  user_leak:       a partition-based association in account1 whose
                   partition is named like account2 (must not show up
                   when filtering on account2)
  user_stale:      partition='' and partition-based associations in
                   account3; the partition-based one is deleted and
                   account3 is re-parented afterwards, so the deleted
                   association keeps a stale lineage that the partition=''
                   one no longer rolls up (its usage must still be counted)

Seeds its own cluster with 2008 usage and rolls that window up for every
cluster in the accounting database; run it against a throwaway DB.
"""

import datetime
import os
import re

import pytest

import atf

# Period Boundaries
# Sat Mar 1 00:00:00 2008
period_start_datetime = datetime.datetime(2008, 3, 1, 0, 0, 0)
period_start_epoch = int(period_start_datetime.timestamp())
period_start_string = period_start_datetime.strftime("%Y-%m-%dT%H:%M:%S")
# Sun Mar 2 00:00:00 2008
period_end_datetime = datetime.datetime(2008, 3, 2, 0, 0, 0)
period_end_epoch = int(period_end_datetime.timestamp())
period_end_string = period_end_datetime.strftime("%Y-%m-%dT%H:%M:%S")

# Identities
uid = os.geteuid()
gid = os.getegid()

# Entities
cluster1 = "test115_3_cluster"
account1 = "test115_3_acct1"
account2 = "test115_3_acct2"
account3 = "test115_3_acct3"
account4 = "test115_3_acct4"
partition1 = "test115_3_part"
partition2 = "test115_3_part2"
user_part_only = "test115_3_puser"
user_both = "test115_3_buser"
user_plain = "test115_3_nuser"
user_two_parts = "test115_3_tuser"
user_leak = "test115_3_luser"
user_stale = "test115_3_suser"

# Nodes
node_list = f"{cluster1}_node[0-1]"
cluster_cpus = 32

# Jobs: all run within the first hour of the period on 2 cpus each
job_cpus = 2

# job1: user_part_only, account1, partition association
job1_start_epoch = period_start_epoch
job1_duration = 1200
job1_end_epoch = job1_start_epoch + job1_duration
job1_usage = job1_duration * job_cpus

# job2: user_both, account1, partition association
job2_start_epoch = period_start_epoch
job2_duration = 600
job2_end_epoch = job2_start_epoch + job2_duration
job2_usage = job2_duration * job_cpus

# job3: user_both, account1, non-partition association
job3_start_epoch = period_start_epoch + job2_duration
job3_duration = 900
job3_end_epoch = job3_start_epoch + job3_duration
job3_usage = job3_duration * job_cpus

# job4: user_plain, account1, non-partition association
job4_start_epoch = period_start_epoch
job4_duration = 300
job4_end_epoch = job4_start_epoch + job4_duration
job4_usage = job4_duration * job_cpus

# job5: user_both, account2, partition association
job5_start_epoch = period_start_epoch
job5_duration = 450
job5_end_epoch = job5_start_epoch + job5_duration
job5_usage = job5_duration * job_cpus

# job6: user_two_parts, account1, partition1 association
job6_start_epoch = period_start_epoch
job6_duration = 660
job6_end_epoch = job6_start_epoch + job6_duration
job6_usage = job6_duration * job_cpus

# job7: user_two_parts, account1, partition2 association
job7_start_epoch = period_start_epoch
job7_duration = 240
job7_end_epoch = job7_start_epoch + job7_duration
job7_usage = job7_duration * job_cpus

# job8: user_leak, account1, association with the partition named account2
job8_start_epoch = period_start_epoch
job8_duration = 180
job8_end_epoch = job8_start_epoch + job8_duration
job8_usage = job8_duration * job_cpus

# job9: user_stale, account3, non-partition association
job9_start_epoch = period_start_epoch
job9_duration = 420
job9_end_epoch = job9_start_epoch + job9_duration
job9_usage = job9_duration * job_cpus

# job10: user_stale, account3, partition association (deleted afterwards)
job10_start_epoch = period_start_epoch
job10_duration = 360
job10_end_epoch = job10_start_epoch + job10_duration
job10_usage = job10_duration * job_cpus

# Expected report usage (in seconds)
# user_both's partition association usage (job2) is included in their
# non-partition association usage through the lineage and must only be
# counted once.
user_part_only_usage = job1_usage
user_both_account1_usage = job2_usage + job3_usage
user_both_account2_usage = job5_usage
user_plain_usage = job4_usage
user_two_parts_usage = job6_usage + job7_usage
user_leak_usage = job8_usage
user_stale_usage = job9_usage + job10_usage
account1_usage = (
    user_part_only_usage
    + user_both_account1_usage
    + user_plain_usage
    + user_two_parts_usage
    + user_leak_usage
)
account2_usage = user_both_account2_usage
# account3 is re-parented after its deleted partition association got its
# usage, so only the root row (lineage "/") still includes all of it. The
# contract is user_stale_usage on the user row and job9_usage on the
# account3 row, which is the second NOTE under "cluster
# AccountUtilizationByUser" in sreport.1: an account can be smaller than
# the sum of its user rows.
root_usage = account1_usage + account2_usage + user_stale_usage

# Association id dictionary keyed on (user, account, partition)
# (populated in the create_entities fixture)
assoc_id = {}


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    # sreport -V exits before parsing options unless accounting_storage/slurmdbd
    # is configured, so probe sacctmgr, built from the same libslurmdb, instead.
    # Lower this gate to the first maintenance releases that carry the fix
    # once the backports land.
    atf.require_version(
        (26, 11),
        "bin/sacctmgr",
        reason="Ticket 23099: partition association usage fixed in 26.11",
    )
    atf.require_accounting(modify=True)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def create_entities():
    """Populate accounting database with the clusters, accounts and users"""

    # Clean up after an aborted earlier run; the removal is expected to fail
    # when nothing was left behind. Removing a cluster cascades a delete
    # across every per-cluster usage table, which can exceed the default
    # command timeout on a loaded DB.
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster1}",
        user=atf.properties["slurm-user"],
        timeout=120,
        quiet=True,
    )
    atf.run_command(
        f"sacctmgr -i add cluster {cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add account {account1},{account2},{account3},{account4} cluster={cluster1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_part_only gets a single partition-based association
    atf.run_command(
        f"sacctmgr -i add user user={user_part_only} cluster={cluster1} account={account1} partition={partition1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_both gets a non-partition and a partition-based association in
    # account1 and only a partition-based association in account2
    atf.run_command(
        f"sacctmgr -i add user user={user_both} cluster={cluster1} account={account1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user user={user_both} cluster={cluster1} account={account1},{account2} partition={partition1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_plain gets a single non-partition association
    atf.run_command(
        f"sacctmgr -i add user user={user_plain} cluster={cluster1} account={account1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_two_parts gets two partition-based associations in account1
    atf.run_command(
        f"sacctmgr -i add user user={user_two_parts} cluster={cluster1} account={account1} partition={partition1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user user={user_two_parts} cluster={cluster1} account={account1} partition={partition2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_leak gets a partition-based association in account1 whose
    # partition is named like account2
    atf.run_command(
        f"sacctmgr -i add user user={user_leak} cluster={cluster1} account={account1} partition={account2}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # user_stale gets a non-partition and a partition-based association in
    # account3
    atf.run_command(
        f"sacctmgr -i add user user={user_stale} cluster={cluster1} account={account3}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i add user user={user_stale} cluster={cluster1} account={account3} partition={partition1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Populate assoc_id dictionary keyed on (user, account, partition)
    output = atf.run_command_output(
        f'sacctmgr -n -P list assoc cluster={cluster1} format="user,account,partition,id"',
        fatal=True,
    )
    for line in output.splitlines():
        if match := re.search(r"^([^|]*)\|([^|]*)\|([^|]*)\|(\d+)$", line):
            user, account, partition, aid = match.group(1, 2, 3, 4)
            assoc_id[(user, account, partition)] = aid

    # Verify that the associations we need were all created
    for assoc_key in [
        (user_part_only, account1, partition1),
        (user_both, account1, ""),
        (user_both, account1, partition1),
        (user_both, account2, partition1),
        (user_plain, account1, ""),
        (user_two_parts, account1, partition1),
        (user_two_parts, account1, partition2),
        (user_leak, account1, account2),
        (user_stale, account3, ""),
        (user_stale, account3, partition1),
    ]:
        if assoc_key not in assoc_id:
            pytest.fail(f"Association {assoc_key} was not created")

    # The bug only manifests for a user with no partition='' association, so
    # make sure those anchors were not created behind our back
    for assoc_key in [
        (user_part_only, account1, ""),
        (user_both, account2, ""),
        (user_two_parts, account1, ""),
        (user_leak, account1, ""),
    ]:
        if assoc_key in assoc_id:
            pytest.fail(
                f"Association {assoc_key} must not exist for this test to be "
                "meaningful"
            )

    yield

    # Removing a cluster cascades a delete across every per-cluster usage
    # table, which can exceed the default command timeout on a loaded DB
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster1}",
        user=atf.properties["slurm-user"],
        timeout=120,
        fatal=True,
    )
    # Users and accounts are not cluster-scoped, so removing the cluster
    # leaves them behind
    atf.run_command(
        f"sacctmgr -i remove user {user_part_only},{user_both},{user_plain},{user_two_parts},{user_leak},{user_stale}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i remove account {account1},{account2},{account3},{account4}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def archive_load(create_entities):
    """Populate the accounting database with usage for the associations"""

    job_values = [
        # (jobid, assoc_key, partition, start, end)
        (
            65536,
            (user_part_only, account1, partition1),
            partition1,
            job1_start_epoch,
            job1_end_epoch,
        ),
        (
            65537,
            (user_both, account1, partition1),
            partition1,
            job2_start_epoch,
            job2_end_epoch,
        ),
        (
            65538,
            (user_both, account1, ""),
            partition1,
            job3_start_epoch,
            job3_end_epoch,
        ),
        (
            65539,
            (user_plain, account1, ""),
            partition1,
            job4_start_epoch,
            job4_end_epoch,
        ),
        (
            65540,
            (user_both, account2, partition1),
            partition1,
            job5_start_epoch,
            job5_end_epoch,
        ),
        (
            65541,
            (user_two_parts, account1, partition1),
            partition1,
            job6_start_epoch,
            job6_end_epoch,
        ),
        (
            65542,
            (user_two_parts, account1, partition2),
            partition2,
            job7_start_epoch,
            job7_end_epoch,
        ),
        (
            65543,
            (user_leak, account1, account2),
            account2,
            job8_start_epoch,
            job8_end_epoch,
        ),
        (
            65544,
            (user_stale, account3, ""),
            partition1,
            job9_start_epoch,
            job9_end_epoch,
        ),
        (
            65545,
            (user_stale, account3, partition1),
            partition1,
            job10_start_epoch,
            job10_end_epoch,
        ),
    ]

    sql_input_path = str(atf.module_tmp_path / "usage.sql")
    with open(sql_input_path, "w") as f:
        # Add the cluster processor count for the period
        f.write(
            f"insert into cluster_event_table (node_name, cluster, tres, period_start, period_end, reason, cluster_nodes) values ('', '{cluster1}', '1={cluster_cpus}', {period_start_epoch}, {period_end_epoch}, 'Cluster processor count', '{node_list}')"
        )
        f.write(
            " on duplicate key update period_start=VALUES(period_start), period_end=VALUES(period_end);\n"
        )
        # Add a completed job for each association of interest
        f.write(
            "insert into job_table (jobid, associd, wckey, wckeyid, uid, gid, `partition`, blockid, cluster, account, eligible, submit, start, end, suspended, name, state, comp_code, priority, req_cpus, tres_alloc, nodelist, kill_requid, qos, deleted) values"
        )
        job_records = []
        for jobid, assoc_key, partition, start, end in job_values:
            account = assoc_key[1]
            job_records.append(
                f" ('{jobid}', '{assoc_id[assoc_key]}', '', '0', '{uid}', '{gid}', '{partition}', '', '{cluster1}', '{account}', {start}, {start}, {start}, {end}, '0', 'job_{jobid}', '3', '0', '{job_cpus}', {job_cpus}, '1={job_cpus}', '{cluster1}_node0', '0', '0', '0')"
            )
        f.write(",".join(job_records))
        f.write(
            " on duplicate key update id=LAST_INSERT_ID(id), eligible=VALUES(eligible), submit=VALUES(submit), start=VALUES(start), end=VALUES(end), associd=VALUES(associd), tres_alloc=VALUES(tres_alloc), wckey=VALUES(wckey), wckeyid=VALUES(wckeyid);\n"
        )

    # Perform archive load
    atf.run_command(
        f"sacctmgr -i -n archive load {sql_input_path}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Use sacct to verify the jobs loaded
    output = atf.run_command_output(
        f"sacct -n -P -M {cluster1} --format=jobid,associd --start={period_start_string} --end={period_end_string}",
        fatal=True,
    )
    for jobid, assoc_key, *_ in job_values:
        if not re.search(rf"^{jobid}\|{assoc_id[assoc_key]}$", output, re.MULTILINE):
            pytest.fail(f"Job {jobid} was not loaded correctly")

    # Use sacctmgr to roll up the time period
    atf.run_command(
        f"sacctmgr -i roll {period_start_string} {period_end_string}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


@pytest.fixture(scope="module")
def stale_lineage(archive_load):
    """Leave user_stale's deleted partition association with a stale lineage

    Removing an association with jobs only marks it deleted, and moving an
    account rewrites the lineage of its live descendants only, so afterwards
    the partition='' association no longer rolls up the deleted one."""

    atf.run_command(
        f"sacctmgr -i delete user user={user_stale} cluster={cluster1} account={account3} partition={partition1}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i modify account {account3} cluster={cluster1} set parent={account4}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # Make sure the scenario is the one we want: the live association moved
    # under account4 while the deleted one kept its old lineage
    output = atf.run_command_output(
        f"sacctmgr -n -P list assoc withdeleted cluster={cluster1} user={user_stale} format=partition,lineage",
        fatal=True,
    )
    lineages = dict(line.split("|", 1) for line in output.splitlines() if "|" in line)
    if f"/{account4}/{account3}/" not in lineages.get("", ""):
        pytest.fail(f"{user_stale}'s live association was not moved: {output}")
    if partition1 not in lineages:
        pytest.fail(
            f"{user_stale}'s deleted association was purged instead of being "
            f"marked deleted: {output}"
        )
    if (f"/{account3}/" not in lineages[partition1]) or (
        f"/{account4}/" in lineages[partition1]
    ):
        pytest.fail(
            f"{user_stale}'s deleted association did not keep its lineage: {output}"
        )


def _assert_report_line(output, pattern, command):
    assert (
        re.search(pattern, output, re.MULTILINE) is not None
    ), f'Command output for "{command}" did not match expected pattern "{pattern}"'


@pytest.mark.usefixtures("create_entities", "archive_load", "stale_lineage")
class TestUserUtilizationByAccount:
    """Test cluster UserUtilizationByAccount with partition associations"""

    command = f"sreport --local cluster UserUtilizationByAccount cluster='{cluster1}' start={period_start_string} end={period_end_string} -t Seconds -P -n format=Login,Account,Used"

    def test_partition_only_user(self):
        """A user with only partition associations must be reported"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_part_only}\|{account1}\|{user_part_only_usage}$",
            self.command,
        )

    def test_user_with_both_associations_not_double_counted(self):
        """Partition association usage rolled up into the non-partition
        association must only be counted once"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_both}\|{account1}\|{user_both_account1_usage}$",
            self.command,
        )

    def test_partition_only_association_in_second_account(self):
        """A user with a non-partition association in one account and only a
        partition association in another account must be reported for both"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_both}\|{account2}\|{user_both_account2_usage}$",
            self.command,
        )

    def test_plain_user(self):
        """A user with only a non-partition association must be reported"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_plain}\|{account1}\|{user_plain_usage}$",
            self.command,
        )

    def test_two_partition_user(self):
        """A user with several partition associations in one account must be
        reported once with their usage summed"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_two_parts}\|{account1}\|{user_two_parts_usage}$",
            self.command,
        )

    def test_stale_lineage_user(self):
        """A deleted partition association whose lineage the non-partition
        association no longer covers must still have its usage counted"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_stale}\|{account3}\|{user_stale_usage}$",
            self.command,
        )

    def test_account_filter(self):
        """Users with only partition associations must be reported when
        filtering on accounts (as in the original ticket report)"""

        command = f"{self.command} accounts={account1}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_part_only}\|{account1}\|{user_part_only_usage}$",
            command,
        )
        _assert_report_line(
            output,
            rf"^{user_both}\|{account1}\|{user_both_account1_usage}$",
            command,
        )
        _assert_report_line(
            output,
            rf"^{user_plain}\|{account1}\|{user_plain_usage}$",
            command,
        )
        assert (
            re.search(rf"^[^|]*\|{account2}\|", output, re.MULTILINE) is None
        ), f"{account2} should not be reported when filtering on {account1}"

    def test_user_filter(self):
        """Users with only partition associations must be reported when
        filtering on users"""

        command = f"{self.command} users={user_part_only}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_part_only}\|{account1}\|{user_part_only_usage}$",
            command,
        )
        assert (
            re.search(rf"^{user_plain}\|", output, re.MULTILINE) is None
        ), f"{user_plain} should not be reported when filtering on {user_part_only}"

    def test_user_filter_sums_partitions(self):
        """A user with several partition associations must have them summed
        when filtering on users"""

        command = f"{self.command} users={user_two_parts}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(
            output,
            rf"^{user_two_parts}\|{account1}\|{user_two_parts_usage}$",
            command,
        )
        assert (
            len(re.findall(rf"^{user_two_parts}\|", output, re.MULTILINE)) == 1
        ), f"{user_two_parts} should be reported on exactly one row"


@pytest.mark.usefixtures("create_entities", "archive_load", "stale_lineage")
class TestAccountUtilizationByUser:
    """Test cluster AccountUtilizationByUser with partition associations"""

    command = f"sreport --local cluster AccountUtilizationByUser cluster='{cluster1}' start={period_start_string} end={period_end_string} -t Seconds -P -n format=Account,Login,Used"

    def test_account_totals(self):
        """Account and root rows must include partition association usage
        exactly once"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(output, rf"^root\|\|{root_usage}$", self.command)
        _assert_report_line(output, rf"^{account1}\|\|{account1_usage}$", self.command)
        _assert_report_line(output, rf"^{account2}\|\|{account2_usage}$", self.command)

    def test_user_rows(self):
        """Every user must be reported under their accounts with the usage
        of all of their associations counted exactly once"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{account1}\|{user_part_only}\|{user_part_only_usage}$",
            self.command,
        )
        _assert_report_line(
            output,
            rf"^{account1}\|{user_both}\|{user_both_account1_usage}$",
            self.command,
        )
        _assert_report_line(
            output,
            rf"^{account2}\|{user_both}\|{user_both_account2_usage}$",
            self.command,
        )
        _assert_report_line(
            output,
            rf"^{account1}\|{user_plain}\|{user_plain_usage}$",
            self.command,
        )
        _assert_report_line(
            output,
            rf"^{account1}\|{user_leak}\|{user_leak_usage}$",
            self.command,
        )
        assert (
            len(re.findall(rf"^{account1}\|{user_both}\|", output, re.MULTILINE)) == 1
        ), f"{user_both} should have exactly one row under {account1}"

    def test_two_partition_user_single_row(self):
        """A user with several partition associations in one account must
        get a single row with their usage summed"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{account1}\|{user_two_parts}\|{user_two_parts_usage}$",
            self.command,
        )
        assert (
            len(re.findall(rf"^{account1}\|{user_two_parts}\|", output, re.MULTILINE))
            == 1
        ), f"{user_two_parts} should have exactly one row under {account1}"

    def test_stale_lineage_user_keeps_partition_usage(self):
        """A deleted partition association whose lineage the non-partition
        association no longer covers must still have its usage counted"""

        output = atf.run_command_output(self.command, fatal=True)
        _assert_report_line(
            output,
            rf"^{account3}\|{user_stale}\|{user_stale_usage}$",
            self.command,
        )

    def test_account_filter_ignores_partition_names(self):
        """Filtering on an account must not pick up associations whose
        partition is named like that account"""

        command = f"{self.command} accounts={account2}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(
            output,
            rf"^{account2}\|{user_both}\|{user_both_account2_usage}$",
            command,
        )
        assert (
            re.search(rf"^{account1}\|", output, re.MULTILINE) is None
        ), f"{account1} rows should not be reported when filtering on {account2}"

        command = f"{self.command} accounts={account1}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(
            output,
            rf"^{account1}\|{user_leak}\|{user_leak_usage}$",
            command,
        )

    def test_account_filter_includes_sub_accounts(self):
        """Filtering on an account must still report its sub-accounts and
        their users; the deleted association kept its old lineage, so only
        the live association's usage shows up under account4"""

        command = f"{self.command} accounts={account4}"
        output = atf.run_command_output(command, fatal=True)
        _assert_report_line(output, rf"^{account4}\|\|{job9_usage}$", command)
        _assert_report_line(output, rf"^{account3}\|\|{job9_usage}$", command)
        _assert_report_line(
            output, rf"^{account3}\|{user_stale}\|{job9_usage}$", command
        )
        assert (
            re.search(rf"^{account1}\|", output, re.MULTILINE) is None
        ), f"{account1} rows should not be reported when filtering on {account4}"

    def test_tree_spans_sub_accounts(self):
        """The tree option must indent a sub-account under its parent"""

        command = f"{self.command} accounts={account4} tree"
        output = atf.run_command_output(command, fatal=True)
        parent = re.search(rf"^( *){account4}\|", output, re.MULTILINE)
        child = re.search(rf"^( *){account3}\|", output, re.MULTILINE)
        assert parent is not None, f"{account4} should be reported by {command}"
        assert child is not None, f"{account3} should be reported by {command}"
        assert len(child.group(1)) > len(
            parent.group(1)
        ), f"{account3} should be indented under {account4} with the tree option"
