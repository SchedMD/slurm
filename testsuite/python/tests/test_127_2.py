############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""An Epilog must see the job's node list however the controller ended the job.

Ticket 25958.

The node list a termination message carries becomes the Epilog's
SLURM_JOB_NODELIST, and slurmd before 26.11 also derived this node's index
into the job's GRES arrays from it (26.11 takes the index from the job
credential instead). So a message that carries no list, or only the subset it
is addressed to, leaves the Epilog without SLURM_JOB_NODELIST or pointing at
another node's devices. Two of the paths that end a job used to do that, and
each has a test here.

re_kill_job() used to leave the node list of REQUEST_TERMINATE_JOB unset. A node
whose slurmd is down when the job is cancelled never gets the first termination
request, so the resend that follows its return is the only one it sees and the
Epilog runs from that message.

kill_job_on_node() used to send only the one node it was addressing. It answers
a node that registers a job the controller still holds as completing, which a
node whose slurmd was down across the termination does when it comes back.

The nodes carry a GPU so that the GRES half of the contract, which is what the
Changelog leads with, is checked and not just the node list it derives from.
"""

import pathlib
import re
import shlex

import pytest

import atf

pytestmark = pytest.mark.slow

NODE_COUNT = 2

# re_kill_job() only resends once a job has been completing for longer than
# KillWait + 2*MessageTimeout, on a purge_old_job() tick every MinJobAge
# seconds, and it skips a node that is not responding, so the resend lands
# after the slurmd below is back.
KILL_WAIT = 5
MESSAGE_TIMEOUT = 10
MIN_JOB_AGE = 10
RESEND_TIMEOUT = KILL_WAIT + (2 * MESSAGE_TIMEOUT) + (6 * MIN_JOB_AGE)

# A restarted controller learns of a node only when it registers, which is
# slower than anything atf's default timeout is tuned for.
REGISTRATION_TIMEOUT = 120

# The Epilog environment the fix restores. SLURM_JOB_NODELIST comes from the
# message's node list (req->nodes), which pre-26.11 slurmd also indexed the
# job's GRES arrays with; SLURM_JOB_GPUS is the output an Epilog needs to
# clean the right devices up. SLURM_JOB_NUM_NODES comes from the credential's
# job_nhosts, built from the same pre-shrink allocation, so the two must
# agree.
NODELIST_VAR = "SLURM_JOB_NODELIST"
GPUS_VAR = "SLURM_JOB_GPUS"
NUM_NODES_VAR = "SLURM_JOB_NUM_NODES"

# prolog_epilog.html lists SLURM_NODELIST beside SLURM_JOB_NODELIST, and a job
# holding GPUs gets CUDA_VISIBLE_DEVICES beside SLURM_JOB_GPUS. Each pair is
# built from the same value in the same place -- setup_cred_arg() for the node
# list, gres_common_prep_set_env() for the devices, where the vendor string
# falls back to the same device index when a device carries no unique id -- so
# a node list or an index the fix missed shows up in both halves of the pair.
ALIAS_NODELIST_VAR = "SLURM_NODELIST"
CUDA_VAR = "CUDA_VISIBLE_DEVICES"

# The tests using filler_job need it and the test job to hold devices on the
# same node at the same time. select/linear allocates whole nodes, so the
# test job would sit pending behind the filler job instead. live=False: this
# is evaluated at collection, before any fixture has started the daemons.
requires_node_sharing = pytest.mark.skipif(
    atf.get_config_parameter("SelectType", live=False, quiet=True) == "select/linear",
    reason="Ticket 25958: select/linear allocates whole nodes, so the "
    "filler job and the test job cannot share one",
)


# Setup
# Resolved by setup().
_CTLD_LOG = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    # The full node list on a termination message is 26.05.6 / 26.11.0.
    atf.require_version((26, 5, 6), reason="Ticket 25958")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    # Two devices per node, not one: with a single device every node's
    # allocation is index 0 regardless of which node job_gres_prep's entry
    # actually came from, so a wrong node index reads exactly like a correct
    # one. filler_job below blocks one device to force the test
    # job onto a distinct, known index on each, so a swapped index shows up
    # as a swapped value instead of disappearing into the coincidence.
    # RealMemory bigger than the default: with no --mem, a job takes a whole
    # node's memory, so the filler job and the test job sharing job_nodes[1]
    # in filler_job need room to both fit.
    qualified_nodes = atf.require_nodes(
        NODE_COUNT, [("Gres", "gpu:2"), ("CPUs", 2), ("RealMemory", 4)]
    )
    # Back every configured GPU with a device file. A GPU with no gres.conf
    # entry is file-less, so slurmd drops it, and a node left with fewer
    # devices than its Gres= promises keeps the configured count under
    # SlurmdParameters=config_overrides but loses its device topology. Either
    # way the Epilog gets no SLURM_JOB_GPUS. So write one device per GPU each
    # node is configured with rather than the one require_nodes() asked for,
    # which a node that already carries more of them satisfies as it is.
    gpu_counts = {}
    for name, params in atf.get_nodes(live=False, quiet=True).items():
        if name == "DEFAULT":
            continue
        if match := re.search(r"gpu:(?:[^:,]+:)?(\d+)", str(params.get("Gres", ""))):
            gpu_counts[name] = int(match.group(1))
    assert gpu_counts, "Expected require_nodes() to configure nodes with GPUs"
    for i in range(max(gpu_counts.values())):
        atf.require_tty(i)
    # Nodes are separate to Slurm, so the same device files serve them all.
    # The dict-of-dicts form is what require_config_parameter() documents for a
    # parameter that repeats and carries its own sub-parameters, which is what
    # a gres.conf NodeName stanza is.
    gpu_nodes = sorted(gpu_counts)
    atf.require_config_parameter(
        "NodeName",
        {
            name: {
                "Name": "gpu",
                "File": (
                    f"/dev/tty[0-{gpu_counts[name] - 1}]"
                    if gpu_counts[name] > 1
                    else "/dev/tty0"
                ),
            }
            for name in gpu_nodes
        },
        source="gres",
    )
    atf.require_config_parameter("KillWait", KILL_WAIT)
    atf.require_config_parameter("MessageTimeout", MESSAGE_TIMEOUT)
    atf.require_config_parameter("MinJobAge", MIN_JOB_AGE)
    # kill_job_on_node() announces itself with a debug() line, which is the
    # only way to tell its Epilog run from one the resend produced, so debug
    # is the floor. List the levels above it as well, so a cluster already
    # running one of them is not lowered; debug2 leads because it is what the
    # SQA configs ship and so is what an unconfigured run already has.
    atf.require_config_parameter(
        "SlurmctldDebug", ["debug2", "debug", "debug3", "debug4", "debug5"]
    )
    # The resend, the registration and the orphan path are only visible in the
    # controller's log, so a cluster that does not keep one cannot host this
    # test.
    global _CTLD_LOG
    _CTLD_LOG = next(
        (
            str(value)
            for name, value in atf.get_config(live=False, quiet=True).items()
            if name.casefold() == "slurmctldlogfile"
        ),
        None,
    )
    if not _CTLD_LOG or _CTLD_LOG.lower() == "none":
        pytest.skip("Ticket 25958: Test requires SlurmctldLogFile to name a log file")
    atf.require_slurm_running()
    return qualified_nodes


@pytest.fixture(scope="module")
def job_nodes(setup):
    """Idle nodes satisfying the test's GPU, CPU and memory requirements."""
    nodes = [
        name
        for name, node in atf.get_nodes(quiet=True, fatal=True).items()
        if name in setup and set(node.get("state", [])) == {"IDLE"}
    ]
    if len(nodes) < NODE_COUNT:
        pytest.skip(f"Ticket 25958: Test requires {NODE_COUNT} idle GPU nodes")
    return nodes[:NODE_COUNT]


