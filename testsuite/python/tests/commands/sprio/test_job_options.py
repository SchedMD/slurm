############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting()
    atf.require_config_parameter('PriorityType', 'priority/multifactor')
    atf.require_slurm_running()


@pytest.fixture(scope='module')
def default_partition():
    return atf.default_partition()


@pytest.fixture(scope='module')
def queued_job(default_partition):
    nodes_dict = atf.get_nodes()
    total_cpus = 0

    for node_name in nodes_dict:
        node_dict = nodes_dict[node_name]
        node_partitions = node_dict['Partitions'].split(',')
        if default_partition in node_partitions and node_dict['State'] == 'IDLE':
            total_cpus += node_dict['CPUTot']

    running_job_id = atf.submit_job(f"--output=/dev/null --error=/dev/null -n {total_cpus} --exclusive --wrap=\"sleep 600\"", fatal=True)
    queued_job_id = atf.submit_job(f"--output=/dev/null --error=/dev/null -n {total_cpus} --exclusive --wrap=\"sleep 600\"", fatal=True)

    return queued_job_id
    

def test_noheader(queued_job, default_partition):
    """Verify sprio --noheader option"""

    output = atf.run_command_output(f"sprio --noheader -j {queued_job}", fatal=True)
    assert re.search(r'JOBID|PARTITION|PRIORITY|SITE', output) is None
    assert re.search(fr'{queued_job}\s+{default_partition}', output) is not None


def test_jobs(queued_job, default_partition):
    """Verify sprio --jobs option"""

    output = atf.run_command_output(f"sprio --jobs {queued_job}", fatal=True)
    assert re.search(r'JOBID\s+PARTITION\s+PRIORITY\s+SITE', output) is not None
    assert re.search(fr'{queued_job}\s+{default_partition}', output) is not None


def test_long(queued_job, default_partition):
    """Verify sprio --long option"""

    user_name = atf.get_user_name()
    output = atf.run_command_output(f"sprio --long -j {queued_job}", fatal=True)
    assert re.search(r'JOBID\s+PARTITION\s+USER\s+PRIORITY\s+SITE.*ASSOC.*PARTITION.*QOS', output) is not None
    assert re.search(fr'{queued_job}\s+{default_partition}\s+{user_name}(?:\s+\d+){{8,}}', output) is not None


def test_norm(queued_job, default_partition):
    """Verify sprio --norm option"""

    output = atf.run_command_output(f"sprio --norm -j {queued_job}", fatal=True)
    assert re.search(r'JOBID\s+PARTITION\s+PRIORITY', output) is not None
    assert re.search(fr'{queued_job}\s+{default_partition}\s+[\d\.]+', output) is not None


def test_format(queued_job, default_partition):
    """Verify sprio --norm option"""

    user_name = atf.get_user_name()
    output = atf.run_command_output(f"sprio --format \"%.15i %.8u %.10y %.10Y %.10S %.10a %.10A %.10b %.10B %.10f %.10F %.10j %.10J %.10p %.10P %.10q %.10Q %.6N\" -j {queued_job}", fatal=True)
    assert re.search(r'JOBID\s+USER\s+PRIORITY\s+PRIORITY\s+SITE\s+AGE\s+AGE\s+ASSOC\s+ASSOC', output) is not None
    assert re.search(r'FAIRSHARE\s+FAIRSHARE\s+JOBSIZE\s+JOBSIZE\s+PARTITION\s+PARTITION\s+QOS\s+QOS\s+NICE', output) is not None
    assert re.search(fr'{queued_job}\s+{user_name}\s+[\d\.]+\s+\d+\s+\d+\s+[\d\.]+\s+\d+\s+[\d\.]+\s+\d+\s+[\d\.]+\s+\d+\s+[\d\.]+\s+\d+\s+[\d\.]+\s+\d+\s+\d+', output) is not None


def test_user(queued_job, default_partition):
    """Verify sprio --user option"""

    user_name = atf.get_user_name()
    output = atf.run_command_output(f"sprio --user {user_name}", fatal=True)
    assert re.search(r'JOBID\s+PARTITION\s+USER', output) is not None
    assert re.search(fr'{queued_job}\s+{default_partition}\s+{user_name}', output) is not None


def test_verbose(queued_job, default_partition):
    """Verify sprio --verbose option"""

    output = atf.run_command_output(f"sprio --verbose -j {queued_job}", fatal=True)
    assert re.search(r'format\s+= \(null\)', output) is not None
    assert re.search(r'job_flag\s+= 1', output) is not None
    assert re.search(fr'jobs\s+= {queued_job}', output) is not None
    assert re.search(r'partition\s+= \(null\)', output) is not None
    assert re.search(r'users\s+= \(null\)', output) is not None
    assert re.search(r'verbose\s+= 1', output) is not None
    assert re.search(r'JOBID\s+PARTITION\s+PRIORITY', output) is not None
    assert re.search(fr'{queued_job}\s+{default_partition}\s+\d+', output) is not None
