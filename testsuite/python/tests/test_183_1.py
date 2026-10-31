############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that relay_io/uds_json relays job and step output.

Relaying is supplementary: normal output handling is unchanged and the
relay takes a copy.

The site scraper is simulated by a process that binds the datagram socket
the daemons are configured to write to, and collects the JSON records it
receives, one per datagram.
"""

import os
from collections import Counter

import pytest
from relay_io_scraper import Scraper, make_socket_dir, remove_socket_dir

import atf

# the Unicode replacement character, which invalid input is rewritten to
REPLACEMENT = "\ufffd"

# what a NUL in task output is rewritten to, since a raw one would truncate the
# serialized record at the first of them
NUL_ESCAPE = "\\u0000"

# the kernel caps this, but ask for enough to ride out a burst
RCVBUF_SIZE = 64 << 20

# max_log_kbytes pinned in setup(), as bytes. This caps the task output one
# record carries; the identity fields and JSON escaping are on top of it, so a
# datagram is larger than this and by a margin that depends on what was
# written.
# Deliberately not the shipped default, so that a configuration that never
# reached the plugin would be caught rather than pass by matching it.
MAX_LOG_BYTES = 8 * 1024

# worst case a record can reach: every raw byte escaping to six characters,
# plus room for the identity fields
MAX_RECORD_BYTES = (MAX_LOG_BYTES * 6) + 1024

# 64KiB writes the flooding task makes when the relay is checked for
# fairness. Enough to keep the relay occupied while the other task writes,
# and well inside the 8MiB ring pinned in setup() so that nothing is evicted
# before it can be reassembled.
FLOOD_WRITES = 64

# Nothing larger than a record can arrive, so asking for more only makes the
# interpreter allocate and resize a buffer per datagram, in the loop that has
# to stay ahead of a flooding step. Keep one spare record of margin.
RECV_SIZE = MAX_RECORD_BYTES * 2


@pytest.fixture(scope="module", autouse=True)
def setup(scraper, socket_path):
    # gate on the client: --relay-io is an srun/sbatch/salloc option, and
    # clients are upgraded last, so slurmd's version does not imply theirs
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 51035: --relay-io added in 26.11"
    )
    atf.require_config_parameter("RelayIOPlugin", "uds_json")
    # the largest payload below is 1MiB, so pin headroom rather than rely on
    # whatever stepd_ring_mbytes happens to default to
    atf.require_config_parameter(
        "RelayIOParameters",
        f"socket={socket_path},stepd_ring_mbytes=8"
        f",max_log_kbytes={MAX_LOG_BYTES // 1024}",
    )
    # Enabled by default so that --relay-io=no has something to override.
    atf.require_config_parameter("RelayIODefaultEnabled", "yes")
    atf.require_nodes(1, [("CPUs", 4)])
    # Reduce the time it takes for a requeue job to start again
    atf.require_config_parameter_includes("AuthInfo", ("cred_expire", "5"))
    atf.require_config_parameter_includes(
        "SchedulerParameters", ("sched_interval", "1")
    )
    atf.require_slurm_running()

    # Prove relaying actually works before any test runs. The tests that
    # assert nothing was relayed cannot tell a working opt out from a relay
    # that is disabled or broken, and a plugin that fails to find a JSON
    # serializer disables relaying without failing the daemon, so without
    # this they would all pass against a dead relay.
    atf.run_job("--relay-io=yes -N1 -n1 true", fatal=True)
    scraper.wait_for("job_end")
    scraper.clear()


@pytest.fixture(scope="module")
def socket_path():
    """Path slurmd is told to relay to.

    In a directory owned by root and writable by no one else, which is what
    relay_io/uds_json requires of the directory holding the socket. Whoever
    is bound there receives the output of every relayed job on the node.

    Kept directly under /tmp rather than in the pytest supplied temporary
    directory: the test user owns that one, and its path can overflow
    sockaddr_un.sun_path.
    """
    directory = make_socket_dir("out")
    yield f"{directory}/relay.sock"
    remove_socket_dir(directory)


@pytest.fixture(scope="module")
def scraper(socket_path):
    """A stand in for the site's log scraper."""
    scraper = Scraper(socket_path, recv_size=RECV_SIZE, rcvbuf=RCVBUF_SIZE)

    yield scraper

    scraper.stop()


@pytest.fixture(autouse=True)
def clean_records(scraper):
    scraper.clear()
    yield


def logs_of(records, stream=None):
    """Return the log records, optionally only those of one stream."""
    return [
        r
        for r in records
        if r["type"] == "log" and (stream is None or r["stream"] == stream)
    ]


def check_common_fields(record):
    """Every record carries the documented identity, whatever its type."""
    assert record["v"] == 1, f"schema version should be 1: {record}"
    # nanoseconds since the epoch, as a number rather than a formatted stamp
    assert isinstance(
        record["time"], int
    ), f"time should be an integer: {record['time']!r}"
    assert (
        record["time"] > 1_700_000_000_000_000_000
    ), f"time should be nanoseconds since the epoch: {record['time']}"

    for field in ("cluster", "node", "job_id", "sluid", "user_name"):
        assert field in record["slurm"], f"slurm.{field} should be set: {record}"

    # Presence is not attribution: a record naming the wrong job or user is the
    # one failure a consumer of the aggregate log cannot detect for itself.
    assert record["slurm"]["cluster"] == atf.get_config_parameter(
        "ClusterName"
    ), f"the record should name this cluster: {record}"
    assert (
        record["slurm"]["user_name"] == atf.get_user_name()
    ), f"the record should name the submitting user: {record}"
    assert record["slurm"]["job_id"] > 0, f"job_id should be a real id: {record}"
    assert record["slurm"]["sluid"], f"sluid should not be empty: {record}"

    # these describe the whole job, so they ride on the job events only
    job_event = record["type"] in ("job_start", "job_end")
    assert (
        "restart_count" in record["slurm"]
    ) == job_event, f"restart_count belongs on job events only: {record}"
    if job_event:
        assert (
            "partition" in record["slurm"]
        ), f"a job event should name its partition: {record}"
    else:
        assert (
            "partition" not in record["slurm"]
        ), f"partition belongs on job events only: {record}"
        assert (
            "account" not in record["slurm"]
        ), f"account belongs on job events only: {record}"


