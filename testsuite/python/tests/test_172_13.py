############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test jobcomp/elasticsearch records sent to JobCompLoc and kept on restart."""

import http.server
import json
import logging
import random
import threading

import pytest

import atf

index = "slurm_qa_jobcomp"
# The first send after slurmctld starts can wait about 30 seconds
record_timeout = 90
# Job names unique to this run, as slurmctld can resend records of old runs
token = random.randrange(0, 999999999)
received = []
server = None


@pytest.fixture(scope="module", autouse=True)
def setup(es_port):
    atf.require_build_config("HAVE_LIBCURL")
    atf.require_build_config("HAVE_JSON")

    atf.require_config_parameter("JobCompType", "jobcomp/elasticsearch")
    atf.require_config_parameter("JobCompLoc", f"127.0.0.1:{es_port}/{index}/_doc")
    # Job ids too long to match the small numbers in the record
    atf.require_config_parameter("FirstJobId", 1000000)
    # One node for a running job and one for the jobs that finish
    atf.require_nodes(2)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def es_port():
    """Run the HTTP server that receives the records."""
    yield start_server()
    stop_server()


class ElasticsearchHandler(http.server.BaseHTTPRequestHandler):
    """Store each request and answer like Elasticsearch on create."""

    protocol_version = "HTTP/1.1"

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        received.append((self.path, self.rfile.read(length)))
        self.send_response(201)
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def log_message(self, fmt, *args):
        logging.debug(fmt, *args)


def start_server(port=0):
    """Start the HTTP server on 127.0.0.1 and return its port."""
    global server

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), ElasticsearchHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1]


def stop_server():
    """Stop the HTTP server, so new connections are refused."""
    server.shutdown()
    server.server_close()


def run_job(name):
    """Run a batch job until it is done and return its id."""
    job_id = atf.submit_job_sbatch(f"-J {name} -o /dev/null --wrap true", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    return job_id


def get_paths(job_id, name):
    """Return the request path of each record with job_id and name as values."""
    paths = []
    for path, body in received:
        values = json.loads(body).values()
        if job_id in values and name in values:
            paths.append(path)
    return paths


def test_one_record_per_job():
    """slurmctld sends one record per finished job to JobCompLoc, none for others."""
    held_name = f"jobcomp_held_{token}"
    running_name = f"jobcomp_running_{token}"
    first_name = f"jobcomp_first_{token}"
    second_name = f"jobcomp_second_{token}"

    held_id = atf.submit_job_sbatch(
        f"-H -J {held_name} -o /dev/null --wrap true", fatal=True
    )
    running_id = atf.submit_job_sbatch(
        f"-J {running_name} -o /dev/null --wrap 'sleep infinity'", fatal=True
    )
    atf.wait_for_job_state(running_id, "RUNNING", fatal=True)
    first_id = run_job(first_name)
    second_id = run_job(second_name)

    for _ in atf.timer(timeout=record_timeout):
        first_paths = get_paths(first_id, first_name)
        second_paths = get_paths(second_id, second_name)
        if first_paths and second_paths:
            break

    assert first_paths == [
        f"/{index}/_doc"
    ], f"Job {first_id} should have one record sent to /{index}/_doc"
    assert second_paths == [
        f"/{index}/_doc"
    ], f"Job {second_id} should have one record sent to /{index}/_doc"
    assert not get_paths(
        held_id, held_name
    ), f"Pending job {held_id} should have no record"
    assert not get_paths(
        running_id, running_name
    ), f"Running job {running_id} should have no record"


def test_record_kept_on_restart(es_port):
    """Only the records not sent before slurmctld stops are sent after a restart."""
    sent_name = f"jobcomp_sent_{token}"
    kept_name = f"jobcomp_kept_{token}"

    sent_id = run_job(sent_name)
    for _ in atf.timer(timeout=record_timeout, fatal=True):
        if get_paths(sent_id, sent_name):
            break
    stop_server()
    kept_id = run_job(kept_name)
    assert not atf.stop_slurmctld(), "slurmctld should stop"
    start_server(es_port)
    atf.start_slurmctld()

    for _ in atf.timer(timeout=record_timeout):
        kept_paths = get_paths(kept_id, kept_name)
        if kept_paths:
            break

    assert kept_paths == [
        f"/{index}/_doc"
    ], f"Job {kept_id} should have one record sent after the restart"
    assert get_paths(sent_id, sent_name) == [
        f"/{index}/_doc"
    ], f"Job {sent_id} should not be sent again after the restart"
