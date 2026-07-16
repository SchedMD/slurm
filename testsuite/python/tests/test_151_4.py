############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Verify per-partition OverSubscribe job count for FORCE:<n> and YES:<n>.

The FORCE:<n>/YES:<n> count is tracked per partition with select/cons_tres,
while select/linear counts jobs per node across all partitions sharing it (see
doc/html/cons_tres_share.shtml). Each test branches on SelectType to exercise
the corresponding expected behavior.
"""

import pytest

import atf

select_type = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global select_type

    atf.require_config_parameter(
        "PartitionName",
        {
            "p1": {"Nodes": "ALL", "OverSubscribe": "YES", "Default": "YES"},
            "p2": {"Nodes": "ALL", "OverSubscribe": "YES:2"},
            "p3": {"Nodes": "ALL", "OverSubscribe": "FORCE:2"},
            "p4": {"Nodes": "ALL", "OverSubscribe": "FORCE:3"},
            "p5": {"Nodes": "ALL", "OverSubscribe": "YES:3"},
        },
    )

    # Allow more than 4 jobs in terms of memory
    atf.require_nodes(1, [("CPUs", 1), ("RealMemory", 2048)])
    atf.require_config_parameter("DefMemPerNode", 256)

    # Just to speed up the test:
    atf.require_config_parameter_includes("SchedulerParameters", ("bf_interval", 1))

    atf.require_slurm_running()

    select_type = atf.get_config_parameter("SelectType")


def test_oversubscribe_force_partition():
    """
    Test behavior when one partition has OverSubscribe=FORCE.

    select/cons_tres tracks the FORCE:2 count per partition independently,
    so up to 2 jobs from p3 can share the node alongside the p1 job.
    select/linear counts every job on the node, so the p1 job already consumes
    one of the 2 slots in p3.
    """
    # Submit first job to p1 (OverSubscribe=YES) with --oversubscribe
    job_id1 = atf.submit_job_sbatch(
        "--oversubscribe -p p1 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)
    node1 = atf.get_job_parameter(job_id1, "NodeList", fatal=True)

    # Submit second job to p3 (OverSubscribe=FORCE:2) *without* --oversubscribe
    # This job should run because p3 forces oversubscription
    job_id2 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)
    node2 = atf.get_job_parameter(job_id2, "NodeList", fatal=True)

    # Verify first two jobs are running on the same node
    assert (
        node1 == node2 and node1 is not None
    ), f"Jobs should run on the same node ({node1} vs {node2})"

    # Submit third job to p3 (OverSubscribe=FORCE:2) *without* --oversubscribe
    job_id3 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)

    if select_type == "select/linear":
        assert atf.wait_for_job_state(
            job_id3,
            "PENDING",
            "Resources",
        ), "Job3 should be PENDING due Resources with select/linear because node has already 2 jobs (regardless off the partition)"
    else:
        atf.wait_for_job_state(job_id3, "RUNNING", fatal=True)
        node3 = atf.get_job_parameter(job_id3, "NodeList", fatal=True)
        assert (
            node3 == node1
        ), f"Job 3 should run on the same node with cons_tres because FORCE:2 counts jobs independently per partition ({node1} vs {node3})"

        # Submit fourth job to p3 (OverSubscribe=FORCE:2) *without* --oversubscribe
        job_id4 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)
        assert atf.wait_for_job_state(
            job_id4,
            "PENDING",
            "Resources",
        ), "Job4 should be PENDING due Resources (p3's FORCE:2 reached)"

    # Submit a last job to p1 *without* --oversubscribe
    # This job should be PENDING because p1 is OverSubscribe=YES and doesn't
    # oversubscribe by default. The Reason is not Resources because jobs are
    # in different partitions.
    job_id5 = atf.submit_job_sbatch("-p p1 --wrap='sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id5,
        "PENDING",
    ), "Final p1 job should be PENDING (different partition, not oversubscribed)"


def test_oversubscribe_force_independent_partition_counts():
    """
    Two overlapping FORCE partitions enforce the colon count of the *submission*
    partition, not the higher or lower of the two (issue 19643 review question).

    p3 is FORCE:2 and p4 is FORCE:3 on the same node. select/cons_tres tracks
    each partition's count independently, so p3 places 2 jobs and p4 places 3
    more on the same node (5 total). select/linear counts every job on the node
    against the submission partition's limit, so once p3 fills the node to 2
    jobs a single p4 job (limit 3) still starts but the node total is capped
    at 3.
    """
    # Fill p3 (FORCE:2) to its limit
    p3_job1 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)
    p3_job2 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)
    atf.wait_for_job_state(p3_job1, "RUNNING", fatal=True)
    atf.wait_for_job_state(p3_job2, "RUNNING", fatal=True)
    node = atf.get_job_parameter(p3_job1, "NodeList", fatal=True)
    p3_job2_node = atf.get_job_parameter(p3_job2, "NodeList", fatal=True)
    assert (
        p3_job2_node == node and node is not None
    ), f"p3 jobs should share the same node ({node} vs {p3_job2_node})"

    # A 3rd p3 job exceeds p3's FORCE:2 and pends under either plugin
    p3_job3 = atf.submit_job_sbatch("-p p3 --wrap='sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        p3_job3, "PENDING", "Resources"
    ), "3rd p3 job should pend: p3's FORCE:2 is reached"

    # A p4 job starts on the same node under both plugins (cons_tres: p4's own
    # count is still 0; linear: 2 jobs on the node is below p4's cap of 3)
    p4_job1 = atf.submit_job_sbatch("-p p4 --wrap='sleep infinity'", fatal=True)
    atf.wait_for_job_state(p4_job1, "RUNNING", fatal=True)
    p4_job1_node = atf.get_job_parameter(p4_job1, "NodeList", fatal=True)
    assert (
        p4_job1_node == node
    ), f"p4 job should share the same node as the p3 jobs ({node} vs {p4_job1_node})"

    if select_type == "select/linear":
        # The node total (3) has reached p4's cap, so the next p4 job pends.
        # An overflow job blocked by an overlapping partition pends with a
        # partition-priority reason rather than "Resources", so only the state
        # is checked.
        p4_job2 = atf.submit_job_sbatch("-p p4 --wrap='sleep infinity'", fatal=True)
        assert atf.wait_for_job_state(
            p4_job2, "PENDING"
        ), "select/linear caps the node at the submission partition's limit (p4=3)"
    else:
        # cons_tres counts p4 independently, so 2 more p4 jobs run (5 on node)
        p4_job2 = atf.submit_job_sbatch("-p p4 --wrap='sleep infinity'", fatal=True)
        p4_job3 = atf.submit_job_sbatch("-p p4 --wrap='sleep infinity'", fatal=True)
        atf.wait_for_job_state(p4_job2, "RUNNING", fatal=True)
        atf.wait_for_job_state(p4_job3, "RUNNING", fatal=True)
        p4_job2_node = atf.get_job_parameter(p4_job2, "NodeList", fatal=True)
        p4_job3_node = atf.get_job_parameter(p4_job3, "NodeList", fatal=True)
        assert (
            p4_job2_node == node and p4_job3_node == node
        ), f"all 5 jobs should share the same node ({node} vs {p4_job2_node} vs {p4_job3_node})"

        # A 4th p4 job exceeds p4's FORCE:3 and pends, independent of p3 (it
        # pends with a partition-priority reason, not "Resources")
        p4_job4 = atf.submit_job_sbatch("-p p4 --wrap='sleep infinity'", fatal=True)
        assert atf.wait_for_job_state(
            p4_job4, "PENDING"
        ), "4th p4 job should pend: p4's FORCE:3 is reached independently of p3"


def test_oversubscribe_yes_independent_partition_counts():
    """
    Same as test_oversubscribe_force_independent_partition_counts but for YES
    partitions (p2=YES:2, p5=YES:3), where each job must opt into sharing with
    --oversubscribe.
    """
    # Fill p2 (YES:2) to its limit
    p2_job1 = atf.submit_job_sbatch(
        "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
    )
    p2_job2 = atf.submit_job_sbatch(
        "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(p2_job1, "RUNNING", fatal=True)
    atf.wait_for_job_state(p2_job2, "RUNNING", fatal=True)
    node = atf.get_job_parameter(p2_job1, "NodeList", fatal=True)
    p2_job2_node = atf.get_job_parameter(p2_job2, "NodeList", fatal=True)
    assert (
        p2_job2_node == node and node is not None
    ), f"p2 jobs should share the same node ({node} vs {p2_job2_node})"

    # A 3rd p2 job exceeds p2's YES:2 and pends under either plugin
    p2_job3 = atf.submit_job_sbatch(
        "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
    )
    assert atf.wait_for_job_state(
        p2_job3, "PENDING", "Resources"
    ), "3rd p2 job should pend: p2's YES:2 is reached"

    # A p5 job starts on the same node under both plugins (cons_tres: p5's own
    # count is still 0; linear: 2 jobs on the node is below p5's cap of 3)
    p5_job1 = atf.submit_job_sbatch(
        "--oversubscribe -p p5 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(p5_job1, "RUNNING", fatal=True)
    p5_job1_node = atf.get_job_parameter(p5_job1, "NodeList", fatal=True)
    assert (
        p5_job1_node == node
    ), f"p5 job should share the same node as the p2 jobs ({node} vs {p5_job1_node})"

    if select_type == "select/linear":
        # The node total (3) has reached p5's cap, so the next p5 job pends.
        # An overflow job blocked by an overlapping partition pends with a
        # partition-priority reason rather than "Resources", so only the state
        # is checked.
        p5_job2 = atf.submit_job_sbatch(
            "--oversubscribe -p p5 --wrap='sleep infinity'", fatal=True
        )
        assert atf.wait_for_job_state(
            p5_job2, "PENDING"
        ), "select/linear caps the node at the submission partition's limit (p5=3)"
    else:
        # cons_tres counts p5 independently, so 2 more p5 jobs run (5 on node)
        p5_job2 = atf.submit_job_sbatch(
            "--oversubscribe -p p5 --wrap='sleep infinity'", fatal=True
        )
        p5_job3 = atf.submit_job_sbatch(
            "--oversubscribe -p p5 --wrap='sleep infinity'", fatal=True
        )
        atf.wait_for_job_state(p5_job2, "RUNNING", fatal=True)
        atf.wait_for_job_state(p5_job3, "RUNNING", fatal=True)
        p5_job2_node = atf.get_job_parameter(p5_job2, "NodeList", fatal=True)
        p5_job3_node = atf.get_job_parameter(p5_job3, "NodeList", fatal=True)
        assert (
            p5_job2_node == node and p5_job3_node == node
        ), f"all 5 jobs should share the same node ({node} vs {p5_job2_node} vs {p5_job3_node})"

        # A 4th p5 job exceeds p5's YES:3 and pends, independent of p2 (it
        # pends with a partition-priority reason, not "Resources")
        p5_job4 = atf.submit_job_sbatch(
            "--oversubscribe -p p5 --wrap='sleep infinity'", fatal=True
        )
        assert atf.wait_for_job_state(
            p5_job4, "PENDING"
        ), "4th p5 job should pend: p5's YES:3 is reached independently of p2"


def test_oversubscribe_yes_partition():
    """
    Same as test_oversubscribe_force_partition but with OverSubscribe=YES:2.
    Unlike FORCE, YES requires each job to opt into sharing with --oversubscribe.
    """
    # Submit first job to p1 (OverSubscribe=YES) with --oversubscribe
    job_id1 = atf.submit_job_sbatch(
        "--oversubscribe -p p1 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id1, "RUNNING", fatal=True)
    node1 = atf.get_job_parameter(job_id1, "NodeList", fatal=True)

    # Submit second job to p2 (OverSubscribe=YES:2) *with* --oversubscribe
    # This job should run and share the node since both jobs opted into sharing
    job_id2 = atf.submit_job_sbatch(
        "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id2, "RUNNING", fatal=True)
    node2 = atf.get_job_parameter(job_id2, "NodeList", fatal=True)

    # Verify first two jobs are running on the same node
    assert (
        node1 == node2 and node1 is not None
    ), f"Jobs should run on the same node ({node1} vs {node2})"

    # Submit third job to p2 (OverSubscribe=YES:2) *with* --oversubscribe
    job_id3 = atf.submit_job_sbatch(
        "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
    )
    if select_type == "select/linear":
        assert atf.wait_for_job_state(
            job_id3,
            "PENDING",
            "Resources",
        ), "Job3 should be PENDING due Resources with select/linear because node has already 2 jobs (regardless off the partition)"
    else:
        atf.wait_for_job_state(job_id3, "RUNNING", fatal=True)
        node3 = atf.get_job_parameter(job_id3, "NodeList", fatal=True)

        assert (
            node3 == node1
        ), f"Job 3 should run on the same node with cons_tres because YES:2 counts jobs independently per partition ({node1} vs {node3})"

        # Submit fourth job to p2 (OverSubscribe=YES:2) *with* --oversubscribe
        job_id4 = atf.submit_job_sbatch(
            "--oversubscribe -p p2 --wrap='sleep infinity'", fatal=True
        )
        assert atf.wait_for_job_state(
            job_id4,
            "PENDING",
            "Resources",
        ), "Job4 should be PENDING due Resources (p2's YES:2 reached)"

    # Submit a last job to p1 *without* --oversubscribe
    # This job should be PENDING because p1 is OverSubscribe=YES and doesn't
    # oversubscribe by default. The Reason is not Resources because jobs are
    # in different partitions.
    job_id5 = atf.submit_job_sbatch("-p p1 --wrap='sleep infinity'", fatal=True)
    assert atf.wait_for_job_state(
        job_id5,
        "PENDING",
    ), "Final p1 job should be PENDING (different partition, not oversubscribed)"