def test_srun_output_is_relayed_and_still_displayed(scraper):
    """srun output is relayed in addition to being returned to srun."""
    # both streams, since srun returns them separately
    result = atf.run_job(
        "--relay-io=yes -N1 -n2 "
        "bash -c 'echo out-$SLURM_PROCID; echo err-$SLURM_PROCID >&2'",
        fatal=True,
    )
    out, err = result["stdout"], result["stderr"]

    for task in (0, 1):
        assert (
            f"out-{task}" in out
        ), f"relaying should not stop srun displaying output, got {out!r}"
        assert (
            f"err-{task}" in err
        ), f"relaying should not stop srun displaying stderr, got {err!r}"

    records = scraper.wait_for("job_end")
    for record in records:
        check_common_fields(record)

    # Counted rather than looked for: a duplicate job_start, or a step_end
    # per task, would pass a membership test and break a consumer that pairs
    # them up.
    types = [r["type"] for r in records]
    counts = Counter(types)
    for event in ("job_start", "step_start", "step_end", "job_end"):
        assert (
            counts[event] == 1
        ), f"expected exactly one {event} record, got {counts[event]}: {types}"

    # Not asserted: that job_start precedes step_start, or step_end precedes
    # job_end. slurmd emits the job events and slurmstepd the step events,
    # each through its own ring and socket, and the guide promises ordering
    # only within one stream of one task.

    bodies = {(r["stream"], r["body"]) for r in logs_of(records)}
    for task in (0, 1):
        assert (
            "stdout",
            f"out-{task}\n",
        ) in bodies, f"stdout of task {task} should be relayed: {bodies}"
        assert (
            "stderr",
            f"err-{task}\n",
        ) in bodies, f"stderr of task {task} should be relayed: {bodies}"

    # each record is attributed to the task that produced it
    for record in logs_of(records):
        task_id = record["slurm"]["task_id_global"]
        assert record["body"].endswith(
            f"-{task_id}\n"
        ), f"record attributed to the wrong task: {record}"


def test_default_enabled_relays_without_the_option(scraper):
    """RelayIODefaultEnabled=yes relays a job that says nothing either way."""
    output = atf.run_job_output("-N1 -n1 echo defaulted-output", fatal=True)

    assert (
        "defaulted-output" in output
    ), f"relaying should not stop srun displaying output, got {output!r}"

    records = scraper.wait_for("job_end")
    bodies = [r["body"] for r in logs_of(records, "stdout")]
    assert bodies == [
        "defaulted-output\n"
    ], f"the default should relay the job's output, got {bodies}"


def test_relay_io_no_leaves_output_alone(scraper):
    """--relay-io=no overrides RelayIODefaultEnabled=yes and keeps normal output."""
    output = atf.run_job_output("--relay-io=no -N1 -n1 echo normal-output", fatal=True)

    assert (
        "normal-output" in output
    ), f"srun should display output when not relaying, got {output!r}"

    assert (
        scraper.wait_for_quiet() == []
    ), "a job that opted out should produce no records"


