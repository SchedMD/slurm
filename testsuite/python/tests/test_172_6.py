############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/kafka explicit per-event topics and topic_all precedence."""

import json
import logging
import random

import pytest

import atf

kafka = pytest.importorskip("kafka")

logging.getLogger("kafka").setLevel(logging.WARNING)

bootstrap_servers = ["localhost:9092"]
run_id = random.randrange(0, 999999999)
# Every event but start has its own topic option, so topic_all, which only
# catches the events without one, must receive the start records and nothing
# else. test_172_4 covers topic_all alone receiving every event, and
# test_172_2 an explicit topic_job_start.
topics = {
    "finish": f"slurm-qa-{run_id}-finish",
    "queue": f"slurm-qa-{run_id}-queue",
    "hold": f"slurm-qa-{run_id}-hold",
    "release": f"slurm-qa-{run_id}-release",
    "requeue": f"slurm-qa-{run_id}-requeue",
    "all": f"slurm-qa-{run_id}-all",
}
routes = {
    "job finish": topics["finish"],
    "job queue": topics["queue"],
    "job start": topics["all"],
    "job hold": topics["hold"],
    "job release": topics["release"],
    "job requeue": topics["requeue"],
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: jobcomp job state transition events require 26.11+",
    )
    atf.require_build_config("HAVE_RDKAFKA")
    try:
        kafka.KafkaConsumer(bootstrap_servers=bootstrap_servers).close()
    except kafka.errors.NoBrokersAvailable as e:
        pytest.skip(f"Kafka server not available at localhost:9092: {e}")
    atf.require_config_parameter("JobCompType", "jobcomp/kafka")
    params = [f"topic={topics['finish']}", "enable_job_start"]
    for event in ("queue", "hold", "release", "requeue"):
        params.append(f"enable_job_{event}")
        params.append(f"topic_job_{event}={topics[event]}")
    # Last, so the explicit options before it must still take precedence
    params.append(f"topic_all={topics['all']}")
    atf.require_config_parameter("JobCompParams", ",".join(params))
    k_conf_path = atf.properties["slurm-config-dir"] + f"/slurm-qa-{run_id}.conf"
    atf.require_config_parameter("JobCompLoc", k_conf_path)
    atf.require_config_parameter_includes(
        "bootstrap.servers", "localhost:9092", source=f"slurm-qa-{run_id}"
    )
    atf.require_config_parameter_includes(
        "enable.idempotence", "true", source=f"slurm-qa-{run_id}"
    )
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


def read_records(topic_name, job_id):
    """Return the job's records on a topic, reading it to its end.

    Every partition is read from its beginning up to the end offset taken
    when the read starts, so an empty poll on a slow broker cannot cut the
    read short. A topic nothing was produced to does not exist yet and has
    no records.
    """
    consumer = kafka.KafkaConsumer(bootstrap_servers=bootstrap_servers)
    records = []
    try:
        partitions = consumer.partitions_for_topic(topic_name)
        if not partitions:
            return records
        assignment = [kafka.TopicPartition(topic_name, p) for p in partitions]
        consumer.assign(assignment)
        consumer.seek_to_beginning()
        ends = consumer.end_offsets(assignment)
        for t in atf.timer(quiet=True, fatal=True):
            for batch in consumer.poll(timeout_ms=1000).values():
                for message in batch:
                    data = json.loads(message.value.decode("utf-8"))
                    if data.get("jobid") == job_id:
                        records.append(data)
            if all(consumer.position(tp) >= ends[tp] for tp in assignment):
                break
    finally:
        consumer.close()
    return records


def wait_for_routed_records(job_id, expected_events):
    """Return the job's records per event once each reached its topic.

    Every topic of the configuration is read, topic_all included, and a
    record found on a topic its event is not routed to fails the test at
    once.
    """
    expected = {}
    for event in expected_events:
        expected[event] = expected.get(event, 0) + 1

    found = {}
    for t in atf.timer():
        found = {}
        for topic_name in set(topics.values()):
            for record in read_records(topic_name, job_id):
                assert (
                    routes.get(record.get("event")) == topic_name
                ), f"Record of job {job_id} on the wrong topic {topic_name}: {record}"
                found.setdefault(record["event"], []).append(record)
        if {event: len(records) for event, records in found.items()} == expected:
            break

    counts = {event: len(records) for event, records in found.items()}
    assert (
        counts == expected
    ), f"Job {job_id} records per event {counts}, expected {expected}"
    return found


def test_hold_and_release_routing():
    """Queue, hold, release, and finish each go to their explicit topic.

    The finish record goes to the topic option and the other three to their
    topic_job_* options, which take precedence over topic_all.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    found = wait_for_routed_records(
        job_id, ["job queue", "job hold", "job release", "job finish"]
    )
    (hold,) = found["job hold"]
    assert hold["priority"] == 0, f"Hold record: {hold}"
    (finish,) = found["job finish"]
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"


def test_requeue_routing():
    """A requeue goes to topic_job_requeue; both runs start and finish apart.

    The two start records share topic_all, start having no topic option of
    its own, and the two finish records the explicit finish topic, where the
    job id key keeps them in order: the requeued run first.
    """
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    found = wait_for_routed_records(
        job_id,
        [
            "job queue",
            "job start",
            "job finish",
            "job requeue",
            "job start",
            "job finish",
        ],
    )
    states = [finish["state"] for finish in found["job finish"]]
    assert states == [
        "REQUEUED",
        "CANCELLED",
    ], f"Finish records of the two runs: {states}"
    (requeue,) = found["job requeue"]
    assert requeue["state"] == "PENDING", f"Requeue record state: {requeue['state']}"
