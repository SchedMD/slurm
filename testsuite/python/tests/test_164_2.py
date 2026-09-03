############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""
Test that a job requeued while completing keeps its completing nodes
accounted when slurmctld restarts after one of its nodes was removed from
slurm.conf.

_sync_jobs_to_conf() fails such a job with JOB_NODE_FAIL because its node
list no longer resolves, and job_state_set() replaces the whole state word,
so the job used to lose JOB_COMPLETING there.  _sync_nodes_to_jobs() and
_sync_nodes_to_comp_job() both skip a job without that flag, so no node's
comp_job_cnt was incremented, while the requeue that follows still rebuilt
a full node_bitmap_cg -- every epilog complete that arrived afterwards
underflowed the counter.

Ticket: 25792
"""

import re

import pytest

import atf

pytestmark = pytest.mark.slow


@pytest.fixture(scope="module", autouse=True)
def setup():
    # Fixed in 26.05.5 and 26.11.0; nothing released in between lacks it.
    atf.require_version(
        (26, 5, 5),
        component="sbin/slurmctld",
        reason="Ticket 25792: completing flag dropped on a failed pending job",
    )
    atf.require_auto_config("needs to set Epilog and remove a node from slurm.conf")
    # One more than the job asks for, so a node can be removed and the job,
    # which is requeued rather than terminal, still has somewhere to run.
    atf.require_nodes(3)
    # The Epilog below blocks until the test releases it; bound it so an
    # aborted run cannot leave a node completing for the rest of the suite.
    atf.require_config_parameter("PrologEpilogTimeout", 300)
    atf.require_slurm_running()


@pytest.fixture
def epilog_proceed(tmp_path):
    """Sentinel the Epilog waits on, always released so none is left hung."""

    proceed = str(tmp_path / "epilog_proceed")
    yield proceed
    atf.run_command(f"touch {proceed}", fatal=True, quiet=True)


@pytest.fixture
def stop_removed_slurmd():
    """Stops slurmds of nodes removed from slurm.conf, which shutdown misses."""

    nodes = []
    yield nodes.append
    for node in nodes:
        atf.run_command(
            f"pkill -f '[s]lurmd -N {node}$'", user="root", fatal=True, quiet=True
        )


def _logfile():
    return atf.get_config_parameter("SlurmctldLogFile", live=False, quiet=True)


def _log_grep(pattern):
    """Matching slurmctld log lines, or an empty string."""

    return atf.run_command_output(
        f"grep -F -- '{pattern}' {_logfile()} || true",
        user=atf.properties["slurm-user"],
        quiet=True,
    ).strip()


def _remove_node_from_conf(node_name):
    """Drop node_name from slurm.conf, splitting its NodeName range if needed."""

    config_file = f"{atf.properties['slurm-config-dir']}/slurm.conf"
    original_config = atf.run_command_output(
        f"cat {config_file}", user=atf.properties["slurm-user"], quiet=True
    )

    new_config_lines = []
    removed = False
    for line in original_config.splitlines():
        match = re.match(r"(?i)^ *NodeName=(\S+)(.*)$", line)
        if match is None:
            new_config_lines.append(line)
            continue

        node_names = atf.node_range_to_list(match.group(1))
        if node_name not in node_names:
            new_config_lines.append(line)
            continue

        removed = True
        node_index = node_names.index(node_name)
        node_names.remove(node_name)
        if not node_names:
            continue

        # A per-node Port list must shrink with the node range, or slurmctld
        # refuses the config ("Port count must equal that of NodeName
        # records"). Mirrors the remainder handling in atf.set_node_parameter().
        attributes = []
        for word in match.group(2).split():
            parameter, _, value = word.partition("=")
            if parameter.lower() == "port":
                port_list = atf.range_to_list(value)
                if node_index < len(port_list):
                    port_list.pop(node_index)
                if port_list:
                    word = f"Port={atf.list_to_range(port_list)}"
            attributes.append(word)

        node_range = atf.node_list_to_range(node_names)
        remainder = f"NodeName={node_range} {' '.join(attributes)}"
        new_config_lines.append(remainder.rstrip())

    assert removed, f"{node_name} was not found in {config_file}"

    new_config_string = "\n".join(new_config_lines)
    atf.run_command(
        f"echo '{new_config_string}' > {config_file}",
        user=atf.properties["slurm-user"],
        fatal=True,
        quiet=True,
    )


def test_requeued_completing_job_survives_node_removal(
    tmp_path, epilog_proceed, stop_removed_slurmd
):
    """A requeued completing job keeps its nodes accounted across a restart."""

    slurm_user = atf.properties["slurm-user"]

    # The Epilog blocks until released, so the job stays COMPLETING while we
    # requeue it, drop a node and restart slurmctld.
    epilog_script = str(tmp_path / "epilog.sh")
    atf.make_bash_script(
        epilog_script,
        f"""while [ ! -f {epilog_proceed} ]; do sleep 0.5; done
