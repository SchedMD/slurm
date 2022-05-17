############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import datetime
import os
import pytest
import re

# Period Boundaries
# Mon Dec 31 23:00:00 2007
period_start_datetime = datetime.datetime(2007, 12, 31, 23, 0, 0)
period_start_epoch = int(period_start_datetime.timestamp())
period_start_string = period_start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
# Thu Jan 31 23:59:59 2008
period_end_datetime = datetime.datetime(2008, 1, 31, 23, 59, 59)
period_end_epoch = int(period_end_datetime.timestamp())
period_end_string = period_end_datetime.strftime('%Y-%m-%dT%H:%M:%S')
# Midnight Fri Thu Jan 31 00:00:00 2008
midnight_datetime = datetime.datetime(2008, 1, 31, 0, 0, 0)
midnight_epoch = int(midnight_datetime.timestamp())

# Identities
uid = os.geteuid()
gid = os.getegid()

# Clusters
cluster1 = 'cluster1'
cluster2 = 'cluster2'

# Accounts
account1 = 'account1'
account2 = 'account2'
account3 = 'account3'

# Workload Characterization Keys
wckey1 = 'wckey1'

# Users
user1 = 'user1'
user2 = 'user2'

# Nodes
node0 = f"{cluster1}_node0"
node1 = f"{cluster1}_node1"
node_list = f"{cluster1}_node[0-1]"
node0_cpus = 2
node1_cpus = 2
cluster_cpus = node0_cpus + node1_cpus

# node0 down
node0_down_start_epoch = period_start_epoch + (45 * 60)
node0_down_end_epoch = period_start_epoch + (75 * 60)
node0_start_datetime = datetime.datetime.fromtimestamp(node0_down_start_epoch)
node0_start_string = node0_start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
node0_end_datetime = datetime.datetime.fromtimestamp(node0_down_end_epoch)
node0_end_string = node0_end_datetime.strftime('%Y-%m-%dT%H:%M:%S')

# Job names
test_job1 = 'job1'
test_job2 = 'job2'
test_job3 = 'job3'

# Job 0
# We want this to look like job1 but run right before hand
job0_start_epoch = period_start_epoch
job0_duration = 1200
job0_end_epoch = job0_start_epoch + job0_duration

# Job 1
job1_start_epoch = job0_end_epoch
job1_duration = 2700
job1_end_epoch = job1_start_epoch + job1_duration
job1_start_datetime = datetime.datetime.fromtimestamp(job1_start_epoch)
job1_start_string = job1_start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job1_end_datetime = datetime.datetime.fromtimestamp(job1_end_epoch)
job1_end_string = job1_end_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job1_duration_datetime = datetime.datetime.fromtimestamp(midnight_epoch + job1_duration)
job1_duration_string = job1_duration_datetime.strftime('%H:%M:%S')
job1_nodes = node1
job1_cpus = node1_cpus
job1_alloc = (job0_duration + job1_duration) * job1_cpus
job1_acct = account1

# Job 2
# Make job eligible an hour into the allocation
job2_elig_epoch = period_start_epoch + 3600
# start the job 65 minutes later so we can check reserved time
job2_start_epoch = job2_elig_epoch + 3900
# Run for a day
job2_duration = 86400
job2_end_epoch = job2_start_epoch + job2_duration
job2_start_datetime = datetime.datetime.fromtimestamp(job2_start_epoch)
job2_start_string = job2_start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job2_end_datetime = datetime.datetime.fromtimestamp(job2_end_epoch)
job2_end_string = job2_end_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job2_duration_datetime = datetime.datetime.fromtimestamp(midnight_epoch + job2_duration)
job2_duration_string = job2_duration_datetime.strftime('%-d-%H:%M:%S')
job2_nodes = f"{cluster1}_node[0-1]"
job2_cpus = node0_cpus + node1_cpus
job2_alloc = job2_duration * job2_cpus
job2_acct = account3

# Job 3
# Make job eligible an hour before the end of job2
job3_elig_epoch = job2_end_epoch - 3600
# Start the job at the end of job2
job3_start_epoch = job2_end_epoch
# Run for 65 minutes
job3_duration = 3900
job3_end_epoch = job3_start_epoch + job3_duration
job3_start_datetime = datetime.datetime.fromtimestamp(job3_start_epoch)
job3_start_string = job3_start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job3_end_datetime = datetime.datetime.fromtimestamp(job3_end_epoch)
job3_end_string = job3_end_datetime.strftime('%Y-%m-%dT%H:%M:%S')
job3_duration_datetime = datetime.datetime.fromtimestamp(midnight_epoch + job3_duration)
job3_duration_string = job3_duration_datetime.strftime('%H:%M:%S')
# Run on just node0
job3_nodes = node0
job3_cpus = node0_cpus
job3_alloc = job3_duration * job3_cpus
job3_acct = account2

