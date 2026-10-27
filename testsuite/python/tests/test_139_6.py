############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test SlurmctldParameters=dynamic_node_dns.

slurm.conf.5 says that with this option slurmctld tells each host to resolve a
dynamic node's NodeName through its own resolver, while slurmctld keeps using
the address the node registered from.

A single host cannot have split-horizon DNS, so the two halves are tested by
giving the nodes NodeNames that do not resolve, or that resolve to an address
nothing listens on. slurmctld must still reach them, and everyone else must
fail to.
"""

import pytest

import atf

node_name = "ddns-no-such-node"
cloud_node = "ddns-no-such-cloud-node"
resolvable_node = "ddns-resolvable-node"
job_name = "test_139_6"

# TEST-NET-1 (RFC 5737), which is not the node's address
unroutable_addr = "192.0.2.1"

# A resolver with a wildcard or a search domain would answer for these names and
# both halves of this test would be meaningless.
pytestmark = pytest.mark.skipif(
    any(
        atf.run_command_exit(f"getent hosts {name}", quiet=True) == 0
        for name in (node_name, cloud_node)
    ),
    reason=f"the resolver answers for {node_name} or {cloud_node}",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # srun is the command that must understand the marking and resolve the
    # NodeName. Clients are upgraded last, so gate on srun, not on slurmctld.
    atf.require_version(
        (26, 11), "bin/srun", reason="dynamic_node_dns was added in 26.11"
    )

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("MaxNodeCount", 8)
    atf.require_config_parameter_includes("SlurmctldParameters", "dynamic_node_dns")
    atf.require_config_parameter(
        "PartitionName", {"primary": {"Nodes": "ALL", "Default": "YES"}}
    )

    # Needed to create a CLOUD node and to reach it at its registered address
    atf.require_config_parameter("ResumeProgram", "/bin/true")
    atf.require_config_parameter("SuspendProgram", "/bin/true")
    atf.require_config_parameter("SuspendTime", 600)
    atf.require_config_parameter_includes("SlurmctldParameters", "cloud_reg_addrs")
    # Otherwise every job has a step manager and keeps its credential's setting
    atf.require_config_parameter_excludes("SlurmctldParameters", "enable_stepmgr")

    # Without it slurmctld drops --stepmgr and only logs it
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_slurm_running()


def srun_step(node, extra="", xfail=False):
    return atf.run_command(
        f"srun --job-name={job_name} --mpi=none -t1 -N1 -w {node} {extra} "
        "-o /dev/null /bin/true",
        xfail=xfail,
    )


def remove_hosts_line():
    # /etc/hosts can be a bind mount, which cp or mv cannot replace. Rewrite
    # it in place. grep exits 1 when it selects nothing, which is not an error
    # here, so only stop on 2.
    atf.run_command(
        f"t=$(mktemp) && {{ grep -vxE '[0-9.]+ {resolvable_node}' /etc/hosts "
        f"> $t; [ $? -le 1 ]; }} && cat $t > /etc/hosts && rm -f $t",
        user="root",
        fatal=True,
        quiet=True,
    )


def set_hosts_line(addr):
    remove_hosts_line()
    atf.run_command(
        f"echo '{addr} {resolvable_node}' >> /etc/hosts",
        user="root",
        fatal=True,
        quiet=True,
    )


@pytest.fixture(scope="module")
def dynamic_node():
    """Register a dynamic node whose NodeName does not resolve."""
    atf.start_slurmd(node_name, "-Z", wait="idle")
    return node_name


@pytest.fixture(scope="module")
def resolvable_dynamic_node():
    """Register a dynamic node whose NodeName resolves to another address.

    slurmctld reaches the node at the address it registered from, so the job
    still runs and cleans up. Only a host that uses its resolver's answer is
    sent elsewhere.
    """
    # Its own port, because node0's slurmd and the unresolvable node's slurmd
    # are already running on this host.
    atf.start_slurmd(resolvable_node, "-Z --conf 'Port=6830'", wait="idle")

    # Written last: everything above can abort the fixture, and the cleanup
    # below only runs if we reach the yield. Setting the line also cleans up
    # after a run that died before its cleanup.
    set_hosts_line(unroutable_addr)

    yield resolvable_node

    remove_hosts_line()


@pytest.fixture(scope="module")
def dynamic_cloud_node():
    """Register a CLOUD node whose NodeName does not resolve."""
    atf.run_command(
        f"scontrol create NodeName={cloud_node} State=CLOUD Port=6831",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.start_slurmd(cloud_node, "-Z --conf 'Port=6831'", wait="idle")

    yield cloud_node

    atf.stop_slurmd(cloud_node)
    atf.run_command(
        f"scontrol delete NodeName={cloud_node}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )


def disable_dynamic_node_dns(node):
    """Drop only the dynamic_node_dns value."""
    atf.remove_config_parameter_value("SlurmctldParameters", "dynamic_node_dns")
    # Each change reconfigures, and slurmctld re-execs and recovers the node
    # from state, so wait for it before the next step launch.
    atf.wait_for_node_state(node, "IDLE", fatal=True)


def enable_dynamic_node_dns(node):
    atf.require_config_parameter_includes("SlurmctldParameters", "dynamic_node_dns")
    atf.wait_for_node_state(node, "IDLE", fatal=True)


@pytest.fixture
def restore_dynamic_node_dns(dynamic_node):
    """Put dynamic_node_dns back however the test ends."""
    yield

    enable_dynamic_node_dns(dynamic_node)


def submit_running_job(node, extra=""):
    job_id = atf.submit_job_sbatch(
        f"--job-name={job_name} -N1 -w {node} {extra} --wrap 'sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    return job_id


@pytest.fixture
def running_job(dynamic_node):
    """A job on dynamic_node, allocated while dynamic_node_dns is set."""
    job_id = submit_running_job(dynamic_node)

    yield job_id

    atf.cancel_jobs([job_id], fatal=True)


@pytest.fixture
def stepmgr_job(dynamic_node):
    """A job with a step manager, allocated while dynamic_node_dns is set."""
    job_id = submit_running_job(dynamic_node, "--stepmgr")

    yield job_id

    atf.cancel_jobs([job_id], fatal=True)


def assert_no_address(result, node):
    assert (
        result["exit_code"] != 0
    ), f"srun reached {node} even though its NodeName does not resolve"
    # The allocation succeeds, so this has to be the launch failing to find an
    # address rather than the node being unusable for some other reason.
    assert f"can't resolve {node}" in result["stderr"], (
        f"srun failed, but not because the NodeName would not resolve: "
        f"{result['stderr']}"
    )


def test_step_launch_needs_the_nodename_to_resolve(dynamic_node):
    """srun must resolve the NodeName itself, and fail when it cannot."""
    assert_no_address(srun_step(dynamic_node, xfail=True), dynamic_node)


@pytest.mark.parametrize("node_fixture", ["dynamic_node", "resolvable_dynamic_node"])
def test_step_launch_works_without_the_option(
    request, node_fixture, restore_dynamic_node_dns
):
    """Without the option every host uses the address slurmctld recorded."""
    node = request.getfixturevalue(node_fixture)
    disable_dynamic_node_dns(node)

    result = srun_step(node)

    assert result["exit_code"] == 0, (
        f"srun could not reach {node} using the address slurmctld "
        f"distributed: {result['stderr']}"
    )


def test_option_applies_again_when_set_back(dynamic_node, restore_dynamic_node_dns):
    """Turning the option off and on again must mark the node again."""
    disable_dynamic_node_dns(dynamic_node)
    enable_dynamic_node_dns(dynamic_node)

    assert_no_address(srun_step(dynamic_node, xfail=True), dynamic_node)


def test_running_job_follows_the_option(
    dynamic_node, restore_dynamic_node_dns, running_job
):
    """New steps of an already allocated job must use the new setting.

    slurm.conf.5 says this holds for jobs without a step manager.
    """
    overlap = f"--jobid={running_job} --overlap"
    assert_no_address(srun_step(dynamic_node, overlap, xfail=True), dynamic_node)

    atf.remove_config_parameter_value("SlurmctldParameters", "dynamic_node_dns")
    assert atf.wait_for_job_state(
        running_job, "RUNNING"
    ), f"Job {running_job} did not survive the reconfigure"

    result = srun_step(dynamic_node, overlap)

    assert result["exit_code"] == 0, (
        f"A new step of job {running_job} kept the marking after "
        f"dynamic_node_dns was removed: {result['stderr']}"
    )


def test_stepmgr_job_keeps_the_option(
    dynamic_node, restore_dynamic_node_dns, stepmgr_job
):
    """New steps of a job with a step manager must keep the job's setting.

    slurm.conf.5 says so. The step manager builds every step credential from
    the job credential it started with, which a reconfigure does not reach.
    """
    flags = atf.get_jobs(stepmgr_job, use_json=True, fatal=True)[stepmgr_job]["flags"]
    assert (
        "STEPMGR_ENABLED" in flags
    ), f"Job {stepmgr_job} has no step manager, so this test would prove nothing"

    overlap = f"--jobid={stepmgr_job} --overlap"
    assert_no_address(srun_step(dynamic_node, overlap, xfail=True), dynamic_node)

    atf.remove_config_parameter_value("SlurmctldParameters", "dynamic_node_dns")
    assert atf.wait_for_job_state(
        stepmgr_job, "RUNNING"
    ), f"Job {stepmgr_job} did not survive the reconfigure"

    assert_no_address(srun_step(dynamic_node, overlap, xfail=True), dynamic_node)


def test_slurmctld_uses_the_registered_address(dynamic_node):
    """slurmctld must keep reaching the node at the address it recorded."""
    # Other tests and fixture teardowns stand between setup() and here, and a
    # batch job reaches the node with or without the option, so without this
    # the test could pass while proving nothing.
    assert atf.config_parameter_includes(
        "SlurmctldParameters", "dynamic_node_dns"
    ), "dynamic_node_dns is not in effect, so this test would prove nothing"

    # A batch job needs only slurmctld to reach the node, which it does over
    # the recorded address. A step additionally needs the client to resolve
    # the NodeName, which is the half that must fail.
    job_id = atf.submit_job_sbatch(
        f"--job-name={job_name} -t1 -N1 -w {dynamic_node} --wrap '/bin/true'",
        fatal=True,
    )

    assert atf.wait_for_job_state(
        job_id, "COMPLETED"
    ), f"slurmctld could not run a batch job on {dynamic_node}"


def test_step_launch_uses_the_local_resolution(resolvable_dynamic_node):
    """srun must connect to the address its resolver gives for the NodeName.

    The NodeName first resolves to an address that is not the node's, so a
    launch that used the recorded address instead would succeed.
    test_step_launch_works_without_the_option shows that it does. Pointing
    the name at this host must then make the launch succeed, which a launch
    that refused marked nodes outright would not.
    """
    assert atf.config_parameter_includes(
        "SlurmctldParameters", "dynamic_node_dns"
    ), "dynamic_node_dns is not in effect, so this test would prove nothing"
    # An earlier reconfigure may still be recovering this node
    atf.wait_for_node_state(resolvable_dynamic_node, "IDLE", fatal=True)

    set_hosts_line(unroutable_addr)
    result = srun_step(resolvable_dynamic_node, xfail=True)

    assert result["exit_code"] != 0, (
        f"srun reached {resolvable_dynamic_node} even though its NodeName "
        f"resolves to {unroutable_addr}, so it used the recorded address"
    )

    set_hosts_line("127.0.0.1")
    result = srun_step(resolvable_dynamic_node)

    assert result["exit_code"] == 0, (
        f"srun could not reach {resolvable_dynamic_node} even though its "
        f"NodeName resolves to this host: {result['stderr']}"
    )


def test_cloud_node_is_not_marked(dynamic_cloud_node):
    """A CLOUD node must keep the address slurmctld recorded."""
    assert atf.config_parameter_includes(
        "SlurmctldParameters", "dynamic_node_dns"
    ), "dynamic_node_dns is not in effect, so this test would prove nothing"

    result = srun_step(dynamic_cloud_node)

    assert result["exit_code"] == 0, (
        f"srun could not reach CLOUD node {dynamic_cloud_node}, so it was "
        f"told to resolve the NodeName: {result['stderr']}"
    )