""",
    )
    atf.set_config_parameter("Epilog", epilog_script)

    job_id = atf.submit_job_sbatch(
        "--requeue -N2 -o /dev/null --wrap 'sleep 300'", fatal=True
    )
    assert atf.wait_for_job_state(
        job_id, "RUNNING", timeout=60
    ), f"Job {job_id} never started"

    node_list = atf.get_job_parameter(job_id, "NodeList", fatal=True)
    allocated_nodes = atf.node_range_to_list(node_list)
    assert len(allocated_nodes) == 2, f"Expected 2 allocated nodes, got {node_list}"
    victim_node = allocated_nodes[-1]

    # Requeue while running. The blocked Epilog holds the job in COMPLETING,
    # which is where it must still be when slurmctld comes back.
    atf.run_command(f"scontrol requeue {job_id}", user=slurm_user, fatal=True)
    assert atf.wait_for_job_state(
        job_id, "COMPLETING", timeout=60
    ), f"Job {job_id} did not enter COMPLETING after being requeued"

    # Only the recovery pass should be visible to the assertions below.
    atf.run_command(f"truncate -s 0 {_logfile()}", user=slurm_user, fatal=True)

    # Removing an allocated node is what makes _sync_jobs_to_conf() fail the
    # job on the next startup.
    _remove_node_from_conf(victim_node)
    stop_removed_slurmd(victim_node)
    atf.restart_slurmctld()

    # Regression: with the completing flag dropped, _sync_nodes_to_comp_job()
    # skipped the job entirely and never logged this.
    assert atf.repeat_until(
        lambda: _log_grep(f"JobId={job_id} in completing state"),
        lambda output: output != "",
        timeout=60,
    ), f"slurmctld did not recover Job {job_id} as completing after the restart"

    # The documented invariant: while its Epilog runs, the surviving node is
    # COMPLETING and takes no new allocations.
    assert atf.wait_for_node_state(
        allocated_nodes[0], "COMPLETING", timeout=60
    ), f"Node {allocated_nodes[0]} was not completing after the restart"

    # Release the Epilog so the surviving node reports its epilog complete.
    atf.run_command(f"touch {epilog_proceed}", fatal=True)

    # The job was requeued, so it settles back as PENDING once the last epilog
    # complete clears the flag.
    assert atf.wait_for_job_state(
        job_id, "PENDING", timeout=120
    ), f"Job {job_id} did not settle as PENDING after the Epilog was released"

    # The positive consequence of a correctly decremented comp_job_cnt: the
    # node stops completing and can be scheduled on again.
    assert atf.wait_for_node_state(
        allocated_nodes[0], "COMPLETING", timeout=120, reverse=True
    ), f"Node {allocated_nodes[0]} never left COMPLETING"

    # Regression: each epilog complete landed on a node whose comp_job_cnt was
    # never incremented, because the sync above had skipped it.
    underflows = _log_grep("comp_job_cnt underflow")
    assert underflows == "", f"slurmctld logged comp_job_cnt underflow:\n{underflows}"