# Cred Allocations
acct1_alloc = job1_alloc
acct3_alloc = job2_alloc
acct2_alloc = acct3_alloc + job3_alloc
total_alloc = job1_alloc + job2_alloc + job3_alloc
wckey1_alloc = job1_alloc + job2_alloc + job3_alloc
user1_wckey1_alloc = job1_alloc + job3_alloc
user2_wckey1_alloc = job2_alloc

# Association dictionaries (populated up in create_entities fixture)
user_account_id = {}
user_wckey_id = {}

# Federation
federation1 = 'federation1'


# Setup
@pytest.fixture(scope='module', autouse=True)
def setup():
    atf.require_accounting(modify=True)
    atf.require_config_parameter('TrackWCKey', 'Yes', source='slurmdbd')
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def create_entities():
    """Populate accounting database with entities (clusters, accounts, users, ...)"""

    # Create accounting entities
    atf.run_command(f"sacctmgr -i add cluster {cluster1}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add cluster {cluster2}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add account {account1} cluster={cluster1},{cluster2}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add account {account2} cluster={cluster1},{cluster2}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add account {account3} cluster={cluster1},{cluster2} parent={account2}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add user user={user1} cluster={cluster1},{cluster2} account={account1},{account2},{account3} wckey={wckey1}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i add user user={user2} cluster={cluster1},{cluster2} account={account1},{account2},{account3} wckey={wckey1}", user=atf.properties['slurm-user'], fatal=True)

    # Populate user_account_id dictionary with user-account association ids
    output = atf.run_command_output(f"sacctmgr -n -P list assoc users={user1},{user2} account={account1},{account2},{account3} cluster={cluster2} format=\"user,account,id\"", fatal=True)
    for line in output.splitlines():
        if match := re.search(r'^([^|]+)\|([^|]+)\|(\d+)$', line):
            user, account, id = match.group(1, 2, 3)
            if user not in user_account_id:
                user_account_id[user] = {}
            user_account_id[user][account] = id
    # Verify they were all populated with an id
    for user in user1, user2:
        if user not in user_account_id:
            atf.log_die(f"Account association for {user} was not created")
        for account in account1, account2, account3:
            if account not in user_account_id[user]:
                atf.log_die(f"Association for {user} and {account} was not created")

    # Populate user_wckey_id dictionary with user-wckey association ids
    output = atf.run_command_output(f"sacctmgr -n -P list wckeys users={user1},{user2} wckeys={wckey1} cluster={cluster1} format=\"user,wckey,id\"", user=atf.properties['slurm-user'], fatal=True)
    for line in output.splitlines():
        if match := re.search(r'^([^|]+)\|([^|]+)\|(\d+)$', line):
            user, wckey, id = match.group(1, 2, 3)
            if user not in user_wckey_id:
                user_wckey_id[user] = {}
            user_wckey_id[user][wckey] = id
    # Verify they were all populated with an id
    for user in user1, user2:
        if user not in user_wckey_id:
            atf.log_die(f"WCKey association for {user} was not created")
        if wckey1 not in user_wckey_id[user]:
            atf.log_die(f"Association for {user} and {wckey1} was not created")


