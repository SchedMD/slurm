############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/kafka job start and end records on separate topics."""

import json
import logging
import random

import pytest

import atf

kafka = pytest.importorskip("kafka")

logging.getLogger("kafka").setLevel(logging.WARNING)
topic = f"slurm-qa-{random.randrange(0, 999999999)}-end"
topic_start = f"slurm-qa-{random.randrange(0, 999999999)}-start"


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (25, 5),
        component="sbin/slurmctld",
        reason="Issue 50120: jobcomp/kafka enable_job_start and topic_job_start require 25.05+",
    )
    try:
        kafka.KafkaConsumer(bootstrap_servers=["localhost:9092"]).close()
    except kafka.errors.NoBrokersAvailable as e:
        pytest.skip(f"Kafka server not available at localhost:9092: {e}")
    atf.require_config_parameter("JobCompType", "jobcomp/kafka")
    atf.require_config_parameter(
        "JobCompParams",
        f"topic={topic},enable_job_start,topic_job_start={topic_start}",
    )
    k_conf_path = atf.properties["slurm-config-dir"] + f"/{topic}.conf"
    atf.require_config_parameter("JobCompLoc", k_conf_path)
    atf.require_config_parameter_includes(
        "bootstrap.servers", "localhost:9092", source=topic
    )
    atf.require_slurm_running()


def test_kafka_basic_functionality():
    """Test that jobcomp/kafka tracks job start and termination"""

    job_id = atf.submit_job_sbatch(
        "--wrap 'while [ ! -f job_release ]; do sleep 1; done'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    try:
        consumer = kafka.KafkaConsumer(
            topic_start,
            bootstrap_servers=["localhost:9092"],
            group_id=None,
            auto_offset_reset="earliest",
            consumer_timeout_ms=30000,
        )
    except Exception as e:
        pytest.fail(f"Error connecting to Kafka: {e}")

    message = next(consumer, None)
    assert message is not None, f"No job start record received on topic {topic_start}"
    try:
        data = json.loads(message.value.decode("utf-8"))
    except Exception as e:
        pytest.fail(f"Failed to decode or parse JSON: {e}")
    assert job_id == data.get(
        "jobid"
    ), f"Expected jobid={job_id} in the job start record, got {data.get('jobid')}"
    assert (
        data.get("state") is None
    ), f"Expected no state in the job start record, got {data.get('state')}"

    atf.run_command("touch job_release", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    try:
        consumer = kafka.KafkaConsumer(
            topic,
            bootstrap_servers=["localhost:9092"],
            group_id=None,
            auto_offset_reset="earliest",
            consumer_timeout_ms=30000,
        )
    except Exception as e:
        pytest.fail(f"Error connecting to Kafka: {e}")

    message = next(consumer, None)
    assert message is not None, f"No job end record received on topic {topic}"
    try:
        data = json.loads(message.value.decode("utf-8"))
    except Exception as e:
        pytest.fail(f"Failed to decode or parse JSON: {e}")
    assert job_id == data.get(
        "jobid"
    ), f"Expected jobid={job_id} in the job end record, got {data.get('jobid')}"
    assert (
        data.get("state") == "COMPLETED"
    ), f"Expected state COMPLETED in the job end record, got {data.get('state')}"
