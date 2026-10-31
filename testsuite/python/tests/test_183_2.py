############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test relay_io/uds_json at its shipped defaults.

test_183_1 pins every relay setting so that it can assert against known
values. That leaves the defaults themselves untested, including
RelayIODefaultEnabled, whose documented default is the opposite of what
that module needs. Only the socket path is configured here; everything
else is whatever the plugin ships with.
"""

import pytest
from relay_io_scraper import Scraper, make_socket_dir, remove_socket_dir

import atf


@pytest.fixture(scope="module", autouse=True)
def setup(scraper, socket_path):
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 51035: --relay-io added in 26.11"
    )
    atf.require_config_parameter("RelayIOPlugin", "uds_json")
    # Only the socket, so that the ring sizes and max_log_kbytes are the
    # shipped defaults rather than values this test chose.
    atf.require_config_parameter("RelayIOParameters", f"socket={socket_path}")
    # Unset, since its default is what this module exists to check
    atf.require_config_parameter("RelayIODefaultEnabled", None)
    atf.require_config_parameter_includes("PrologFlags", "Contain")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()

    # Prove the relay works at the defaults before the opt out tests run,
    # which otherwise cannot tell a working opt out from a dead relay.
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
    directory = make_socket_dir("def")
    yield f"{directory}/relay.sock"
    remove_socket_dir(directory)


@pytest.fixture(scope="module")
def scraper(socket_path):
    scraper = Scraper(socket_path)
    yield scraper
    scraper.stop()


@pytest.fixture(autouse=True)
def clean_records(scraper):
    scraper.clear()
    yield


def test_a_job_is_not_relayed_by_default(scraper):
    """RelayIODefaultEnabled defaults to no, so a job must ask to be relayed."""
    output = atf.run_job_output("-N1 -n1 echo not-relayed", fatal=True)
    assert (
        "not-relayed" in output
    ), f"the job should still run and display its output, got {output!r}"

    records = scraper.wait_for_quiet()
    assert (
        records == []
    ), f"a job that did not ask to be relayed should emit nothing, got {records}"


@pytest.mark.parametrize(
    "command, step_id",
    [
        ("srun --relay-io=yes -N1 -n1 echo default-settings", "0"),
        (
            'sbatch --relay-io=yes -N1 -o /dev/null --wrap "echo default-settings"',
            "batch",
        ),
        ("salloc --relay-io=yes -N1 srun -n1 echo default-settings", "0"),
    ],
    ids=["srun", "sbatch", "salloc"],
)
def test_relay_io_yes_relays_at_the_default_settings(scraper, command, step_id):
    """The shipped ring sizes and max_log_kbytes relay a job end to end.

    Run for each client that takes --relay-io. Opting in is the shipped
    workflow, since RelayIODefaultEnabled is off, and each client has its
    own path to the value.
    """
    atf.run_command(command, fatal=True)

    records = scraper.wait_for("job_end")
    types = [r["type"] for r in records]

    for event in ("job_start", "step_start", "step_end", "job_end"):
        assert event in types, f"expected a {event} record, got {types}"

    bodies = [
        r["body"]
        for r in records
        if r["type"] == "log" and r["slurm"].get("step_id") == step_id
    ]
    assert any("default-settings" in b for b in bodies), (
        f"task output should be relayed at the defaults under step {step_id}, "
        f"got {bodies}"
    )


def test_default_max_log_kbytes_splits_a_long_line(scraper):
    """The default cap is small, so a long line arrives split, not truncated."""
    written = "z" * 20000
    atf.run_job_output(
        "--relay-io=yes -N1 -n1 --output=none "
        "python3 -c 'import sys; sys.stdout.write(\"z\" * 20000)'",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    logs = [r for r in records if r["type"] == "log"]
    body = "".join(r["body"] for r in logs)

    assert len(logs) >= 5, (
        f"20000 bytes should take at least 5 records at the default "
        f"max_log_kbytes of 4 kilobytes, got {len(logs)}"
    )
    assert all(len(r["body"]) <= 4096 for r in logs), (
        f"no record should carry more than the default 4 kilobytes, got "
        f"{[len(r['body']) for r in logs]}"
    )
    assert body == written, (
        f"the records should reassemble to all {len(written)} bytes written, "
        f"got {len(body)}"
    )


def test_the_extern_step_is_not_relayed(scraper):
    """The extern step carries no task output, so it emits no step events."""
    atf.run_job_output("--relay-io=yes -N1 -n1 echo in-the-job-step", fatal=True)

    records = scraper.wait_for("job_end")
    step_ids = {
        r["slurm"]["step_id"] for r in records if "step_id" in r.get("slurm", {})
    }

    assert (
        "extern" not in step_ids
    ), f"the extern step has no output to relay, got {step_ids}"
    assert "0" in step_ids, f"the job's own step should be relayed, got {step_ids}"