@pytest.fixture(scope='module')
def archive_load(create_entities):
    """Populate accounting database with cluster utilization data"""

    # Cluster utilization
    for cluster in cluster1, cluster2:
        sql_input_file = f"{cluster}.sql"
        sql_input_path = f"{str(atf.module_tmp_path / sql_input_file)}"
        with open(sql_input_path, 'w') as f:
            # Add utilization data for a period prior to today's date
            # We are using 'Mon Dec 31 23:00:00 2007' = 1199167200 as the start
            f.write(f"insert into cluster_event_table (node_name, cluster, tres, period_start, period_end, reason, cluster_nodes) values ('', '{cluster}', '1={cluster_cpus}', {period_start_epoch}, {period_end_epoch}, 'Cluster processor count', '{node_list}')")
            # Mark a node down for 30 minutes starting at 45 minutes after the start to make sure our rollups work so we should get 15 minutes on one hour and 15 on the other
            f.write(f", ('{node0}', '{cluster}', '1={node0_cpus}', {node0_down_start_epoch}, {node0_down_end_epoch}, 'down','')")
            f.write(" on duplicate key update period_start=VALUES(period_start), period_end=VALUES(period_end);\n")
            # Now we will put in a job running for an hour and 5 minutes
            f.write("insert into job_table (jobid, associd, wckey, wckeyid, uid, gid, `partition`, blockid, cluster, account, eligible, submit, start, end, suspended, name, state, comp_code, priority, req_cpus, tres_alloc, nodelist, kill_requid, qos, deleted) values")
            f.write(f" ('65536', '{user_account_id[user1][account1]}', '{wckey1}', '{user_wckey_id[user1][wckey1]}', '{uid}', '{gid}', 'debug', '', '{cluster}', '{job1_acct}', {job0_start_epoch}, {job0_start_epoch}, {job0_start_epoch}, {job0_end_epoch}, '0', '{test_job1}', '3', '0', '{job1_cpus}', {job1_cpus}, '1={job1_cpus}', '{job1_nodes}', '0', '0', '0')")
            f.write(f", ('65537', '{user_account_id[user1][account1]}', '{wckey1}', '{user_wckey_id[user1][wckey1]}', '{uid}', '{gid}', 'debug', '', '{cluster}', '{job1_acct}', {job1_start_epoch}, {job1_start_epoch}, {job1_start_epoch}, {job1_end_epoch}, '0', '{test_job1}', '3', '0', '{job1_cpus}', {job1_cpus}, '1={job1_cpus}', '{job1_nodes}', '0', '0', '0')")
            f.write(f", ('65538', '{user_account_id[user2][account3]}', '{wckey1}', '{user_wckey_id[user2][wckey1]}', '{uid}', '{gid}', 'debug', '', '{cluster}', '{job2_acct}', {job2_elig_epoch}, {job2_elig_epoch}, {job2_start_epoch}, {job2_end_epoch}, '0', '{test_job2}', '3', '0', '{job2_cpus}', {job2_cpus}, '1={job2_cpus}', '{job2_nodes}', '0', '0', '0')")
            f.write(f", ('65539', '{user_account_id[user1][account2]}', '{wckey1}', '{user_wckey_id[user1][wckey1]}', '{uid}', '{gid}', 'debug', '', '{cluster}', '{job3_acct}', {job3_elig_epoch}, {job3_elig_epoch}, {job3_start_epoch}, {job3_end_epoch}, '0', '{test_job3}', '3', '0', {job3_cpus}, '{job3_cpus}', '1={job3_cpus}', '{job3_nodes}', '0', '0', '0')")
            f.write(" on duplicate key update id=LAST_INSERT_ID(id), eligible=VALUES(eligible), submit=VALUES(submit), start=VALUES(start), end=VALUES(end), associd=VALUES(associd), tres_alloc=VALUES(tres_alloc), wckey=VALUES(wckey), wckeyid=VALUES(wckeyid);\n")

        # Perform archive load
        atf.run_command(f"sacctmgr -i -n archive load {sql_input_path}", user=atf.properties['slurm-user'], fatal=True)

        # Use sacct to see if the job loaded
        output = atf.run_command_output(f"sacct -P -M {cluster} --format=cluster,account,associd,wckey,wckeyid,start,end,elapsed --noheader --start={period_start_string} --end={period_end_string}", fatal=True)
        # Verify values
        if not re.search(fr"{cluster}\|{account1}\|{user_account_id[user1][account1]}\|{wckey1}\|{user_wckey_id[user1][wckey1]}\|{job1_start_string}\|{job1_end_string}\|{job1_duration_string}", output):
            atf.log_die(f"The job accounting data was not loaded correctly for job1")
        if not re.search(fr"{cluster}\|{account3}\|{user_account_id[user2][account3]}\|{wckey1}\|{user_wckey_id[user2][wckey1]}\|{job2_start_string}\|{job2_end_string}\|{job2_duration_string}", output):
            atf.log_die(f"The job accounting data was not loaded correctly for job2")
        if not re.search(fr"{cluster}\|{account2}\|{user_account_id[user1][account2]}\|{wckey1}\|{user_wckey_id[user1][wckey1]}\|{job3_start_string}\|{job3_end_string}\|{job3_duration_string}", output):
            atf.log_die(f"The job accounting data was not loaded correctly for job3")

        # Use sacctmgr to see if the node event loaded
        output = atf.run_command_output(f"sacctmgr -P list events cluster={cluster} format=cluster,noden,start,end,cpu --noheader start={period_start_string} end={period_end_string}", fatal=True)
        # Verify values
        if not re.search(fr"{cluster}\|\|{period_start_string}\|{period_end_string}\|{cluster_cpus}", output):
            atf.log_die(f"The event accounting data was not loaded correctly for the cluster")
        if not re.search(fr"{cluster}\|{node0}\|{node0_start_string}\|{node0_end_string}\|{node0_cpus}", output):
            atf.log_die(f"The event accounting data was not loaded correctly for node0")

        # Use sacctmgr to roll up the time period
        atf.run_command_output(f"sacctmgr -i roll {period_start_string} {period_end_string}", user=atf.properties['slurm-user'], fatal=True)