def test_batch_output_is_relayed_and_written(scraper):
    """A relayed batch job still writes its output file, byte for byte."""
    output_file = os.path.abspath("relayed_batch.out")
    script = "relayed_batch.sh"
    atf.make_bash_script(script, "echo line1\necho line2\n")

    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -o {output_file} {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = scraper.wait_for("job_end")

    # exact, not contains: the file must be byte for byte what the job wrote
    atf.assert_file_contents(output_file, "line1\nline2\n", strip=False)

    step_ids = {r["slurm"]["step_id"] for r in records if "step_id" in r["slurm"]}
    assert step_ids == {
        "batch"
    }, f"batch step should be reported as 'batch', got {step_ids}"

    bodies = [r["body"] for r in logs_of(records, "stdout")]
    assert bodies == [
        "line1\n",
        "line2\n",
    ], f"batch output should be relayed one line per record, got {bodies}"

    # batch output is ordinary buffered output, not --unbuffered
    for record in logs_of(records):
        assert not record.get(
            "unbuffered"
        ), f"batch records should not be marked unbuffered: {record}"
        assert not record.get(
            "partial"
        ), f"a complete line should not be marked partial: {record}"


def test_batch_relay_io_no_writes_output_file(scraper):
    """--relay-io=no on a batch job overrides the default and writes its file."""
    output_file = os.path.abspath("normal_batch.out")
    script = "normal_batch.sh"
    atf.make_bash_script(script, "echo normal-batch\n")

    job_id = atf.submit_job_sbatch(
        f"--relay-io=no -o {output_file} {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents(output_file, "normal-batch", contains=True)

    assert (
        scraper.wait_for_quiet() == []
    ), "a job that opted out should produce no records"


def test_unbuffered_output_is_chunked(scraper):
    """--unbuffered output is relayed in fixed size chunks."""
    atf.run_job_output(
        "-u --relay-io=yes -N1 -n1 "
        "python3 -c 'import sys; sys.stdout.write(\"x\" * 2500)'",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    logs = logs_of(records, "stdout")

    assert logs, "unbuffered output should still be relayed"
    for record in logs:
        assert record.get(
            "unbuffered"
        ), f"records of an --unbuffered step should say so: {record}"

    assert (
        "".join(r["body"] for r in logs) == "x" * 2500
    ), "the chunks should reassemble to exactly what the task wrote"

    # Chunks are 1KiB, and the remainder is flushed rather than held back, so
    # the split depends on how the task's writes land in slurmstepd's reads.
    # Only the documented part is asserted: none over 1KiB.
    lengths = [len(r["body"]) for r in logs]
    assert sum(lengths) == 2500, f"expected 2500 bytes in total, got {lengths}"
    assert all(
        n <= 1024 for n in lengths
    ), f"no unbuffered chunk may exceed 1KiB, got {lengths}"
    # partial describes a line that was cut short, and unbuffered records have
    # no lines to be short of
    for record in logs:
        assert not record.get(
            "partial"
        ), f"an unbuffered record should not be marked partial: {record}"


def test_line_without_newline_is_marked_partial(scraper):
    """Output with no trailing newline is still relayed, and flagged."""
    atf.run_job_output("--relay-io=yes -N1 -n1 printf no-newline", fatal=True)

    records = scraper.wait_for("job_end")
    logs = logs_of(records, "stdout")

    assert len(logs) == 1, f"expected one record, got {logs}"
    assert logs[0]["body"] == "no-newline", f"body should be relayed intact: {logs[0]}"
    assert logs[0].get(
        "partial"
    ), f"output without a trailing newline should be marked partial: {logs[0]}"


def test_invalid_utf8_is_replaced(scraper):
    """Arbitrary bytes are made safe rather than emitted as invalid JSON."""
    # --output=none keeps the raw byte out of srun's stdout, which the harness
    # decodes as UTF-8; it does not affect what is relayed
    atf.run_job_output(
        "--relay-io=yes -N1 -n1 --output=none " "bash -c 'printf \"bad\\xffbyte\\n\"'",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    logs = logs_of(records, "stdout")

    assert len(logs) == 1, f"expected one record, got {logs}"
    assert (
        logs[0]["body"] == f"bad{REPLACEMENT}byte\n"
    ), f"the invalid byte should become U+FFFD, got {logs[0]['body']!r}"


def test_max_log_kbytes_caps_the_output_a_record_carries(scraper):
    """A line longer than max_log_kbytes is split, not truncated."""
    # plain ASCII, so a byte of task output is a byte of record body and the
    # cap can be read straight off the records
    written = (MAX_LOG_BYTES * 2) + 500
    body, records = relay_python(scraper, f'"a" * {written}')

    logs = logs_of(records, "stdout")
    biggest = max(len(r["body"]) for r in logs)

    assert biggest <= MAX_LOG_BYTES, (
        f"no record may carry more than max_log_kbytes ({MAX_LOG_BYTES}) of "
        f"task output, largest carried {biggest}"
    )
    assert len(logs) >= 3, (
        f"{written} bytes capped at {MAX_LOG_BYTES} should need at least 3 "
        f"records, got {len(logs)}"
    )
    check_reassembles(body, "a" * written)


def test_null_bytes_do_not_truncate_a_record(scraper):
    """A NUL is escaped, so output after one is still relayed."""
    # --output=none keeps the raw byte out of srun's stdout, which the harness
    # decodes as UTF-8; it does not affect what is relayed
    body, _ = relay_python(scraper, '"before\\x00after\\n"')

    assert (
        "\x00" not in body
    ), f"a raw NUL should never reach the consumer, got {body!r}"
    assert (
        NUL_ESCAPE in body
    ), f"a NUL should be relayed as {NUL_ESCAPE!r}, got {body!r}"
    # the bug this guards: a raw NUL ends the serialized string early, so
    # everything the task wrote after it is silently lost
    assert (
        "after" in body
    ), f"output following a NUL should still be relayed, got {body!r}"
    assert body == f"before{NUL_ESCAPE}after\n", f"unexpected body {body!r}"


def test_many_null_bytes_are_relayed(scraper):
    """A body of NULs costs six bytes each and still reassembles."""
    written = 2000
    body, _ = relay_python(scraper, f'"\\x00" * {written}')

    assert "\x00" not in body, "a raw NUL should never reach the consumer"
    assert body == (
        NUL_ESCAPE * written
    ), f"expected {written} escaped NULs, got {len(body)} characters"


def test_labelled_output_is_relayed_unlabelled(scraper):
    """--label still labels what srun shows, and the relay carries the raw line."""
    output = atf.run_job_output(
        "--relay-io=yes --label -N1 -n2 bash -c 'echo lbl-$SLURM_PROCID'",
        fatal=True,
    )

    for task in (0, 1):
        assert (
            f"{task}: lbl-{task}" in output
        ), f"srun should still label its output, got {output!r}"

    records = scraper.wait_for("job_end")
    bodies = sorted(r["body"] for r in logs_of(records, "stdout"))
    assert bodies == [
        "lbl-0\n",
        "lbl-1\n",
    ], f"the label is srun's presentation, not part of the line: {bodies}"


def test_per_task_output_files_are_relayed_and_written(scraper):
    """--output=%t writes each task's file and relays it."""
    prefix = os.path.abspath("relayed_task")
    atf.run_job_output(
        f"--relay-io=yes -N1 -n3 --output={prefix}-%t.out "
        "bash -c 'echo task-$SLURM_PROCID'",
        fatal=True,
    )

    records = scraper.wait_for("job_end")

    for task in range(3):
        atf.assert_file_contents(f"{prefix}-{task}.out", f"task-{task}")

    relayed = sorted(
        (r["slurm"]["task_id_global"], r["body"]) for r in logs_of(records, "stdout")
    )
    assert relayed == [
        (0, "task-0\n"),
        (1, "task-1\n"),
        (2, "task-2\n"),
    ], f"every task should be relayed with its own id: {relayed}"


def test_output_none_is_relayed(scraper):
    """--output=none writes nothing locally but is still relayed."""
    output = atf.run_job_output(
        "--relay-io=yes --output=none -N1 -n1 echo discarded", fatal=True
    )

    assert (
        "discarded" not in output
    ), f"--output=none should show nothing, got {output!r}"

    records = scraper.wait_for("job_end")
    bodies = [r["body"] for r in logs_of(records, "stdout")]
    assert bodies == [
        "discarded\n"
    ], f"--output=none should still be relayed, got {bodies}"


def test_discarded_output_is_still_relayed(scraper):
    """Output the job discards locally is still relayed."""
    output = atf.run_job_output(
        "--relay-io=yes -N1 -n3 --output=1 bash -c 'echo from-task-$SLURM_PROCID'",
        fatal=True,
    )

    # --output=<taskid> sends every other task to /dev/null
    assert "from-task-1" in output, f"task 1 should reach srun, got {output!r}"
    for task in (0, 2):
        assert (
            f"from-task-{task}" not in output
        ), f"task {task} should not reach srun, got {output!r}"

    records = scraper.wait_for("job_end")
    logs = logs_of(records, "stdout")

    task_ids = sorted(r["slurm"]["task_id_global"] for r in logs)
    assert task_ids == [
        0,
        1,
        2,
    ], f"discarding output locally should not stop it being relayed, got {task_ids}"


def check_datagram_sizes(scraper):
    """No record grows without bound, whatever the task wrote."""
    sizes = scraper.get_sizes()
    assert sizes, "expected at least one record"
    assert max(sizes) <= MAX_RECORD_BYTES, (
        f"max_log_kbytes caps the output a record carries, so none may exceed "
        f"{MAX_RECORD_BYTES} bytes, largest was {max(sizes)}"
    )


def relay_python(scraper, expression, options=""):
    """Run a python expression as a job and return its reassembled stdout.

    Takes the pieces of a split line back to the single string the task wrote,
    which is what a scraper joining a partial chain would end up with.
    """
    # --output=none keeps megabytes of payload, including raw control bytes,
    # out of the test log; test_output_none_is_relayed proves it does not
    # affect what is relayed
    atf.run_job_output(
        f"--relay-io=yes -N1 -n1 --output=none {options} "
        f"python3 -c 'import sys; sys.stdout.write({expression})'",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    check_datagram_sizes(scraper)

    return "".join(r["body"] for r in logs_of(records, "stdout")), records


def check_reassembles(body, written):
    """Compare a rejoined partial chain against what the task wrote."""
    assert len(body) == len(written), (
        f"the records should reassemble to all {len(written)} characters "
        f"written, got {len(body)} - a short chain means a record was dropped"
    )
    assert body == written, "the records should reassemble to what was written"


def test_long_line_is_split_into_datagram_sized_records(scraper):
    """A line too long for one datagram is split rather than truncated."""
    written = "A" * (1 << 20) + "\n"
    body, records = relay_python(scraper, '"A" * (1 << 20) + "\\n"')

    check_reassembles(body, written)

    logs = logs_of(records, "stdout")
    assert (
        len(logs) > 1
    ), f"a line of {len(written)} bytes should need several records, got {len(logs)}"

    # every piece but the last continues into the next one
    assert all(
        r.get("partial") for r in logs[:-1]
    ), "every record but the last should be marked partial"
    assert not logs[-1].get(
        "partial"
    ), "the record holding the end of the line should not be marked partial"


@pytest.mark.parametrize(
    "expression, written",
    [
        # a control character costs six bytes as \uXXXX
        ('"\\x01" * 100000', "\x01" * 100000),
        # a quote or a backslash costs two
        ('"\\"\\\\" * 50000', '"\\' * 50000),
        # multi-byte characters are emitted verbatim, so they cost their length
        (
            '"\\u00e9\\u4e2d\\U0001f600" * 20000',
            "\u00e9\u4e2d\U0001f600" * 20000,
        ),
    ],
    # the payloads are megabytes long, and an id derived from one of them ends
    # up on a command line
    ids=["control-bytes", "quotes-and-backslashes", "multi-byte"],
)
def test_escaping_is_accounted_for_when_splitting(scraper, expression, written):
    """Expensive escaping reaches the consumer intact and still bounded.

    JSON escaping is not counted against max_log_kbytes, so it does not move
    where a line is split. What it does change is how large the resulting
    record is, which is why the bound checked here is the escaped one.
    """
    body, _ = relay_python(scraper, expression)

    check_reassembles(body, written)


def test_split_never_lands_inside_a_character(scraper):
    """Splitting for size does not cut a multi-byte character in half."""
    written = "\u00e9" * 100000
    body, _ = relay_python(scraper, '"\\u00e9" * 100000')

    assert (
        REPLACEMENT not in body
    ), "a character split for size should not become U+FFFD"
    check_reassembles(body, written)


def test_split_unbuffered_output_is_never_partial(scraper):
    """An unbuffered record says so instead of being marked partial."""
    written = "\x01" * 100000
    body, records = relay_python(scraper, '"\\x01" * 100000', options="-u")

    check_reassembles(body, written)

    logs = logs_of(records, "stdout")
    assert all(
        r.get("unbuffered") for r in logs
    ), "every record of an unbuffered step should be marked unbuffered"
    assert not any(
        r.get("partial") for r in logs
    ), "an unbuffered record is cut arbitrarily by definition, so never partial"


def test_long_final_line_without_newline_is_relayed_whole(scraper):
    """A long last line with no newline is relayed in full, not truncated."""
    written = "T" * 30000
    body, records = relay_python(scraper, '"T" * 30000')

    check_reassembles(body, written)
    assert logs_of(records, "stdout")[-1].get(
        "partial"
    ), "a final line with no newline should be marked partial"


def test_records_are_attributed_to_the_submitting_job(scraper):
    """Records name the job that produced them, not just some job."""
    job_id = atf.submit_job_sbatch(
        "--relay-io=yes -N1 -o /dev/null --wrap 'echo attributed'", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = scraper.wait_for("job_end")
    assert records, "expected records for the job"

    for record in records:
        assert (
            record["slurm"]["job_id"] == job_id
        ), f"every record should name job {job_id}: {record}"

    # one job has one sluid, and it rides on every record of that job
    sluids = {r["slurm"]["sluid"] for r in records}
    assert len(sluids) == 1, f"a job's records should share one sluid, got {sluids}"


def test_job_relay_io_propagates_to_its_steps(scraper):
    """A job's --relay-io reaches the srun steps inside it."""
    # --relay-io=no has to override RelayIODefaultEnabled=yes for the batch
    # step and for the srun step launched by the script
    job_id = atf.submit_job_sbatch(
        "--relay-io=no -N1 -o /dev/null "
        "--wrap 'echo from-batch; srun echo from-step'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = scraper.wait_for_quiet()
    bodies = [r["body"] for r in logs_of(records)]
    assert (
        not bodies
    ), f"--relay-io=no should reach the job's steps as well, got {bodies}"


def test_step_relay_io_is_ignored_inside_an_allocation(scraper):
    """--relay-io on an srun inside an allocation does not opt the step out."""
    # Relaying is a property of the job, so a step cannot opt itself out of a
    # relayed job. srun inside an allocation takes the job's value from the
    # allocation response and discards whatever was passed on its own command
    # line, so this step relays despite asking not to.
    job_id = atf.submit_job_sbatch(
        "--relay-io=yes -N1 -o /dev/null "
        "--wrap 'echo from-batch; srun --relay-io=no echo from-step'",
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = scraper.wait_for("job_end")
    step_ids = {r["slurm"]["step_id"] for r in logs_of(records)}
    bodies = [r["body"] for r in logs_of(records)]

    assert any(
        "from-batch" in b for b in bodies
    ), f"the batch step should be relayed, got {bodies}"
    assert any(
        "from-step" in b for b in bodies
    ), f"the step should be relayed despite --relay-io=no, got {bodies}"

    assert step_ids == {
        "batch",
        "0",
    }, f"both the batch step and the srun step should relay, got {step_ids}"


def test_unbuffered_split_keeps_characters_whole(scraper):
    """--unbuffered cuts at a fixed size, never inside a character."""
    # Chunking is by byte count, so every cut lands mid character in a stream
    # of multi-byte ones, including the cuts made as the stream ends.
    written = "\u00e9\u4e2d\U0001f600" * 20000
    body, _ = relay_python(scraper, '"\\u00e9\\u4e2d\\U0001f600" * 20000', options="-u")

    assert (
        REPLACEMENT not in body
    ), "a character cut by unbuffered chunking should not become U+FFFD"
    check_reassembles(body, written)


def test_long_line_is_relayed_before_the_task_ends(scraper):
    """A line too long for a record does not wait for a newline, or for exit."""
    script = "long_line.sh"
    # The task holds the line open with no newline, so anything relayed
    # arrived without one and without the stream ending.
    atf.make_bash_script(
        script,
        'python3 -c \'import sys; sys.stdout.write("Q" * 12000); '
        "sys.stdout.flush()'\n"
        "sleep infinity\n",
    )

    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -N1 -o /dev/null {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    for _ in atf.timer(fatal=True):
        if sum(len(r["body"]) for r in logs_of(scraper.get(), "stdout")) >= (
            MAX_LOG_BYTES
        ):
            break

    # the premise of the test: the task had not exited when those arrived
    assert (
        atf.get_job_parameter(job_id, "JobState", fatal=True) == "RUNNING"
    ), "the task should still be running, so the records cannot be an eof flush"

    for record in logs_of(scraper.get(), "stdout"):
        assert record.get(
            "partial"
        ), f"a line emitted before its newline should be partial: {record}"

    # leave nothing in flight to bleed into the next test
    atf.cancel_jobs([job_id], fatal=True)
    scraper.wait_for("job_end")


def test_output_is_held_rather_than_dropped_when_the_relay_falls_behind(scraper):
    """A task that outruns the relay is slowed, not truncated."""
    # Many times what the per task per stream buffer holds, written as fast as
    # the task can, so the relay spends most of the run behind. Refusing the
    # bytes instead of holding the task would cut at whatever byte the buffer
    # filled on, losing lines and splicing the remains of two together.
    lines = 50000
    written = "".join(f"line-{i:06d}\n" for i in range(lines))

    body, _ = relay_python(
        scraper, f'"".join("line-%06d\\n" % i for i in range({lines}))'
    )

    check_reassembles(body, written)

    # Named so that a failure says which lines went missing rather than only
    # that the lengths differ.
    relayed = body.splitlines()
    assert relayed == written.splitlines(), (
        f"every line should be relayed once and in order, got {len(relayed)} "
        f"of {lines}"
    )


def test_error_file_output_is_relayed_and_written(scraper):
    """--error sends stderr to a file, and it is still relayed."""
    script = "relay_error.sh"
    atf.make_bash_script(script, "echo to-the-error-file >&2\n")

    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -N1 -o /dev/null -e relay_error.out {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    atf.assert_file_contents("relay_error.out", "to-the-error-file", contains=True)

    records = scraper.wait_for("job_end")
    bodies = [r["body"] for r in logs_of(records, "stderr")]
    assert any(
        "to-the-error-file" in b for b in bodies
    ), f"stderr sent to a file should still be relayed, got {bodies}"


def test_salloc_relay_io_no_is_honored(scraper):
    """A step run inside an allocation relays under the job's setting.

    --relay-io=no rather than yes: RelayIODefaultEnabled is yes in this
    module, so yes is what the job would get anyway and salloc could drop
    or mis-parse the option without the test noticing.
    """
    output = atf.run_command_output(
        "salloc --relay-io=no -N1 srun -n1 echo from-the-allocation",
        fatal=True,
    )
    assert (
        "from-the-allocation" in output
    ), f"opting out should not stop the step displaying output, got {output!r}"

    records = scraper.wait_for_quiet()
    assert records == [], f"salloc --relay-io=no should relay nothing, got {records}"


def test_restart_count_tracks_a_requeue(scraper):
    """restart_count on the job events distinguishes one run from the next."""
    script = "relay_requeue.sh"
    atf.make_bash_script(script, "sleep 10\n")

    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -N1 -o /dev/null --requeue {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    scraper.wait_for("job_start")

    first = [
        r["slurm"]["restart_count"] for r in scraper.get() if r["type"] == "job_start"
    ]
    assert first, "the first run should emit a job_start"

    scraper.clear()
    atf.run_command(f"scontrol requeue {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    scraper.wait_for("job_start")

    second = [
        r["slurm"]["restart_count"] for r in scraper.get() if r["type"] == "job_start"
    ]
    assert second, "the requeued run should emit a job_start of its own"
    assert second[0] > first[0], (
        f"restart_count should rise across a requeue, so that a consumer can "
        f"tell the runs apart, got {first[0]} then {second[0]}"
    )


def test_scontrol_show_config_reports_the_relay_options(scraper):
    """The configured relay settings are visible in scontrol show config."""
    # values come back casefolded, so compare in lower case
    live = dict(live=True, source="scontrol")

    assert (
        atf.get_config_parameter("RelayIOPlugin", **live) == "uds_json"
    ), "scontrol show config should report the configured RelayIOPlugin"

    parameters = atf.get_config_parameter("RelayIOParameters", **live)
    assert (
        "max_log_kbytes" in parameters
    ), f"scontrol show config should report RelayIOParameters, got {parameters!r}"

    assert (
        atf.get_config_parameter("RelayIODefaultEnabled", **live) == "yes"
    ), "scontrol show config should report RelayIODefaultEnabled"


def held_relay_job(marker, options=""):
    """Submit a held job that echoes marker once it is released."""
    return atf.submit_job_sbatch(
        f'{options} --hold -N1 -n1 -o /dev/null --wrap "echo {marker}"',
        fatal=True,
    )


def release_and_wait(job_id):
    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)


def test_scontrol_update_job_turns_relaying_on(scraper):
    """RelayIO=yes overrides the --relay-io=no the job was submitted with."""
    # An explicit refusal to override, so the job is not merely agreeing
    # with RelayIODefaultEnabled once the update has been applied.
    job_id = held_relay_job("update-turned-it-on", "--relay-io=no")

    atf.run_command(f"scontrol update jobid={job_id} RelayIO=yes", fatal=True)
    release_and_wait(job_id)

    records = scraper.wait_for("job_end")
    assert any(
        "update-turned-it-on" in r["body"] for r in logs_of(records)
    ), f"the update should have turned relaying on, got {records}"


def test_scontrol_update_job_turns_relaying_off(scraper):
    """RelayIO=no overrides RelayIODefaultEnabled for one job."""
    job_id = held_relay_job("update-turned-it-off")

    atf.run_command(f"scontrol update jobid={job_id} RelayIO=no", fatal=True)
    release_and_wait(job_id)

    records = scraper.wait_for_quiet()
    assert records == [], f"the update should have turned relaying off, got {records}"


def test_scontrol_update_job_default_restores_the_configured_default(scraper):
    """RelayIO=default hands the job back to RelayIODefaultEnabled."""
    job_id = held_relay_job("update-restored-the-default", "--relay-io=no")

    # RelayIODefaultEnabled is yes here, so giving the job back to it undoes
    # the --relay-io=no rather than leaving the job where it was.
    atf.run_command(f"scontrol update jobid={job_id} RelayIO=default", fatal=True)
    release_and_wait(job_id)

    records = scraper.wait_for("job_end")
    assert any(
        "update-restored-the-default" in r["body"] for r in logs_of(records)
    ), f"RelayIO=default should relay under RelayIODefaultEnabled=yes, got {records}"


def test_scontrol_update_job_is_refused_once_the_job_is_running(scraper):
    """A running job's relaying cannot be changed, so its records stay paired.

    Relaying is settled when the job starts. Taking a new value afterwards
    would leave the relay holding a job_start whose job_end never matches,
    so the update is refused rather than silently ignored.
    """
    script = "relay_update_running.sh"
    atf.make_bash_script(script, "sleep 60\n")

    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -N1 -n1 -o /dev/null {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)
    scraper.wait_for("job_start")

    error = atf.run_command_error(f"scontrol update jobid={job_id} RelayIO=no")
    assert (
        "Job is no longer pending execution" in error
    ), f"a running job's RelayIO should not be updatable, got {error!r}"

    assert (
        atf.get_job_parameter(job_id, "RelayIO") == "YES"
    ), "the refused update should have left the running job relaying"

    atf.cancel_jobs([job_id])

    # the start the relay was given has to be followed by its end
    scraper.wait_for("job_end")


def test_scontrol_update_job_rejects_an_unknown_relay_io_value(scraper):
    """A value that is neither yes, no nor default is refused by the client."""
    job_id = held_relay_job("update-was-rejected", "--relay-io=no")

    error = atf.run_command_error(f"scontrol update jobid={job_id} RelayIO=maybe")
    assert (
        "Invalid RelayIO value" in error
    ), f"an unknown RelayIO value should be refused, got {error!r}"

    assert (
        atf.get_job_parameter(job_id, "RelayIO") is None
    ), "a refused update should have left the job alone"

    atf.cancel_jobs([job_id])


def test_scontrol_show_job_reports_relaying_only_when_it_is_on(scraper):
    """RelayIO=YES is shown for a relayed job, and nothing at all otherwise.

    The value slurmctld sends is already resolved, so a job that asked for
    nothing is reported against RelayIODefaultEnabled rather than leaving
    the reader to look the configuration up.
    """
    on = held_relay_job("show-job-on", "--relay-io=yes")
    off = held_relay_job("show-job-off", "--relay-io=no")
    default = held_relay_job("show-job-default")

    assert (
        atf.get_job_parameter(on, "RelayIO") == "YES"
    ), "--relay-io=yes should be reported as RelayIO=YES"
    assert (
        atf.get_job_parameter(off, "RelayIO") is None
    ), "a job that is not relayed should report no RelayIO at all"
    assert atf.get_job_parameter(default, "RelayIO") == "YES", (
        "a job that asked for nothing should be reported against "
        "RelayIODefaultEnabled, which is yes here"
    )

    atf.cancel_jobs([on, off, default])


def test_a_busy_stream_does_not_starve_a_quiet_one(scraper):
    """Output from a quiet task is relayed while another floods."""
    script = "starve.sh"
    started = os.path.abspath("flood-started")
    finished = os.path.abspath("flood-done")

    # The sentinels are what make the overlap certain rather than likely.
    # Task 1 writes nothing until the flood has begun and keeps writing
    # until it has ended, so its lines are written while the flood is in
    # flight however long the flood happens to take. Timing the two against
    # each other instead would pass or fail on how fast the node is.
    atf.make_bash_script(
        script,
        "if [ $SLURM_PROCID = 0 ]; then\n"
        f"  touch {started}\n"
        "  python3 -c 'import sys\n"
        f"for _ in range({FLOOD_WRITES}):\n"
        '    sys.stdout.write("A" * 65536)\n'
        "    sys.stdout.flush()'\n"
        f"  touch {finished}\n"
        "else\n"
        # spun on rather than slept on, so that the first line is written at
        # the start of the flood rather than up to a sleep after it
        f"  while [ ! -f {started} ]; do :; done\n"
        "  i=0\n"
        "  while true; do\n"
        "    i=$((i + 1))\n"
        "    echo QUIET-$i\n"
        f"    [ -f {finished} ] && break\n"
        "    sleep 0.01\n"
        "  done\n"
        "fi\n",
    )

    atf.run_job_output(f"--relay-io=yes -N1 -n2 --output=none {script}", fatal=True)

    records = scraper.wait_for("job_end")
    logs = logs_of(records, "stdout")

    busy = [i for i, r in enumerate(logs) if r["slurm"]["task_id_global"] == 0]
    quiet = [i for i, r in enumerate(logs) if r["slurm"]["task_id_global"] == 1]

    assert busy, "the flooding task's output should be relayed"
    assert quiet, "the quiet task's output should be relayed, not starved"

    # Interleaving is the point, and it has to be interleaving in the middle:
    # a quiet record that merely precedes the last flood record could have
    # been written before the flood began. Every quiet line here is written
    # inside the flood, so a relay that drained one source at a time would
    # put every one of their records after busy[-1].
    inside = [i for i in quiet if busy[0] < i < busy[-1]]
    assert inside, (
        "the quiet task's output should be relayed while the other floods, "
        f"got quiet records at {quiet} and the flood spanning "
        f"{busy[0]}..{busy[-1]}"
    )

    flooded = "".join(r["body"] for r in logs if r["slurm"]["task_id_global"] == 0)
    check_reassembles(flooded, "A" * (FLOOD_WRITES * 65536))


def test_an_absent_scraper_does_not_block_a_job(scraper):
    """A job finishes, with a complete output file, when nothing is bound.

    This is the headline promise: a slow or absent scraper never blocks a
    job, and records are lost rather than allowed to stall the job that
    produced them.
    """
    # More than the 8MiB ring pinned in setup(), so the relay has to evict
    # rather than hold the task back for a reader that is not there.
    lines = 1000000
    written = "".join(f"line-{i:06d}\n" for i in range(lines))
    output_file = os.path.abspath("no_scraper.out")
    script = "no_scraper.sh"
    atf.make_bash_script(
        script,
        "python3 -c 'import sys\n"
        f"for i in range({lines}):\n"
        '    sys.stdout.write("line-%06d\\n" % i)\'\n',
    )

    scraper.stop()
    scraper.remove_socket()
    try:
        job_id = atf.submit_job_sbatch(
            f"--relay-io=yes -o {output_file} {script}", fatal=True
        )
        # Bounded on purpose. The job is not waiting on anything but itself,
        # so one that needs longer than this was held up by the relay.
        atf.wait_for_job_state(job_id, "DONE", timeout=120, fatal=True)
    finally:
        # back before any assertion, so a failure here does not leave the
        # rest of the module without a scraper
        scraper.start()

    check_reassembles(
        atf.run_command_output(f"cat {output_file}", fatal=True, quiet=True),
        written,
    )

    # The writers reconnect on their own, so relaying resumes with nothing
    # reconfigured. slurmd may still be in its backoff, hence waiting on the
    # step rather than on job_start.
    atf.run_job("--relay-io=yes -N1 -n1 echo scraper-is-back", fatal=True)
    records = scraper.wait_for("step_end")
    bodies = [r["body"] for r in logs_of(records, "stdout")]
    assert any(
        "scraper-is-back" in b for b in bodies
    ), f"relaying should resume once the scraper is back, got {bodies}"


def slurmd_log_paths():
    """The slurmd log of each node, by node name.

    SlurmdLogFile may carry the %n placeholder. Auto-config leaves it unset,
    so the controller's log directory is the fallback, as atf derives it.
    The values are read raw, because get_config_parameter() casefolds them.
    """
    conf = atf.get_config(live=False, quiet=True)
    log_file = next(
        (v for k, v in conf.items() if k.casefold() == "slurmdlogfile"), None
    )
    if not log_file:
        log_dir = atf.properties.get("slurm-logs-dir") or os.path.dirname(
            next(v for k, v in conf.items() if k.casefold() == "slurmctldlogfile")
        )
        log_file = f"{log_dir}/slurmd.%n.log"

    return {node: log_file.replace("%n", node) for node in atf.get_nodes(quiet=True)}


def slurmd_log_sizes():
    """Each slurmd log's size, so that a later read sees only what follows.

    A node that has not logged anything yet has no file, and counts as 0.
    """
    sizes = {}
    for node, path in slurmd_log_paths().items():
        size = atf.run_command_output(
            f"stat -c %s {path}", user="root", quiet=True
        ).strip()
        sizes[node] = int(size) if size else 0
    return sizes


def wait_for_slurmd_log(offsets, message):
    """Wait for any slurmd to log message after its offset."""
    paths = slurmd_log_paths()
    for _ in atf.timer(fatal=True):
        if any(
            message
            in atf.run_command_output(
                f"tail -c +{offsets.get(node, 0) + 1} {path}",
                user="root",
                quiet=True,
            )
            for node, path in paths.items()
        ):
            break


def wait_for_relay_to_resume(scraper):
    """Wait until slurmd has delivered what it queued while it could not send.

    slurmd sends its records in order, so the job_end of a job run after the
    scraper is back comes after anything it was still holding.
    """
    job_id = atf.submit_job_sbatch(
        "--relay-io=yes -N1 -o /dev/null --wrap 'echo scraper-is-back'",
        fatal=True,
    )
    # long enough for slurmd's reconnect backoff, which caps at 30 seconds
    for _ in atf.timer(timeout=90, fatal=True):
        if any(
            (r["type"] == "job_end") and (r["slurm"]["job_id"] == job_id)
            for r in scraper.get()
        ):
            break


def test_a_socket_owned_by_another_user_is_refused(scraper):
    """Nothing is sent to a socket owned by other than root or SlurmdUser."""
    offsets = slurmd_log_sizes()
    scraper.stop()
    untrusted = Scraper(scraper.path, owner=os.getuid())
    try:
        atf.run_job("--relay-io=yes -N1 -n1 echo untrusted-socket", fatal=True)
        records = untrusted.wait_for_quiet()
        wait_for_slurmd_log(
            offsets, "not connecting, the relay_io socket is owned by uid"
        )
    finally:
        untrusted.stop()
        scraper.start()

    wait_for_relay_to_resume(scraper)

    assert (
        records == []
    ), f"a socket the test user owns should receive nothing, got {records}"


@pytest.mark.parametrize(
    "command,message",
    [
        ("chmod 0775", "has group or other write permissions"),
        ("chmod 0757", "has group or other write permissions"),
        ("chown {uid}", "is owned by uid"),
    ],
    ids=["group_writable", "other_writable", "user_owned"],
)
def test_an_untrusted_directory_is_refused(scraper, command, message):
    """Nothing is sent to a socket in a directory another user controls."""
    directory = os.path.dirname(scraper.path)
    offsets = slurmd_log_sizes()
    atf.run_command(
        f"{command.format(uid=os.getuid())} {directory}", user="root", fatal=True
    )
    try:
        # the daemons check before connecting, so make them reconnect
        scraper.stop()
        scraper.start()
        atf.run_job("--relay-io=yes -N1 -n1 echo untrusted-directory", fatal=True)
        records = scraper.wait_for_quiet()
        wait_for_slurmd_log(
            offsets,
            f"not connecting, the directory holding the relay_io socket {message}",
        )
    finally:
        atf.run_command(
            f"chown 0 {directory} && chmod 0755 {directory}", user="root", fatal=True
        )

    wait_for_relay_to_resume(scraper)

    assert (
        records == []
    ), f"a socket in an untrusted directory should receive nothing, got {records}"


def test_output_held_while_the_scraper_was_down_is_relayed(scraper):
    """A running step sends what it held once a restarted scraper is back."""
    done = os.path.abspath("ticks-done")
    output = os.path.abspath("ticks.out")
    script = "ticks.sh"
    atf.make_bash_script(
        script,
        "i=0\n"
        f"while [ ! -f {done} ]; do\n"
        "  i=$((i + 1))\n"
        "  echo TICK-$i\n"
        "  sleep 0.1\n"
        "done\n",
    )
    job_id = atf.submit_job_sbatch(
        f"--relay-io=yes -N1 -o {output} {script}", fatal=True
    )

    def relayed():
        return sorted(
            int(r["body"].strip()[len("TICK-") :])
            for r in logs_of(scraper.get(), "stdout")
            if r["slurm"]["job_id"] == job_id
        )

    def written():
        return len(atf.run_command_output(f"cat {output}", quiet=True).split())

    for _ in atf.timer(fatal=True):
        if relayed():
            break

    scraper.stop()
    down_at = written()
    for _ in atf.timer(fatal=True):
        if written() >= down_at + 20:
            break
    held = written()
    scraper.start()

    # the step's reconnect backoff doubles while the scraper is down
    for _ in atf.timer(timeout=60, fatal=True):
        if relayed() and (relayed()[-1] > held):
            break

    atf.run_command(f"touch {done}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    ticks = relayed()
    assert ticks == list(range(1, ticks[-1] + 1)), (
        f"every tick, including those written while the scraper was down, "
        f"should be relayed, got {ticks}"
    )
