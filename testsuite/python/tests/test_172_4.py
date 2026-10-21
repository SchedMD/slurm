############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/kafka job state transition event records."""

import json
import logging
import random
import time

import pytest

import atf

kafka = pytest.importorskip("kafka")

logging.getLogger("kafka").setLevel(logging.WARNING)

bootstrap_servers = ["localhost:9092"]
# Every event goes to the same topic through topic_all. The plugin keys
# messages by job id, so all records of a job land on one partition, and
# enable.idempotence keeps retries from reordering them, so they arrive in the
# order they were produced, which is what lets the tests check sequences. The
# topic gets several partitions so a record keyed differently would land on
# another one and be noticed.
topic = f"slurm-qa-{random.randrange(0, 999999999)}-events"
partitions = 4
requeue_exit_code = 7
requeue_exit_hold_code = 8
# Written into the script and the comment of the job whose records must
# carry them
script_marker = "jobcomp_script_marker"
comment_marker = "jobcomp_comment_marker"
# The fields a record of any event may carry, as documented in
# jobcomp_kafka.html; the queue, hold, release, and requeue records carry
# nothing else
common_fields = set(
    "event @event_time jobid sluid original_sluid restart_cnt cluster username "
    "user_id groupname group_id account partition qos job_name state "
    "state_reason state_desc priority @submit @eligible array_job_id "
    "array_task_id het_job_id het_job_offset".split()
)
# The common fields every record carries whatever the job; account, cluster,
# and qos only come with accounting, the others with the job's state
required_fields = set(
    "event @event_time jobid sluid restart_cnt username user_id groupname "
    "group_id partition job_name state @submit".split()
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 11),
        component="sbin/slurmctld",
        reason="Issue 51114: jobcomp job state transition events require 26.11+",
    )
    atf.require_build_config("HAVE_RDKAFKA")
    try:
        admin = kafka.admin.KafkaAdminClient(bootstrap_servers=bootstrap_servers)
    except kafka.errors.NoBrokersAvailable as e:
        pytest.skip(f"Kafka server not available at localhost:9092: {e}")
    admin.create_topics(
        [kafka.admin.NewTopic(topic, num_partitions=partitions, replication_factor=1)]
    )
    admin.close()
    atf.require_config_parameter("JobCompType", "jobcomp/kafka")
    params = [f"topic_all={topic}", "send_script", "send_comment"]
    for event in ("start", "queue", "hold", "release", "requeue"):
        params.append(f"enable_job_{event}")
    atf.require_config_parameter("JobCompParams", ",".join(params))
    k_conf_path = atf.properties["slurm-config-dir"] + f"/{topic}.conf"
    atf.require_config_parameter("JobCompLoc", k_conf_path)
    atf.require_config_parameter_includes(
        "bootstrap.servers", "localhost:9092", source=topic
    )
    atf.require_config_parameter_includes("enable.idempotence", "true", source=topic)
    atf.require_config_parameter("RequeueExit", str(requeue_exit_code))
    atf.require_config_parameter("RequeueExitHold", str(requeue_exit_hold_code))
    # A requeued job is only eligible again after requeue_delay, which defaults
    # to the AuthInfo cred_expire (120 seconds), and is only started by the
    # next scheduling cycle. The (key, value) form replaces a value the site
    # config already sets.
    atf.require_config_parameter_includes("SchedulerParameters", ("requeue_delay", 0))
    atf.require_config_parameter_includes("SchedulerParameters", ("sched_interval", 1))
    atf.require_slurm_running()


def read_records(match):
    """Return the (message, record) pairs on the topic whose record matches.

    Reads every partition from its beginning up to the end offset taken when
    the read starts, so a quiet topic does not block the read and an active
    one is not cut short by an empty poll.
    """
    consumer = kafka.KafkaConsumer(bootstrap_servers=bootstrap_servers)
    found = []
    try:
        assignment = [
            kafka.TopicPartition(topic, p) for p in consumer.partitions_for_topic(topic)
        ]
        consumer.assign(assignment)
        consumer.seek_to_beginning()
        ends = consumer.end_offsets(assignment)
        for t in atf.timer(quiet=True, fatal=True):
            for batch in consumer.poll(timeout_ms=1000).values():
                for message in batch:
                    record = json.loads(message.value.decode("utf-8"))
                    if match(record):
                        found.append((message, record))
            if all(consumer.position(tp) >= ends[tp] for tp in assignment):
                break
    finally:
        consumer.close()
    return found


