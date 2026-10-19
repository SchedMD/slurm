############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test CLONE_NEWIPC private IPC namespace in namespace/linux.

Two jobs run at the same time on one node. Namespace ids and mount device
numbers only compare within one kernel, and a job that ends releases its
namespace id for reuse, so both jobs must be alive on the same node for the
comparisons to mean anything. Both jobs run on a node with room for two
jobs at once.
"""

import re

import pytest

import atf

namespace_yaml = """
---
defaults:
  auto_base_path: true
  base_path: "/tmp/test_159_3_ns_%n"
  dirs: "/var/tmp,/dev/shm"
  clone_ns_flags:
    - "CLONE_NEWIPC"
    - "CLONE_NEWPID"
    - "CLONE_NEWUSER"
..."""


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    global test_node

    atf.require_version(
        (25, 11),
        component="bin/scontrol",
        reason="Issue 50542: NamespaceType added in 25.11+",
    )
    atf.require_version(
        (26, 11),
        component="sbin/slurmd",
        reason="CLONE_NEWIPC support in clone_ns_flags added in 26.11+",
    )
    # Whole-node allocation leaves no room for a second job on the node.
    if atf.get_config_parameter("SelectType", live=False) == "select/linear":
        pytest.skip(
            "This test needs two jobs on one node, which select/linear cannot do"
        )

    atf.require_config_parameter("NamespaceType", "namespace/linux")
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    # Both jobs must fit on the node at once, a core and memory each.
    test_node = atf.require_nodes(1, [("Cores", 2), ("RealMemory", 100)])[0]
    atf.require_config_file("namespace.yaml", namespace_yaml)
    atf.require_slurm_running()

    yield

    atf.run_command(
        "rm -rf --one-file-system /tmp/test_159_3_ns_*",
        user="root",
        fatal=True,
        quiet=True,
    )


@pytest.fixture(scope="function")
def first_job():
    """Hold a job open and return the node it runs on and what it sees.

    The job reports its node, its IPC namespace, a System V message queue it
    makes, the queues it can see, and its /dev/mqueue device number.
    """
    output_file = "first_job.out"
    job_id = atf.submit_job_sbatch(
        f"-w {test_node} -n1 --mem=10 -o {output_file} "
        "--wrap 'command -v ipcmk >/dev/null && command -v ipcs >/dev/null "
        '&& echo HAVE_TOOLS=yes; queue_id=$(ipcmk -Q 2>/dev/null | grep -o "[0-9]\\+$"); '
        "echo NODE=$SLURMD_NODENAME; "
        "echo IPC_NS=$(readlink /proc/self/ns/ipc); "
        "echo QUEUE_ID=$queue_id; "
        'echo MQUEUE_DEV=$(stat -Lc "%d" /dev/mqueue 2>/dev/null); '
        "echo QUEUES; ipcs -q 2>/dev/null; echo READY; sleep infinity'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.assert_file_contents(output_file, "READY", contains=True)

    output = atf.run_command_output(f"cat {output_file}", fatal=True)
    readings = dict(
        line.split("=", 1) for line in output.splitlines() if re.match(r"\w+=", line)
    )
    readings["job_id"] = job_id

    if "HAVE_TOOLS" not in readings:
        pytest.skip(f"This test needs ipcmk and ipcs on {readings.get('NODE')}")

    # echo always emits the key, so require a value: an empty reading would
    # silently satisfy the inequality assertions in the tests below.
    assert readings.get("NODE"), f"First job reported no node name: {output!r}"
    assert re.fullmatch(
        r"ipc:\[\d+\]", readings.get("IPC_NS", "")
    ), f"First job reported no IPC namespace: {output!r}"
    assert re.fullmatch(
        r"\d+", readings.get("QUEUE_ID", "")
    ), f"First job reported no queue id: {output!r}"

    # A queue the first job cannot see itself would make the second job's
    # empty listing meaningless.
    own_queues = output.split("QUEUES", 1)[1]
    assert queue_listed(
        readings["QUEUE_ID"], own_queues
    ), f"First job cannot see its own message queue {readings['QUEUE_ID']}"

    return readings


def run_in_second_job(first_job, command):
    """Return the output of a command run in a second job on the same node."""
    return atf.run_job_output(
        f"-w {first_job['NODE']} -n1 --mem=10 {command}", fatal=True
    ).strip()


def queue_listed(queue_id, ipcs_output):
    """Return True when ipcs output lists the given message queue id."""
    return bool(
        re.search(rf"^\s*\S+\s+{re.escape(queue_id)}\s", ipcs_output, re.MULTILINE)
    )


def assert_first_job_still_running(first_job):
    """Fail if the first job ended before the second job looked at it."""
    state = atf.get_job_parameter(
        first_job["job_id"], "JobState", fatal=True, quiet=True
    )
    assert state == "RUNNING", f"First job left the RUNNING state ({state})"


def test_jobs_get_private_ipc_namespaces(first_job):
    """Two jobs running together get an IPC namespace each"""
    host_ipc_ns = atf.run_command_output(
        "readlink /proc/self/ns/ipc",
        fatal=True,
    ).strip()
    second_ipc_ns = run_in_second_job(first_job, "readlink /proc/self/ns/ipc")
    assert_first_job_still_running(first_job)

    assert (
        first_job["IPC_NS"] != second_ipc_ns
    ), "Both jobs are running in the same IPC namespace"
    # Two jobs differing from each other does not mean either differs from
    # the host, which is what the docs promise.
    assert (
        first_job["IPC_NS"] != host_ipc_ns
    ), "First job is running in the host IPC namespace"
    assert (
        second_ipc_ns != host_ipc_ns
    ), "Second job is running in the host IPC namespace"


def test_sysv_queue_not_shared(first_job):
    """A System V message queue made in one job is invisible in the other"""
    second_job_queues = run_in_second_job(first_job, "ipcs -q")
    assert_first_job_still_running(first_job)

    assert not queue_listed(
        first_job["QUEUE_ID"], second_job_queues
    ), f"Second job can see message queue {first_job['QUEUE_ID']} of the first job"


def test_mqueue_not_shared(first_job):
    """Each job gets its own /dev/mqueue mount"""
    if not first_job.get("MQUEUE_DEV"):
        pytest.skip(f"This test needs /dev/mqueue on {first_job['NODE']}")

    second_mqueue_dev = run_in_second_job(first_job, "stat -Lc '%d' /dev/mqueue")
    assert_first_job_still_running(first_job)

    assert (
        first_job["MQUEUE_DEV"] != second_mqueue_dev
    ), "Both jobs share one /dev/mqueue mount"
