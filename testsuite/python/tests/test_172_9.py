############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/kafka default topics of the queue, start, and release events."""

import json
import logging
import time

import pytest

import atf

kafka = pytest.importorskip("kafka")

logging.getLogger("kafka").setLevel(logging.WARNING)

# Only these events are enabled. The finish event is always on, and the
# others must stay silent whatever the job goes through. test_172_5 covers
# the hold and requeue events the same way.
enabled_events = ("queue", "start", "release")
disabled_events = ("hold", "requeue")
cluster = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global cluster
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: jobcomp job state transition events require 26.11+",
    )
    atf.require_build_config("HAVE_RDKAFKA")
    try:
        kafka.KafkaConsumer(bootstrap_servers=["localhost:9092"]).close()
    except kafka.errors.NoBrokersAvailable as e:
        pytest.skip(f"Kafka server not available at localhost:9092: {e}")
    atf.require_config_parameter("JobCompType", "jobcomp/kafka")
    # No topic names, so every event has to land on its default topic derived
    # from ClusterName, and only the enabled events may produce a record.
    atf.require_config_parameter(
        "JobCompParams", ",".join(f"enable_job_{event}" for event in enabled_events)
    )
    k_conf_path = atf.properties["slurm-config-dir"] + "/jobcomp_kafka_172_9.conf"
    atf.require_config_parameter("JobCompLoc", k_conf_path)
    atf.require_config_parameter_includes(
        "bootstrap.servers", "localhost:9092", source="jobcomp_kafka_172_9"
    )
    atf.require_config_parameter_includes(
        "enable.idempotence", "true", source="jobcomp_kafka_172_9"
    )
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()
    cluster = atf.get_config_parameter("ClusterName")


def topic_for(event):
    """Return the default topic of an event: ClusterName for finish."""
    if event == "finish":
        return cluster
    return f"{cluster}-job-{event}"


def utc_now():
    """Return the current UTC time in the format of the @event_time field."""
    return time.strftime("%FT%T", time.gmtime())


def read_records(topic_name, job_id, since):
    """Return the job's records on a topic produced at or after since.

    The default topics are shared with earlier runs, in which job ids are
    reused, so records older than the test are ignored. Every partition is
    read from its beginning up to the end offset taken when the read
    starts, so an empty poll on a slow broker cannot cut the read short.
    A topic nothing was produced to does not exist yet and has no records.
    """
    consumer = kafka.KafkaConsumer(bootstrap_servers=["localhost:9092"])
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
                    if (
                        data.get("jobid") == job_id
                        and data.get("@event_time", "") >= since
                    ):
                        records.append(data)
            if all(consumer.position(tp) >= ends[tp] for tp in assignment):
                break
    finally:
        consumer.close()
    return records


def wait_for_records(event, job_id, since, count):
    """Return the job's records on the event's topic once there are count.

    Also checks that every record on that topic is of that event, so a
    record routed to the wrong topic fails here.
    """
    topic_name = topic_for(event)
    records = []
    for t in atf.timer():
        records = read_records(topic_name, job_id, since)
        if len(records) >= count:
            break
    assert (
        len(records) == count
    ), f"Expected {count} records of job {job_id} on {topic_name}, got {records}"
    for record in records:
        assert (
            record.get("event") == f"job {event}"
        ), f"Record of a different event on {topic_name}: {record}"
    return records


def assert_no_records(job_id, since, events):
    """Check that none of the events' topics has a record of the job."""
    for event in events:
        records = read_records(topic_for(event), job_id, since)
        assert (
            records == []
        ), f"Job {job_id} should have no record on {topic_for(event)}: {records}"


def test_queue_and_release_routed_to_their_topics():
    """Queue and release land on their topics; the disabled hold leaves none."""
    since = utc_now()
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    (finish,) = wait_for_records("finish", job_id, since, 1)
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"
    (queue,) = wait_for_records("queue", job_id, since, 1)
    assert queue["state"] == "PENDING", f"Queue record state: {queue['state']}"
    (release,) = wait_for_records("release", job_id, since, 1)
    assert release["priority"] > 0, f"Release record: {release}"
    assert_no_records(job_id, since, disabled_events + ("start",))


def test_start_routed_to_its_topic():
    """Each run starts on the start topic; the disabled requeue leaves none."""
    since = utc_now()
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    finish1, finish2 = wait_for_records("finish", job_id, since, 2)
    assert (
        finish1["state"] == "REQUEUED"
    ), f"Finish record of the requeued run: {finish1['state']}"
    assert (
        finish2["state"] == "CANCELLED"
    ), f"Finish record of the second run: {finish2['state']}"
    start1, start2 = wait_for_records("start", job_id, since, 2)
    assert start2["sluid"] == finish2["sluid"], f"Second run start: {start2}"
    assert (
        start2["restart_cnt"] == start1["restart_cnt"] + 1
    ), f"Second run should carry the next run number: {start2['restart_cnt']}"
    (queue,) = wait_for_records("queue", job_id, since, 1)
    assert queue["state"] == "PENDING", f"Queue record state: {queue['state']}"
    assert_no_records(job_id, since, disabled_events + ("release",))