def wait_for_records(job_id, expected_events):
    """Return the job's records once it has one per expected event.

    Fails if the records do not show up in time, if their event names do not
    match expected_events in order, or if they are not all keyed by the job
    id and on the single partition that key selects.
    """
    found = []
    for t in atf.timer():
        found = read_records(lambda record: record.get("jobid") == job_id)
        if len(found) >= len(expected_events):
            break

    events = [record.get("event") for message, record in found]
    assert (
        events == expected_events
    ), f"Job {job_id} produced the events {events}, expected {expected_events}"
    keys = {message.key for message, record in found}
    assert keys == {
        str(job_id).encode()
    }, f"Records of job {job_id} should be keyed by its id, got {keys}"
    used_partitions = {message.partition for message, record in found}
    assert (
        len(used_partitions) == 1
    ), f"Records of job {job_id} should share one partition, got {used_partitions}"
    return [record for message, record in found]


def wait_for_second_after(local_time):
    """Wait until the local clock is past the second of a scontrol time."""
    for t in atf.timer(fatal=True):
        if time.strftime("%Y-%m-%dT%H:%M:%S") > local_time:
            break


def assert_light(record):
    """Check a transition record carries only the documented common fields.

    It must also carry the common fields every record has, so a field the
    light records lose shows up as well as one they gain.
    """
    extra = sorted(set(record) - common_fields)
    assert (
        not extra
    ), f"Transition record should carry only the common fields, has {extra}: {record}"
    missing = sorted(required_fields - set(record))
    assert not missing, f"Transition record is missing {missing}: {record}"


def assert_full(record):
    """Check a start or finish record carries the request and allocation fields."""
    missing = [k for k in ("nodes", "total_cpus", "time_limit") if k not in record]
    assert not missing, f"Full record is missing {missing}: {record}"


def test_submit_held_then_release():
    """A job submitted held is only queued; its release is a transition.

    The hold reason of a job submitted with --hold is set when the job is
    created, not through a hold transition, so the queue record reports it
    held and no hold record follows. Releasing it is recorded, then the job
    runs to completion. Only the start and finish records carry the script
    and the comment.
    """
    job_id = atf.submit_job_sbatch(
        f"--hold -t1 -o /dev/null --comment={comment_marker} "
        f"--wrap 'true # {script_marker}'",
        fatal=True,
    )
    assert (
        atf.get_job_parameter(job_id, "Reason", fatal=True) == "JobHeldUser"
    ), f"Job {job_id} submitted with --hold should be pending as JobHeldUser"
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    queue, release, start, finish = wait_for_records(
        job_id, ["job queue", "job release", "job start", "job finish"]
    )
    assert queue["state"] == "PENDING", f"Queue record state: {queue['state']}"
    assert queue["priority"] == 0, f"Queue record of a held job: {queue}"
    assert (
        queue["state_reason"] == "JobHeldUser"
    ), f"Queue record of a held job: {queue}"
    assert release["priority"] > 0, f"Release record: {release}"
    assert (
        "state_reason" not in release
    ), f"Release record should carry no reason: {release}"
    assert start["state"] in (
        "RUNNING",
        "CONFIGURING",
    ), f"Start record state: {start['state']}"
    assert finish["state"] == "COMPLETED", f"Finish record state: {finish['state']}"
    assert_light(queue)
    assert_light(release)
    assert_full(start)
    assert_full(finish)
    for record in (start, finish):
        assert script_marker in record.get(
            "script", ""
        ), f"{record['event']} record should carry the script: {record}"
        assert (
            record.get("comment") == comment_marker
        ), f"{record['event']} record should carry the comment: {record}"