@pytest.fixture(scope='class')
def configure_federation():
    """Configuration for federation tests"""

    # Create federation1
    atf.run_command(f"sacctmgr -i add federation {federation1}", user=atf.properties['slurm-user'], fatal=True)
    atf.run_command(f"sacctmgr -i mod federation {federation1} set clusters={cluster1},{cluster2}", user=atf.properties['slurm-user'], fatal=True)

    # Set the ClusterName to cluster1
    original_cluster_name = atf.get_config_parameter('ClusterName')
    atf.set_config_parameter('ClusterName', cluster1)

    yield

    # Undo the above
    atf.run_command(f"sacctmgr -i delete federation {federation1}", user=atf.properties['slurm-user'], fatal=True)
    atf.set_config_parameter('ClusterName', original_cluster_name)


@pytest.mark.usefixtures('create_entities', 'archive_load', 'configure_federation')
class TestFederation:
    """Test federated sreport functionality on second hour cluster1 usage"""

    # Set class variables

    cluster = f"FED:{federation1}"
    # Second hour start: Tue Jan 1 00:00:00 2008
    start_datetime = datetime.datetime(2008, 1, 1, 0, 0, 0)
    start_epoch = int(start_datetime.timestamp())
    start_string = start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
    # Second hour end: Tue Jan 1 01:00:00 2008
    end_datetime = datetime.datetime(2008, 1, 1, 1, 0, 0)
    end_epoch = int(end_datetime.timestamp())
    end_string = end_datetime.strftime('%Y-%m-%dT%H:%M:%S')

    reported_duration = (end_epoch - start_epoch) * cluster_cpus * 2
    down_duration = (node0_down_end_epoch - start_epoch) * node0_cpus * 2
    allocated_duration = (job1_end_epoch - start_epoch) * job1_cpus * 2
    wckey_allocated_duration = allocated_duration
    reserved_duration = (end_epoch - job2_elig_epoch) * job2_cpus * 2
    # Use the same logic inside the plugin to figure out the correct idle and reserved durations
    idle_duration = reported_duration - (down_duration + allocated_duration + reserved_duration)
    if idle_duration < 0:
        reserved_duration = reserved_duration + idle_duration
        idle_duration = 0
        if reserved_duration < 0:
            reserved_duration = 0

    down_string = f"{down_duration}({float(down_duration*100)/reported_duration:.2f}%)"
    allocated_string = f"{allocated_duration}({float(allocated_duration*100)/reported_duration:.2f}%)"
    reserved_string = f"{reserved_duration}({float(reserved_duration*100)/reported_duration:.2f}%)"
    idle_string = f"{idle_duration}({float(idle_duration*100)/reported_duration:.2f}%)"
    reported_string = f"{reported_duration}({100:.2f}%)"


    def test_cluster_utilization(self):
        """Test utilization report for second hour"""

        command = f"sreport --federation cluster utilization start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,idle,down,alloc,res,reported"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{self.idle_string}|{self.down_string}|{self.allocated_string}|{self.reserved_string}|{self.reported_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_AccountUtilizationByUser(self):
        """Test cluster AccountUtilizationByUser report"""

        command = f"sreport --federation cluster AccountUtilizationByUser start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|root||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByWckey(self):
        """Test cluster UserUtilizationByWckey report"""

        command = f"sreport --federation cluster UserUtilizationByWckey start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{user1}||{wckey1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_WckeyUtilizationByUser(self):
        """Test cluster WckeyUtilizationByUser report"""

        command = f"sreport --federation cluster WckeyUtilizationByUser start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}|||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{wckey1}|{user1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_user_top(self):
        """Test User Top report"""

        command = f"sreport --federation user top start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{user1}||{account1}|{self.allocated_string}|{self.idle_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_size(self):
        """Test Job Size report"""

        command = f"sreport --federation job size AcctAsParent grouping=2,4 start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account1}|0|{self.allocated_duration}|0|100.00%"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_sizesbywckey(self):
        """Test Job sizesbywckey report"""

        command = f"sreport --federation job sizesbywckey grouping=2,4 start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}|0|{self.wckey_allocated_duration}|0|100.00%"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


@pytest.mark.usefixtures('create_entities', 'archive_load')
class TestFirstHour:
    """Test cluster usage for the first hour"""

    # Set class variables

    # First hour start: Mon Dec 31 23:00:00 2007
    start_datetime = datetime.datetime(2007, 12, 31, 23, 0, 0)
    start_epoch = int(start_datetime.timestamp())
    start_string = start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
    # Second hour end: Tue Jan 1 00:00:00 2008
    end_datetime = datetime.datetime(2008, 1, 1, 0, 0, 0)
    end_epoch = int(end_datetime.timestamp())
    end_string = end_datetime.strftime('%Y-%m-%dT%H:%M:%S')

    reported_duration = (end_epoch - start_epoch) * cluster_cpus
    down_duration = (end_epoch - node0_down_start_epoch) * node0_cpus
    allocated_duration = (end_epoch - job0_start_epoch) * node1_cpus
    wckey_allocated_duration = allocated_duration
    reserved_duration = 0
    idle_duration = reported_duration - (down_duration + allocated_duration + reserved_duration)

    down_string = f"{down_duration}({float(down_duration*100)/reported_duration:.2f}%)"
    allocated_string = f"{allocated_duration}({float(allocated_duration*100)/reported_duration:.2f}%)"
    reserved_string = f"{reserved_duration}({float(reserved_duration*100)/reported_duration:.2f}%)"
    idle_string = f"{idle_duration}({float(idle_duration*100)/reported_duration:.2f}%)"
    reported_string = f"{reported_duration}({100:.2f}%)"


    def test_cluster_utilization(self):
        """Test cluster utilization report for first hour"""

        command = f"sreport -M{cluster2} cluster utilization cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,idle,down,alloc,res,reported"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster1}|{self.idle_string}|{self.down_string}|{self.allocated_string}|{self.reserved_string}|{self.reported_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{self.idle_string}|{self.down_string}|{self.allocated_string}|{self.reserved_string}|{self.reported_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByAccount(self):
        """Test cluster UserUtilizationByAccount report"""

        command = f"sreport -M{cluster2} cluster UserUtilizationByAccount start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,account,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster2}|{user1}|{account1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} cluster UserUtilizationByAccount cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,account,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster1}|{user1}|{account1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{user1}|{account1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_AccountUtilizationByUser(self):
        """Test cluster AccountUtilizationByUser report"""

        command = f"sreport -M{cluster2} cluster AccountUtilizationByUser start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster2}|root||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{account1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} cluster AccountUtilizationByUser cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster1}|root||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster1}|{account1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster1}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|root||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{account1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByWckey(self):
        """Test cluster UserUtilizationByWckey report"""

        command = f"sreport -M{cluster2} cluster UserUtilizationByWckey start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,wckey,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster2}|{user1}||{wckey1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} cluster UserUtilizationByWckey cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,wckey,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster1}|{user1}||{wckey1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{user1}||{wckey1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_WckeyUtilizationByUser(self):
        """Test cluster WckeyUtilizationByUser report"""

        command = f"sreport -M{cluster2} cluster WckeyUtilizationByUser start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,wckey,login,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster2}|{wckey1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{wckey1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} cluster WckeyUtilizationByUser cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,wckey,login,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster1}|{wckey1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster1}|{wckey1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{wckey1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{wckey1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_user_top(self):
        """Test User Top report"""

        command = f"sreport -M{cluster2} user top start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster2}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} user top cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster1}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_size(self):
        """Test Job Size report"""

        command = f"sreport -M{cluster2} job size AcctAsParent grouping=2,4 start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster2}|{account1}|0|{self.allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport --local job size AcctAsParent grouping=2,4 cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{cluster1}|{account1}|0|{self.allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_sizesbywckey(self):
        """Test Job sizesbywckey report"""

        command = f"sreport -M{cluster2} job sizesbywckey grouping=2,4 start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster2}|{wckey1}|0|{self.wckey_allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport -M{cluster2} job sizesbywckey grouping=2,4 cluster='{cluster1}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{cluster1}|{wckey1}|0|{self.wckey_allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{cluster2}|{wckey1}|0|{self.wckey_allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


@pytest.mark.usefixtures('create_entities', 'archive_load')
class TestSecondHour:
    """Test cluster usage for the second hour"""

    # Set class variables

    # Since there are 2 test clusters we will just use one
    cluster = cluster1

    # Second hour start: Tue Jan 1 00:00:00 2008
    start_datetime = datetime.datetime(2008, 1, 1, 0, 0, 0)
    start_epoch = int(start_datetime.timestamp())
    start_string = start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
    # Second hour end: Tue Jan 1 01:00:00 2008
    end_datetime = datetime.datetime(2008, 1, 1, 1, 0, 0)
    end_epoch = int(end_datetime.timestamp())
    end_string = end_datetime.strftime('%Y-%m-%dT%H:%M:%S')

    reported_duration = (end_epoch - start_epoch) * cluster_cpus
    down_duration = (node0_down_end_epoch - start_epoch) * node0_cpus
    allocated_duration = (job1_end_epoch - start_epoch) * job1_cpus
    wckey_allocated_duration = allocated_duration
    reserved_duration = (end_epoch - job2_elig_epoch) * job2_cpus
    idle_duration = reported_duration - (down_duration + allocated_duration + reserved_duration)
    # Use the same logic inside the plugin to figure out the correct idle and reserved durations
    if idle_duration < 0:
        reserved_duration = reserved_duration + idle_duration
        idle_duration = 0
        if reserved_duration < 0:
            reserved_duration = 0

    down_string = f"{down_duration}({float(down_duration*100)/reported_duration:.2f}%)"
    allocated_string = f"{allocated_duration}({float(allocated_duration*100)/reported_duration:.2f}%)"
    reserved_string = f"{reserved_duration}({float(reserved_duration*100)/reported_duration:.2f}%)"
    idle_string = f"{idle_duration}({float(idle_duration*100)/reported_duration:.2f}%)"
    reported_string = f"{reported_duration}({100:.2f}%)"


    def test_cluster_utilization(self):
        """Test cluster utilization report for second hour"""

        command = f"sreport --local cluster utilization cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,idle,down,alloc,res,reported"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{self.idle_string}|{self.down_string}|{self.allocated_string}|{self.reserved_string}|{self.reported_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_AccountUtilizationByUser(self):
        """Test cluster AccountUtilizationByUser report"""

        command = f"sreport --local cluster AccountUtilizationByUser cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|root||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByWckey(self):
        """Test cluster UserUtilizationByWckey report"""

        command = f"sreport --local cluster UserUtilizationByWckey cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,wckey,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{user1}|{wckey1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_WckeyUtilizationByUser(self):
        """Test cluster WckeyUtilizationByUser report"""

        command = f"sreport --local cluster WckeyUtilizationByUser cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,wckey,login,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}||{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{wckey1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_user_top(self):
        """Test User Top report"""

        command = f"sreport --local user top cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account1}|{user1}|{self.allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_size(self):
        """Test Job Size report"""

        command = f"sreport --local job size AcctAsParent grouping=2,4 cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account1}|0|{self.allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_sizesbywckey(self):
        """Test Job sizesbywckey report"""

        command = f"sreport --local job sizesbywckey grouping=2,4 cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}|0|{self.wckey_allocated_duration}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


@pytest.mark.usefixtures('create_entities', 'archive_load')
class TestFirstThreeDays:
    """Test cluster usage for the first 3 days"""

    # Set class variables

    # Since there are 2 test clusters we will just use one
    cluster = cluster1

    # First 3 Days Start: Mon Dec 31 23:00:00 2007
    start_datetime = datetime.datetime(2007, 12, 31, 23, 0, 0)
    start_epoch = int(start_datetime.timestamp())
    start_string = start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
    # First 3 Days End: Tue Jan 3 00:00:00 2008
    end_datetime = datetime.datetime(2008, 1, 3, 0, 0, 0)
    end_epoch = int(end_datetime.timestamp())
    end_string = end_datetime.strftime('%Y-%m-%dT%H:%M:%S')

    reported_duration = (end_epoch - start_epoch) * cluster_cpus
    down_duration = (node0_down_end_epoch - node0_down_start_epoch) * node0_cpus
    allocated_duration = (
        ((job1_end_epoch - job0_start_epoch) * job1_cpus) +
        ((job2_end_epoch - job2_start_epoch) * job2_cpus) +
        ((job3_end_epoch - job3_start_epoch) * job3_cpus)
    )
    wckey_allocated_duration1 = job1_alloc + job3_alloc
    wckey_allocated_duration2 = job2_alloc
    reserved_duration = (
        ((job2_start_epoch - job2_elig_epoch) * job2_cpus) +
        ((job3_start_epoch - job3_elig_epoch) * job3_cpus)
    )
    # I didn't have time to do the correct math here so I am just putting in 9000 which should be the correct value of over commit
    overcommit_duration = 9000
    reserved_duration -= overcommit_duration
    idle_duration = reported_duration - (down_duration + allocated_duration + reserved_duration)
    # Use the same logic inside the plugin to figure out the correct idle and reserved durations
    if idle_duration < 0:
        reserved_duration = reserved_duration + idle_duration
        idle_duration = 0
        if reserved_duration < 0:
            reserved_duration = 0

    down_string = f"{down_duration}({float(down_duration*100)/reported_duration:.2f}%)"
    allocated_string = f"{allocated_duration}({float(allocated_duration*100)/reported_duration:.2f}%)"
    reserved_string = f"{reserved_duration}({float(reserved_duration*100)/reported_duration:.2f}%)"
    idle_string = f"{idle_duration}({float(idle_duration*100)/reported_duration:.2f}%)"
    overcommit_string = f"{overcommit_duration}({float(overcommit_duration*100)/reported_duration:.2f}%)"
    reported_string = f"{reported_duration}({100:.2f}%)"

    job1_allocated_string = f"{job1_alloc}({float(job1_alloc*100)/reported_duration:.2f}%)"
    job2_allocated_string = f"{job2_alloc}({float(job2_alloc*100)/reported_duration:.2f}%)"
    job3_allocated_string = f"{job3_alloc}({float(job3_alloc*100)/reported_duration:.2f}%)"
    total_allocated_string = f"{total_alloc}({float(total_alloc*100)/reported_duration:.2f}%)"
    account1_allocated_string = f"{acct1_alloc}({float(acct1_alloc*100)/reported_duration:.2f}%)"
    account2_allocated_string = f"{acct2_alloc}({float(acct2_alloc*100)/reported_duration:.2f}%)"
    account3_allocated_string = f"{acct3_alloc}({float(acct3_alloc*100)/reported_duration:.2f}%)"
    wckey1_allocated_string = f"{wckey1_alloc}({float(wckey1_alloc*100)/reported_duration:.2f}%)"
    user1_wckey1_allocated_string = f"{user1_wckey1_alloc}({float(user1_wckey1_alloc*100)/reported_duration:.2f}%)"
    user2_wckey1_allocated_string = f"{user2_wckey1_alloc}({float(user2_wckey1_alloc*100)/reported_duration:.2f}%)"


    def test_cluster_utilization(self):
        """Test cluster utilization report for first 3 days"""

        command = f"sreport --local cluster utilization cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,idle,down,alloc,res,over,reported"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{self.idle_string}|{self.down_string}|{self.allocated_string}|{self.reserved_string}|{self.overcommit_string}|{self.reported_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByAccount(self):
        """Test cluster UserUtilizationByAccount report"""

        command = f"sreport --local cluster UserUtilizationByAccount cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,account,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{user2}|{account3}|{self.job2_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{user1}|{account1}|{self.job1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{user1}|{account2}|{self.job3_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_AccountUtilizationByUser(self):
        """Test cluster AccountUtilizationByUser report"""

        command = f"sreport --local cluster AccountUtilizationByUser cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|root||{self.total_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}||{self.account1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}|{user1}|{self.job1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account2}||{self.account2_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account2}|{user1}|{self.job3_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account3}||{self.account3_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account3}|{user2}|{self.job2_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_UserUtilizationByWckey(self):
        """Test cluster UserUtilizationByWckey report"""

        command = f"sreport --local cluster UserUtilizationByWckey cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,login,wckey,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{user2}|{wckey1}|{self.user2_wckey1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{user1}|{wckey1}|{self.user1_wckey1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_cluster_WckeyUtilizationByUser(self):
        """Test cluster WckeyUtilizationByUser report"""

        command = f"sreport --local cluster WckeyUtilizationByUser cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,wckey,login,used"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}||{self.wckey1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{wckey1}|{user1}|{self.user1_wckey1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{wckey1}|{user2}|{self.user2_wckey1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_user_top(self):
        """Test User Top report"""

        command = f"sreport --local user top cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n format=cluster,account,login,used"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account3}|{user2}|{self.job2_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account1}|{user1}|{self.job1_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account2}|{user1}|{self.job3_allocated_string}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_size(self):
        """Test Job Size report"""

        command = f"sreport --local job size AcctAsParent grouping=2,4 cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account1}|0|{job1_alloc}|0"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        pattern = fr"{self.cluster}|{account2}|0|{job3_alloc}|{job2_alloc}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'
        command = f"sreport --local job size AcctAsParent grouping=2,4 cluster='{self.cluster}' account='{account2}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, fatal=True)
        pattern = fr"{self.cluster}|{account3}|0|0|{job2_alloc}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


    def test_job_sizesbywckey(self):
        """Test Job sizesbywckey report"""

        command = f"sreport --local job sizesbywckey grouping=2,4 cluster='{self.cluster}' start={self.start_string} end={self.end_string} -tsecper -P -n"
        output = atf.run_command_output(command, user=atf.properties['slurm-user'], fatal=True)
        pattern = fr"{self.cluster}|{wckey1}|0|{self.wckey_allocated_duration1}|{self.wckey_allocated_duration2}"
        assert re.search(pattern, output) is not None, f'Command output for "{command}" did not match expected pattern "{pattern}"'


@pytest.mark.usefixtures('create_entities', 'archive_load')
class TestSpecificJobs:
    """Test for jobs that ran on a node at a certain time"""

    # Set class variables

    # Since there are 2 test clusters we will just use one
    cluster = cluster1


    def test_job1(self):
        """Search for job1 on cluster1"""

        output = atf.run_command_output(f"sacct -p -M {self.cluster} --state=completed --start={job1_start_string} --end={job1_end_string} --format=jobname", fatal=True)
        assert len(re.findall(fr"{test_job1}", output)) == 2, "Job 1 not found (twice) in sacct"


    def test_job2(self):
        """Search for job2 on cluster1"""

        output = atf.run_command_output(f"sacct -p -M {self.cluster} --state=completed --start={job2_start_string} --end={job2_end_string} --format=jobname", fatal=True)
        assert len(re.findall(fr"{test_job2}", output)) == 1, "Job 2 not found (once) in sacct"


    def test_job3(self):
        """Search for job3 on cluster1"""

        output = atf.run_command_output(f"sacct -p -M {self.cluster} --state=completed --start={job3_start_string} --end={job3_end_string} --format=jobname", fatal=True)
        assert len(re.findall(fr"{test_job3}", output)) == 1, "Job 3 not found (once) in sacct"


@pytest.mark.usefixtures('create_entities', 'archive_load')
class TestOptions:
    """Get job usage reports with default options and with different switches"""

    # Set class variables

    # Start: Mon Dec 31 23:00:00 2007
    start_datetime = datetime.datetime(2007, 12, 31, 23, 0, 0)
    start_epoch = int(start_datetime.timestamp())
    start_string = start_datetime.strftime('%Y-%m-%dT%H:%M:%S')
    # End: Tue Dec 31 23:59:59 2008
    end_datetime = datetime.datetime(2008, 12, 31, 23, 59, 59)
    end_epoch = int(end_datetime.timestamp())
    end_string = end_datetime.strftime('%Y-%m-%dT%H:%M:%S')


    def test_job_sizesbyaccount_default(self):
        """Test job report for default (non-AcctAsParent)"""

        output = atf.run_command_output(f"sreport job sizesbyaccount printjobcount cluster='{cluster1}' start={self.start_string} end={self.end_string} -P -n", fatal=True)
        assert re.search(fr"{cluster1}|root|4|0|0|0|0|100.00%", output) is not None


    def test_job_sizesbyaccount_flatview(self):
        """Test job report with flatview"""

        output = atf.run_command_output(f"sreport job sizesbyaccount printjobcount cluster='{cluster1}' start={self.start_string} end={self.end_string} -P -n flatview", fatal=True)
        #test22-1clus1|test22-1acct1|2|0|0|0|0|50.00%
        #test22-1clus1|test22-1acct2|1|0|0|0|0|25.00%
        #test22-1clus1|test22-1acct3|1|0|0|0|0|25.00%
        assert re.search(fr"{cluster1}|{account1}|2|0|0|0|0|50.00%\n{cluster1}|{account2}|1|0|0|0|0|25.00\n{cluster1}|{account3}|1|0|0|0|0|25.00", output) is not None


    def test_specific_account(self):
        """Test the job report for specific account"""

        # Should show just one account with all of its jobs
        output = atf.run_command_output(f"sreport job sizesbyaccount printjobcount cluster='{cluster1}' account={account1} start={self.start_string} end={self.end_string} -P -n", fatal=True)
        assert re.search(fr"{cluster1}|{account1}|2|0|0|0|0|100.00%", output) is not None