@pytest.fixture(scope="function")
def filler_job(job_nodes):
    """Pins one device on job_nodes[1] for the life of the test.

    The real test job's own --gres=gpu:1 there can then only land on another
    device, while job_nodes[0] is untouched. The two nodes hold different
    devices, so a node index that reaches the wrong node's entry in
    job_gres_prep surfaces as the other node's device number instead of a
    coincidentally identical one. _gpu_by_node() reads back which device each
    node actually got rather than assuming the allocation order.
    """
    filler_node = job_nodes[1]
    filler_id = atf.submit_job_sbatch(
        f"-N1 -w {filler_node} --gres=gpu:1 --mem=1 "
        f"-J test_127_2_filler -t 30 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(filler_id, "RUNNING", fatal=True)

    yield filler_node

    atf.run_command(f"scancel {filler_id}", fatal=True, quiet=True)


@pytest.fixture(scope="function")
def recording_epilog():
    """Epilog that records the environment it was given, once per run."""
    # The Epilog runs from slurmd's cwd, so both paths have to be absolute.
    # conftest.py chdirs each test function into its own tmp_path, so this
    # resolves somewhere no other test can have written to.
    record_dir = pathlib.Path("nodelists").resolve()
    record_dir.mkdir()
    record_dir.chmod(0o777)
    epilog = str(pathlib.Path("epilog.sh").resolve())
    atf.make_bash_script(
        epilog,
        f"""echo "{NODELIST_VAR}=${{{NODELIST_VAR}:-unset}} """
        f"""{ALIAS_NODELIST_VAR}=${{{ALIAS_NODELIST_VAR}:-unset}} """
        f"""{GPUS_VAR}=${{{GPUS_VAR}:-unset}} """
        f"""{CUDA_VAR}=${{{CUDA_VAR}:-unset}} """
        f"""{NUM_NODES_VAR}=${{{NUM_NODES_VAR}:-unset}}" >> {record_dir}/$SLURM_JOB_ID.$SLURMD_NODENAME
exit 0
""",
    )
    atf.set_config_parameter("Epilog", epilog)
    # scontrol reconfigure returns once the controller has reloaded; each
    # slurmd picks the Epilog up on its own. Restart so no job can launch
    # on a node whose slurmd does not have it yet.
    atf.restart_slurm(quiet=True)

    yield record_dir

    atf.set_config_parameter("Epilog", None)


@pytest.fixture(scope="function", autouse=True)
def restore_slurmd(setup, job_nodes, recording_epilog):
    """Bring back the slurmd the test stops, so the next test has its node.

    Requesting recording_epilog orders this fixture after it, so the node is
    back before the Epilog is unset. Otherwise a test that fails before the
    slurmd is restarted reconfigures the cluster with that node still down.
    """
    yield

    # Whichever node the test stopped, not a fixed one: the tests here take
    # down the batch host or the node the job does not run its batch script
    # on, and which is which is read from the job rather than assumed.
    for node in job_nodes:
        # start_slurmd() kills any running instance first, so an unconditional
        # restart would bounce a healthy slurmd, and a job still completing on
        # that node would lose it. A node whose slurmd the test restarted
        # itself lands here with a live slurmd and still needs everything
        # below, so only this step is conditional.
        if not atf.slurmd_pids(node):
            atf.start_slurmd(node, quiet=True)

        # A node whose slurmd was killed can be left DOWN or NOT_RESPONDING.
        # RESUME is only a legal transition out of DOWN/DRAIN/FAIL, so it
        # succeeds or is rejected depending on whether the slurmd serving the
        # node already registered it healthy. Neither outcome is a failure, so
        # this is left non-fatal and the IDLE wait below is what decides
        # whether the node really came back.
        atf.run_command(
            f"scontrol update NodeName={node} State=RESUME",
            user=atf.properties["slurm-user"],
            quiet=True,
        )
        # The restarted slurmd has to register before the node reads as IDLE,
        # which takes longer than atf's default is tuned for.
        assert atf.wait_for_node_state(
            node, "IDLE", operator="==", timeout=REGISTRATION_TIMEOUT
        ), f"Node {node} should be back in service for the next test"


def _log_size():
    """Bookmark the controller log before a test's observation window."""
    result = atf.run_command(
        f"stat -c %s -- {shlex.quote(_CTLD_LOG)}", user="root", quiet=True
    )
    return int(result["stdout"]) if result["exit_code"] == 0 else 0


def _log_text(offset):
    """Read controller log entries after a byte bookmark, as root."""
    result = atf.run_command(
        f"tail -c +{offset + 1} -- {shlex.quote(_CTLD_LOG)}",
        user="root",
        quiet=True,
    )
    return result["stdout"] if result["exit_code"] == 0 else ""


def _recorded_env(record_dir, node, job_id):
    """Every Epilog environment a job recorded on a node, in order.

    Each entry is a dict of the recorded variable names to their values. The
    record is keyed by job as well as node because the filler job that pins a
    device shares a node with the job under test, and its own Epilog is
    correct for a single node allocation.
    """
    # No xfail=: a node that has not run its Epilog yet legitimately has no
    # record, and that missing file is what several of the waits below poll on.
    result = atf.run_command(f"cat {record_dir}/{job_id}.{node}", quiet=True)
    if result["exit_code"] != 0:
        return []
    return [
        dict(field.split("=", 1) for field in line.split())
        for line in result["stdout"].splitlines()
        if line
    ]


def _gpu_by_node(job_id):
    """The GPU device index the controller gave the job on each of its nodes.

    scontrol -d show job reports the allocation a node at a time as
    Nodes=<expression> ... GRES=gpu:1(IDX:n). Reading it back rather than
    assuming which device the filler job left free keeps a change in GRES
    selection policy from reading as a wrong node index below.

    Fails when no node reports an index: slurmd builds SLURM_JOB_GPUS from the
    device files behind the node's GPU, so a job that holds none leaves the
    variable unset in every Epilog however the job ended, and the GRES half of
    this test would pass without checking anything.
    """
    output = atf.run_command_output(
        f"scontrol -d show job {job_id}", fatal=True, quiet=True
    )
    gpus = {}
    for match in re.finditer(r"Nodes=(\S+).*?GRES=\S*?\(IDX:([^)]+)\)", output):
        for node in atf.node_range_to_list(match.group(1)):
            gpus[node] = match.group(2)
    assert gpus, (
        f"JobId={job_id} reports no per node GPU device index, so the "
        f"{GPUS_VAR} half of the Epilog environment cannot be checked"
    )
    return gpus


def _assert_epilog_env(node, recorded, expected, expected_gpu):
    """Every Epilog run on a node must have seen the job's full allocation."""
    for env in recorded:
        nodelist = env[NODELIST_VAR]
        assert nodelist != "unset", (
            f"Epilog on {node} saw no {NODELIST_VAR}, so slurmd "
            f"cannot derive this node's index into the job's GRES arrays"
        )
        # prolog_epilog.html promises the same node list under both names, so
        # compare them verbatim rather than after expansion: a hostlist that
        # was built twice could agree on the nodes and differ in its wording.
        assert env[ALIAS_NODELIST_VAR] == nodelist, (
            f"Epilog on {node} saw {ALIAS_NODELIST_VAR}="
            f"{env[ALIAS_NODELIST_VAR]!r} but {NODELIST_VAR}={nodelist!r}; "
            f"both name the job's allocation"
        )
        names = sorted(atf.node_range_to_list(nodelist))
        assert names == expected, (
            f"Epilog on {node} saw {NODELIST_VAR}={nodelist!r}, "
            f"expected the full allocation {expected}"
        )
        # A site Epilog that pairs the node list with the node count needs the
        # two to agree; both are built from job_ptr->job_resrcs before it is
        # resized, so they should never diverge.
        assert env[NUM_NODES_VAR] == str(len(expected)), (
            f"Epilog on {node} saw {NUM_NODES_VAR}={env[NUM_NODES_VAR]!r}, "
            f"expected {len(expected)} to match {NODELIST_VAR}={nodelist!r}"
        )
        # The point of the node list: the index it yields must reach this
        # node's own GRES, so the Epilog can clean the right device up.
        #
        # An index that is negative or past the end makes
        # gres_common_prep_set_env() bail before it sets the variable, and one
        # that lands on a node holding no GPU leaves the device bitmap empty,
        # so both failure modes read as "unset" here.
        #
        # filler_job forces the two nodes onto different devices, so this
        # discriminates a correct index from a wrong one: a node index that
        # reaches the other node's job_gres_prep entry reads as that node's
        # device number here, not this one's.
        assert env[GPUS_VAR] == expected_gpu, (
            f"Epilog on {node} saw {GPUS_VAR}={env[GPUS_VAR]!r}, expected "
            f"{expected_gpu!r} for the GPU the job held on this node"
        )
        # The vendor variable comes off the same device bitmap and the same
        # node index, and falls back to the device number when the device has
        # no unique id, which is the case for the tty devices setup() backs
        # these GPUs with. _gpu_by_node() is what guards both.
        assert env[CUDA_VAR] == expected_gpu, (
            f"Epilog on {node} saw {CUDA_VAR}={env[CUDA_VAR]!r}, expected "
            f"{expected_gpu!r} for the GPU the job held on this node"
        )


@requires_node_sharing
def test_resent_terminate_job_sets_epilog_nodelist(
    job_nodes, recording_epilog, filler_job
):
    """An Epilog run from a resent termination must see the full allocation."""

    record_dir = recording_epilog
    expected = sorted(job_nodes)
    log_offset = _log_size()

    job_id = atf.submit_job_sbatch(
        f"--requeue -N{NODE_COUNT} -w {atf.node_list_to_range(job_nodes)} "
        f"--gres=gpu:1 --mem=1 -J test_127_2 -t 5 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    job_gpus = _gpu_by_node(job_id)

    # The absent node must not be the batch host: that is the one node with a
    # stepd for this job, so if it were the one to come back it would register
    # the still-completing job and the Epilog would come from
    # kill_job_on_node() before the resend under test ever fires.
    present_node = atf.get_job_parameter(job_id, "BatchHost", quiet=True)
    assert present_node in job_nodes, (
        f"JobId={job_id} reported BatchHost={present_node!r}, which is not "
        f"one of the nodes it was pinned to: {job_nodes}"
    )
    absent_node = next(node for node in job_nodes if node != present_node)

    # Take the node's slurmd away before the job is cancelled, so the first
    # termination request never reaches it and its Epilog does not run.
    atf.stop_slurmd(absent_node, quiet=True)
    assert not atf.slurmd_pids(absent_node), (
        f"slurmd on {absent_node} is still running, so it would answer the "
        f"first termination request and the resend would not be exercised"
    )

    atf.run_command(f"scancel {job_id}", fatal=True)

    # The surviving node's Epilog is the marker that the first termination
    # request was delivered and run; only then does an empty record on the
    # absent node mean it was skipped rather than merely not sampled yet.
    present_recorded = []
    # Longer than atf's default: the Epilog has to be dispatched, run and
    # flushed. Polled fast, since the absent node's empty record is only
    # meaningful once this one has appeared.
    for _ in atf.timer(timeout=60, poll_interval=1):
        present_recorded = _recorded_env(record_dir, present_node, job_id)
        if present_recorded:
            break
    else:
        pytest.fail(f"Node {present_node} never ran its Epilog for the cancel")

    # This node's Epilog ran from the first termination request, which is the
    # path the resend below only ever stands in for.
    _assert_epilog_env(present_node, present_recorded, expected, job_gpus[present_node])

    assert not _recorded_env(record_dir, absent_node, job_id), (
        f"Node {absent_node} ran its Epilog while its slurmd was down, so the "
        f"resend below would not be the run under test"
    )

    # The agent carrying the first termination retries the connection for
    # MessageTimeout seconds, and a slurmd that comes back inside that window
    # answers that request rather than the resend. Wait for the controller to
    # give up on it, which is when it flags the node as not responding. The
    # MessageTimeout this test configures is what sets the floor here, so
    # atf's default timeout would not cover it.
    assert atf.wait_for_node_state(
        absent_node, "NOT_RESPONDING", timeout=MESSAGE_TIMEOUT + 60
    ), (
        f"slurmctld should have given up on its first termination for "
        f"{absent_node}, leaving the resend as the only request it can see"
    )

    # Back in service, the node is eligible for the resend.
    atf.start_slurmd(absent_node, quiet=True)

    recorded = []
    for _ in atf.timer(timeout=RESEND_TIMEOUT, poll_interval=5):
        recorded = _recorded_env(record_dir, absent_node, job_id)
        if recorded:
            break
    else:
        pytest.fail(
            f"Ticket 25958: {absent_node} never ran its Epilog, so a resent "
            f"termination was never exercised. A controller busy enough to hit "
            f"LOTS_OF_AGENTS skips the resend tick entirely."
        )

    # Registering a job the controller still holds as completing can also draw
    # a termination, so confirm the resend is what fired before crediting it.
    # Both paths set the node list now, but only one is this test's subject.
    assert re.search(
        f"Resending TERMINATE_JOB request JobId={job_id} ",
        _log_text(log_offset),
        re.MULTILINE,
    ), (
        f"Node {absent_node} ran its Epilog, but slurmctld never logged a "
        f"resend for JobId={job_id}, so the run came from the registration "
        f"path rather than re_kill_job()"
    )

    # Every run on this node came from a resend, so all of them must carry the
    # allocation, not just the first.
    _assert_epilog_env(absent_node, recorded, expected, job_gpus[absent_node])


@requires_node_sharing
def test_registered_completing_job_sets_epilog_nodelist(
    job_nodes, recording_epilog, filler_job
):
    """An Epilog run for a node that registers a completing job must see the
    full allocation."""

    record_dir = recording_epilog
    expected = sorted(job_nodes)
    log_offset = _log_size()

    job_id = atf.submit_job_sbatch(
        f"--requeue -N{NODE_COUNT} -w {atf.node_list_to_range(job_nodes)} "
        f"--gres=gpu:1 --mem=1 -J test_127_2c -t 5 -o /dev/null"
        ' --wrap "sleep infinity"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    job_gpus = _gpu_by_node(job_id)

    # A node reports what it has a stepd for, and a batch job with no step of
    # its own has one only on its batch host. The batch host is therefore the
    # only node that can register this job when its slurmd comes back, so it
    # is the one whose slurmd has to go away.
    batch_host = atf.get_job_parameter(job_id, "BatchHost", quiet=True)
    assert batch_host in job_nodes, (
        f"JobId={job_id} reported BatchHost={batch_host!r}, which is not one "
        f"of the nodes it was pinned to: {job_nodes}"
    )
    others = [node for node in job_nodes if node != batch_host]

    atf.stop_slurmd(batch_host, quiet=True)
    assert not atf.slurmd_pids(batch_host), (
        f"slurmd on {batch_host} is still running, so it would answer the "
        f"termination request and never have to register the job"
    )

    # requeuehold rather than requeue, so the job cannot be scheduled again
    # while this runs. Either way the controller starts completing the job on
    # the whole allocation, and the batch host cannot answer.
    atf.run_command(
        f"scontrol requeuehold {job_id}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    # The other node's Epilog is the marker that the termination went out.
    # That node reports and the batch host does not, so the job stays
    # COMPLETING with the batch host still owing a reply, which is the state
    # that sends the registration below down kill_job_on_node().
    for node in others:
        # Longer than atf's default: the termination has to reach the node,
        # run its Epilog and flush the record.
        for _ in atf.timer(timeout=60, poll_interval=1):
            if _recorded_env(record_dir, node, job_id):
                break
        else:
            pytest.fail(f"Node {node} never ran its Epilog for the requeue")

    assert not _recorded_env(record_dir, batch_host, job_id), (
        f"Node {batch_host} ran its Epilog while its slurmd was down, so the "
        f"registration below would not be the run under test"
    )

    # The agent carrying the termination retries the connection for
    # MessageTimeout seconds, and a slurmd that comes back inside that window
    # answers that request rather than registering. Wait for the controller to
    # give up on it, which is when it flags the node as not responding.
    # re_kill_job() skips a node in that state, so no resend can reach it
    # either, and the registration is left as the only way in. The
    # MessageTimeout this test configures is what sets the floor here, so
    # atf's default timeout would not cover it.
    assert atf.wait_for_node_state(
        batch_host, "NOT_RESPONDING", timeout=MESSAGE_TIMEOUT + 60
    ), (
        f"slurmctld should have given up on its termination for {batch_host}, "
        f"leaving its registration as the only request it can see"
    )

    # Back up, slurmd re-attaches to the batch stepd it left running and
    # reports the job. The controller still holds the job as completing, so it
    # answers with a termination addressed to this one node.
    atf.start_slurmd(batch_host, quiet=True)

    recorded = []
    # Longer than atf's default: the node has to register, be told to kill the
    # job, run its Epilog and flush the record.
    for _ in atf.timer(timeout=REGISTRATION_TIMEOUT, poll_interval=1):
        recorded = _recorded_env(record_dir, batch_host, job_id)
        if recorded:
            break
    else:
        pytest.fail(
            f"Ticket 25958: {batch_host} never ran its Epilog after "
            f"registering JobId={job_id}, so kill_job_on_node() was not "
            f"exercised"
        )

    # kill_job_on_node() is the only thing that logs this, so it is what tells
    # this run apart from one the resend produced. A resend that fired as well
    # would carry the same node list, and every recorded run is checked below
    # either way.
    assert re.search(
        rf"Killing JobId={job_id} on node {batch_host}$",
        _log_text(log_offset),
        re.MULTILINE,
    ), (
        f"Node {batch_host} ran its Epilog, but slurmctld never logged killing "
        f"JobId={job_id} on it, so the run came from another path"
    )

    # The job still holds this node, so prolog_epilog.html's exception does
    # not apply: the Epilog gets the allocation, not the one node the request
    # was addressed to.
    _assert_epilog_env(batch_host, recorded, expected, job_gpus[batch_host])

    atf.run_command(f"scancel {job_id}", fatal=True)