def test_hold_and_release_pending_job():
    """Holding a pending job and releasing it produce one record each.

    Repeating the hold or the release does not change the job, so neither
    adds a record.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {job_id}", fatal=True)
    # Only the records matter here, not the exit status of the repetition
    atf.run_command(f"scontrol hold {job_id}")
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.run_command(f"scontrol release {job_id}")
    atf.cancel_jobs([job_id], fatal=True)

    queue, hold, release, finish = wait_for_records(
        job_id, ["job queue", "job hold", "job release", "job finish"]
    )
    assert hold["state"] == "PENDING", f"Hold record state: {hold['state']}"
    assert hold["priority"] == 0, f"Hold record: {hold}"
    assert hold["state_reason"] in (
        "JobHeldUser",
        "JobHeldAdmin",
    ), f"Hold record reason: {hold.get('state_reason')}"
    assert release["priority"] > 0, f"Release record: {release}"
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"
    assert_light(hold)


@pytest.mark.parametrize(
    "hold_command,reason",
    [
        ("scontrol hold {job_id}", "JobHeldAdmin"),
        ("scontrol uhold {job_id}", "JobHeldUser"),
        ("scontrol update JobId={job_id} Priority=0", "JobHeldAdmin"),
    ],
    ids=["hold", "uhold", "priority"],
)
def test_hold_by_an_administrator(hold_command, reason):
    """Each way an administrator holds a job records the reason it sets.

    scontrol hold and a priority update to 0 by an administrator hold the job
    as JobHeldAdmin, while scontrol uhold holds it as JobHeldUser on behalf of
    the user. Each produces one hold record carrying that reason, and the
    release by the administrator one release record.
    """
    job_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.wait_for_job_state(job_id, "PENDING", fatal=True)
    atf.run_command(
        hold_command.format(job_id=job_id),
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"scontrol release {job_id}", user=atf.properties["slurm-user"], fatal=True
    )
    atf.cancel_jobs([job_id], fatal=True)

    queue, hold, release, finish = wait_for_records(
        job_id, ["job queue", "job hold", "job release", "job finish"]
    )
    assert hold["priority"] == 0, f"Hold record: {hold}"
    assert (
        hold["state_reason"] == reason
    ), f"Hold record reason: {hold.get('state_reason')}, expected {reason}"
    assert release["priority"] > 0, f"Release record: {release}"
    assert (
        "state_reason" not in release
    ), f"Release record should carry no reason: {release}"
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"
    assert_light(hold)


def test_hold_by_reservation_end():
    """A reservation that ends holds its pending jobs with a hold record.

    A reservation that reaches its end with jobs still pending is purged by
    the periodic reservation check and Slurm holds those jobs. A hold applied
    by Slurm itself is a transition like a user hold: the record carries the
    state, reason, and description of the hold.
    """
    resv_name = f"resv_172_4_{random.randrange(0, 999999)}"
    # A reservation with pending jobs cannot be deleted, so it has to end on
    # its own. Pending jobs do not keep it from being purged once it has.
    atf.run_command(
        f"scontrol create reservationname={resv_name} starttime=now "
        f"endtime=now+20seconds users={atf.get_user_name()} nodecnt=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    job_id = atf.submit_job_sbatch(
        f"--reservation={resv_name} --begin=now+1hour -t1 -o /dev/null --wrap true",
        fatal=True,
    )
    # The purge runs with the job time limit checks, every 30 seconds, and
    # scontrol shows the hold flag as the job state
    atf.wait_for_job_state(job_id, "RESV_DEL_HOLD", timeout=90, fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    queue, hold, finish = wait_for_records(
        job_id, ["job queue", "job hold", "job finish"]
    )
    assert hold["priority"] == 0, f"Hold record: {hold}"
    assert hold["state"] == "RESV_DEL_HOLD", f"Hold record state: {hold['state']}"
    assert (
        hold["state_reason"] == "ReservationDeleted"
    ), f"Hold record reason: {hold.get('state_reason')}"
    assert (
        hold["state_desc"] == f"Reservation {resv_name} was deleted"
    ), f"Hold record description: {hold.get('state_desc')}"
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"
    assert_light(hold)


def test_requeue_running_job():
    """Requeuing a running job ends its run and queues a new one.

    The run that ended is reported by a finish record saying REQUEUED, then
    the requeue record carries the new SLUID and submit time of the job,
    which the records of the second run share, and the job starts again.
    """
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    # @submit has a one second resolution, so the requeue has to land in a
    # later second than the submission for its record to show a new time
    wait_for_second_after(atf.get_job_parameter(job_id, "SubmitTime", fatal=True))
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    queue, start1, finish1, requeue, start2, finish2 = wait_for_records(
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
    assert (
        finish1["state"] == "REQUEUED"
    ), f"Finish record of the requeued run: {finish1['state']}"
    assert requeue["state"] == "PENDING", f"Requeue record state: {requeue['state']}"
    assert requeue["priority"] > 0, f"Requeue record of a runnable job: {requeue}"
    assert (
        requeue["sluid"] != finish1["sluid"]
    ), f"Requeue record should carry a new SLUID: {requeue['sluid']}"
    assert (
        start2["sluid"] == requeue["sluid"] and finish2["sluid"] == requeue["sluid"]
    ), f"Second run records should carry the requeue SLUID: {requeue['sluid']}"
    assert (
        requeue["@submit"] > finish1["@submit"]
    ), f"Requeue record should carry a new submit time: {requeue['@submit']}"
    assert (
        finish2["state"] == "CANCELLED"
    ), f"Finish record of the second run: {finish2['state']}"
    assert (
        requeue["restart_cnt"] == finish1["restart_cnt"] + 1
    ), f"Requeue record should carry the next run number: {requeue['restart_cnt']}"
    assert (
        start2["restart_cnt"] == requeue["restart_cnt"]
    ), f"Second run should keep the requeue run number: {start2['restart_cnt']}"
    assert_light(requeue)


def test_requeue_hold_running_job():
    """A hold applied by the requeue shows in the requeue record.

    scontrol requeuehold must not produce a hold record: the requeue record
    itself reports the job held, with the reason and a description of the
    requeue. Releasing it afterwards is a normal release.
    """
    job_id = atf.submit_job_sbatch(
        "--requeue -t5 -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.run_command(f"scontrol requeuehold {job_id}", fatal=True)
    # The hold is reported by the requeue record, which only exists once the
    # requeue has completed, so wait for it before releasing the job.
    wait_for_records(job_id, ["job queue", "job start", "job finish", "job requeue"])
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    atf.cancel_jobs([job_id], fatal=True)

    queue, start1, finish1, requeue, release, start2, finish2 = wait_for_records(
        job_id,
        [
            "job queue",
            "job start",
            "job finish",
            "job requeue",
            "job release",
            "job start",
            "job finish",
        ],
    )
    assert (
        finish1["state"] == "REQUEUED"
    ), f"Finish record of the requeued run: {finish1['state']}"
    assert requeue["priority"] == 0, f"Requeue record of a held job: {requeue}"
    assert (
        requeue["state_reason"] == "JobHeldUser"
    ), f"Requeue record reason: {requeue.get('state_reason')}"
    assert requeue.get(
        "state_desc"
    ), f"Requeue record should describe the hold: {requeue}"
    assert release["priority"] > 0, f"Release record: {release}"
    assert (
        finish2["state"] == "CANCELLED"
    ), f"Finish record of the second run: {finish2['state']}"
    assert_light(requeue)


def test_requeue_exit():
    """A RequeueExit requeue follows a finish record with the final state.

    The exit code is evaluated after the job completes, so the first run ends
    with a FAILED finish record that is only later followed by the requeue
    record. The second run completes normally.
    """
    marker = "requeue_exit_marker"
    atf.make_bash_script(
        "requeue_exit.sh",
        f"""
        if [ ! -f {marker} ]; then
            touch {marker}
            exit {requeue_exit_code}
        fi
        exit 0
        """,
    )
    job_id = atf.submit_job_sbatch(
        "--requeue -t1 -o /dev/null requeue_exit.sh", fatal=True
    )

    queue, start1, finish1, requeue, start2, finish2 = wait_for_records(
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
    assert (
        finish1["state"] == "FAILED"
    ), f"Finish record of the first run: {finish1['state']}"
    assert finish1["exit_code"] == f"{requeue_exit_code}:0", (
        f"Finish record of the first run should carry exit code "
        f"{requeue_exit_code}: {finish1['exit_code']}"
    )
    assert requeue["state"] == "PENDING", f"Requeue record state: {requeue['state']}"
    assert (
        finish2["state"] == "COMPLETED"
    ), f"Finish record of the second run: {finish2['state']}"


def test_requeue_exit_hold():
    """A RequeueExitHold requeue holds the job in the SPECIAL_EXIT state.

    The requeue record reports the hold applied by the requeue: the job is
    pending with no priority, in the SPECIAL_EXIT state, and with the user
    hold reason. No hold record precedes it. Releasing the job is a normal
    release and the second run completes.
    """
    marker = "requeue_exit_hold_marker"
    atf.make_bash_script(
        "requeue_exit_hold.sh",
        f"""
        if [ ! -f {marker} ]; then
            touch {marker}
            exit {requeue_exit_hold_code}
        fi
        exit 0
        """,
    )
    job_id = atf.submit_job_sbatch(
        "--requeue -t1 -o /dev/null requeue_exit_hold.sh", fatal=True
    )
    queue, start1, finish1, requeue = wait_for_records(
        job_id, ["job queue", "job start", "job finish", "job requeue"]
    )
    assert (
        finish1["state"] == "FAILED"
    ), f"Finish record of the first run: {finish1['state']}"
    assert (
        requeue["state"] == "SPECIAL_EXIT"
    ), f"Requeue record state: {requeue['state']}"
    assert requeue["priority"] == 0, f"Requeue record of a held job: {requeue}"
    assert (
        requeue["state_reason"] == "JobHeldUser"
    ), f"Requeue record reason: {requeue.get('state_reason')}"

    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    *_, release, start2, finish2 = wait_for_records(
        job_id,
        [
            "job queue",
            "job start",
            "job finish",
            "job requeue",
            "job release",
            "job start",
            "job finish",
        ],
    )
    assert release["priority"] > 0, f"Release record: {release}"
    assert (
        finish2["state"] == "COMPLETED"
    ), f"Finish record of the second run: {finish2['state']}"


def test_requeue_completed_job():
    """Requeuing a completed job runs it again after its finish record.

    scontrol requeue is accepted for a batch job that already completed, so
    a finish record saying COMPLETED can still be followed by a requeue
    record and a second run. The requeue record carries the next run number
    and a new SLUID.
    """
    job_id = atf.submit_job_sbatch("--requeue -t1 -o /dev/null --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    queue, start1, finish1 = wait_for_records(
        job_id, ["job queue", "job start", "job finish"]
    )
    assert (
        finish1["state"] == "COMPLETED"
    ), f"Finish record of the first run: {finish1['state']}"

    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    *_, requeue, start2, finish2 = wait_for_records(
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
    assert requeue["state"] == "PENDING", f"Requeue record state: {requeue['state']}"
    assert (
        requeue["restart_cnt"] == finish1["restart_cnt"] + 1
    ), f"Requeue record should carry the next run number: {requeue['restart_cnt']}"
    assert (
        requeue["sluid"] != finish1["sluid"]
    ), f"Requeue record should carry a new SLUID: {requeue['sluid']}"
    assert (
        finish2["state"] == "COMPLETED"
    ), f"Finish record of the second run: {finish2['state']}"


def test_array_submitted_held():
    """A job array is queued once as a whole; its tasks are recorded apart.

    The queue and release records stand for the whole task set: they carry
    the array job id and no task id. Each task then produces its own start
    and finish records with its task id. The tasks have their own job ids,
    so their records are found through the array job id.
    """
    task_count = 4
    job_id = atf.submit_job_sbatch(
        f"--array=0-{task_count - 1} --hold -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.run_command(f"scontrol release {job_id}", fatal=True)

    expected = {
        "job queue": 1,
        "job release": 1,
        "job start": task_count,
        "job finish": task_count,
    }
    found = []
    for t in atf.timer():
        found = read_records(lambda record: record.get("array_job_id") == job_id)
        if len(found) >= sum(expected.values()):
            break
    by_event = {}
    for message, record in found:
        by_event.setdefault(record["event"], []).append(record)
    counts = {event: len(records) for event, records in by_event.items()}
    assert counts == expected, f"Array {job_id} records per event: {counts}"

    for record in by_event["job queue"] + by_event["job release"]:
        assert (
            record["jobid"] == job_id
        ), f"Record of the whole array should carry its id: {record}"
        assert (
            "array_task_id" not in record
        ), f"Record of the whole array should carry no task id: {record}"
    (queue,) = by_event["job queue"]
    assert queue["priority"] == 0, f"Queue record of a held array: {queue}"
    assert (
        queue["state_reason"] == "JobHeldUser"
    ), f"Queue record of a held array: {queue}"
    (release,) = by_event["job release"]
    assert release["priority"] > 0, f"Release record: {release}"
    assert_light(queue)
    assert_light(release)
    for event in ("job start", "job finish"):
        task_ids = sorted(record.get("array_task_id") for record in by_event[event])
        assert task_ids == list(
            range(task_count)
        ), f"{event} records should cover every task: {task_ids}"
    for finish in by_event["job finish"]:
        assert finish["state"] == "COMPLETED", f"Task finish record: {finish}"


def test_array_cancelled_whole():
    """A held job array cancelled as a whole finishes as a whole.

    No task split off, so the single job record of the array gets the
    finish record, which carries the array job id and no task id like the
    queue record before it.
    """
    job_id = atf.submit_job_sbatch(
        "--array=0-3 --hold -t1 -o /dev/null --wrap true", fatal=True
    )
    atf.cancel_jobs([job_id], fatal=True)

    queue, finish = wait_for_records(job_id, ["job queue", "job finish"])
    assert (
        finish["array_job_id"] == job_id
    ), f"Finish record of the whole array should carry its id: {finish}"
    assert (
        "array_task_id" not in finish
    ), f"Finish record of the whole array should carry no task id: {finish}"
    assert finish["state"] == "CANCELLED", f"Finish record state: {finish['state']}"
    assert_light(queue)


def test_allocation_started_at_submit():
    """An allocation that starts at submit time is still queued first.

    salloc and srun allocations can be started inside the submit RPC, so
    the queue record has to be sent before the nodes are selected or it
    would follow the start record and report the job as running.
    """
    job_id = atf.submit_job_salloc("-t1 -n1 true", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    queue, start, finish = wait_for_records(
        job_id, ["job queue", "job start", "job finish"]
    )
    assert queue["state"] == "PENDING", f"Queue record state: {queue['state']}"
    assert start["state"] in (
        "RUNNING",
        "CONFIGURING",
    ), f"Start record state: {start['state']}"
    assert finish["state"] == "COMPLETED", f"Finish record state: {finish['state']}"
    assert_light(queue)


def test_immediate_allocation_rejected():
    """An immediate allocation that cannot start is queued, then finished.

    salloc --immediate is rejected inside the submit RPC when the job cannot
    start right away, here because its begin time has not come, but the job
    entered the queue first. Accounting keeps such jobs, and so does jobcomp:
    a queue record followed by a FAILED finish record. The rejected salloc
    prints no job id, so the records are found by the job name.
    """
    name = f"immediate_{random.randrange(0, 999999999)}"
    atf.run_command(
        f"salloc --immediate --begin=now+1hour -t1 -J {name} true",
        xfail=True,
        fatal=True,
    )

    found = []
    for t in atf.timer():
        found = read_records(lambda record: record.get("job_name") == name)
        if found:
            break
    assert found, f"The rejected job {name} should have left records"
    job_id = found[0][1]["jobid"]
    queue, finish = wait_for_records(job_id, ["job queue", "job finish"])
    assert queue["state"] == "PENDING", f"Queue record state: {queue['state']}"
    assert finish["state"] == "FAILED", f"Finish record state: {finish['state']}"
    assert (
        finish["state_reason"] == "BadConstraints"
    ), f"Finish record reason: {finish.get('state_reason')}"
    assert_light(queue)


def test_will_run_leaves_no_records():
    """A --test-only request leaves no record, even one Slurm holds.

    sbatch --test-only creates a job record only to find out when the job
    would start and purges it right after, so the job never enters the queue.
    A begin time past the end of the job's reservation makes Slurm hold that
    throwaway job as well, and that hold must not reach a record either. A
    job run afterwards marks how far the records have been produced.
    """
    name = f"test_only_{random.randrange(0, 999999999)}"
    resv_name = f"resv_172_4_{random.randrange(0, 999999)}"
    atf.run_command(
        f"scontrol create reservationname={resv_name} starttime=now "
        f"duration=1 users={atf.get_user_name()} nodecnt=1",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    # Only the records matter here, not what the requests report
    atf.run_command(f"sbatch --test-only -J {name} -t1 -o /dev/null --wrap true")
    atf.run_command(
        f"sbatch --test-only --reservation={resv_name} --begin=now+10minutes "
        f"-J {name} -t1 -o /dev/null --wrap true"
    )
    atf.run_command(
        f"scontrol delete reservationname={resv_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    job_id = atf.submit_job_sbatch("-t1 -o /dev/null --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    wait_for_records(job_id, ["job queue", "job start", "job finish"])
    found = read_records(lambda record: record.get("job_name") == name)
    assert not found, f"--test-only requests left records: {[r for m, r in found]}"


def test_hetjob_components():
    """Each component of a heterogeneous job is recorded as its own job.

    The components have their own job ids, so each one gets its own queue
    record, carrying the het job id of the leader and its offset. Holding,
    releasing, and cancelling the leader applies to every component, and
    each one records the transition on its own.
    """
    leader_id = atf.submit_job_sbatch(
        "--begin=now+1hour -t1 -o /dev/null -N1 : -N1 --wrap true", fatal=True
    )
    atf.wait_for_job_state(leader_id, "PENDING", fatal=True)
    atf.run_command(f"scontrol hold {leader_id}", fatal=True)
    atf.run_command(f"scontrol release {leader_id}", fatal=True)
    atf.cancel_jobs([leader_id], fatal=True)

    expected_events = ["job queue", "job hold", "job release", "job finish"]
    components = 2
    found = []
    for t in atf.timer():
        found = read_records(lambda record: record.get("het_job_id") == leader_id)
        if len(found) >= components * len(expected_events):
            break
    component_ids = sorted({record["jobid"] for message, record in found})
    assert (
        len(component_ids) == components
    ), f"Het job {leader_id} should record {components} components: {component_ids}"
    offsets = []
    for component_id in component_ids:
        queue, hold, release, finish = wait_for_records(component_id, expected_events)
        assert (
            queue["het_job_id"] == leader_id
        ), f"Component {component_id} queue record: {queue}"
        offsets.append(queue["het_job_offset"])
        assert_light(queue)
        assert_light(hold)
        assert_light(release)
        assert hold["state_reason"] in (
            "JobHeldUser",
            "JobHeldAdmin",
        ), f"Component {component_id} hold record: {hold}"
        assert release["priority"] > 0, f"Component {component_id} release: {release}"
        assert (
            finish["state"] == "CANCELLED"
        ), f"Component {component_id} finish record: {finish}"
    assert offsets == list(
        range(components)
    ), f"Queue records should cover every component offset: {offsets}"
