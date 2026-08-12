############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that job_submit/lua can set a job's relay_io field.

The field is writable from both hooks, and any value but off or unset
means on. Both are silent when they go wrong: a job relays when the site
meant it not to, or the other way round, with nothing in a log to say
so. So they are checked against what actually reaches the scraper.
"""

import pytest
from relay_io_scraper import Scraper, make_socket_dir, remove_socket_dir

import atf

# Setting relay_io to a value that only means "on" before it narrows to
# uint16_t, which is where normalizing it too late turned it back off.
WIDE_ON = 65536

job_submit_lua = """
function slurm_job_submit(job_desc, part_list, submit_uid)
    if job_desc.comment == "relay-lua-on" then
        job_desc.relay_io = 1
    elseif job_desc.comment == "relay-lua-wide" then
        job_desc.relay_io = %d
    elseif job_desc.comment == "relay-lua-off" then
        job_desc.relay_io = 0
    end
    return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
    -- turns relaying on for whatever job is being updated
    job_desc.relay_io = 1
    return slurm.SUCCESS
end

return slurm.SUCCESS
""" % (
    WIDE_ON,
)


@pytest.fixture(scope="module", autouse=True)
def setup(scraper, socket_path):
    atf.require_version(
        (26, 11), "bin/srun", reason="Issue 51035: --relay-io added in 26.11"
    )
    atf.require_config_parameter_includes("JobSubmitPlugins", "lua")
    atf.require_config_file("job_submit.lua", job_submit_lua)
    atf.require_config_parameter("RelayIOPlugin", "uds_json")
    atf.require_config_parameter("RelayIOParameters", f"socket={socket_path}")
    # On by default, so that the lua script turning it off has something to
    # override and is not merely agreeing with the default.
    atf.require_config_parameter("RelayIODefaultEnabled", "yes")
    atf.require_nodes(1, [("CPUs", 2)])
    atf.require_slurm_running()

    # A relay that is disabled or broken would pass every negative test
    # below, so prove it works first.
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
    directory = make_socket_dir("lua")
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


def test_lua_can_turn_relaying_on(scraper):
    """A job that asked not to be relayed is relayed when the script says so."""
    # --relay-io=no, so the script has to override an explicit refusal. With
    # no option the job would be relayed by RelayIODefaultEnabled alone, and
    # the test would pass even if the script's assignment did nothing.
    atf.run_job_output(
        "--relay-io=no --comment=relay-lua-on -N1 -n1 echo lua-turned-it-on",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    assert any(
        "lua-turned-it-on" in b
        for b in [r["body"] for r in records if r["type"] == "log"]
    ), f"the script should have turned relaying on, got {records}"


def test_lua_can_turn_relaying_off(scraper):
    """The script setting relay_io to 0 overrides RelayIODefaultEnabled."""
    output = atf.run_job_output(
        "--comment=relay-lua-off -N1 -n1 echo lua-turned-it-off", fatal=True
    )
    assert (
        "lua-turned-it-off" in output
    ), f"the job should still run and display its output, got {output!r}"

    records = scraper.wait_for_quiet()
    assert records == [], f"the script should have turned relaying off, got {records}"


def test_lua_value_is_normalized_before_it_narrows(scraper):
    """Any value but off or unset means on, whatever its width."""
    atf.run_job_output(
        "--relay-io=no --comment=relay-lua-wide -N1 -n1 echo lua-set-it-wide",
        fatal=True,
    )

    records = scraper.wait_for("job_end")
    assert any(
        "lua-set-it-wide" in b
        for b in [r["body"] for r in records if r["type"] == "log"]
    ), (
        f"relay_io={WIDE_ON} should mean on, not whatever it truncates to, "
        f"got {records}"
    )


def test_a_modify_can_turn_relaying_on(scraper):
    """The modify hook turns relaying on for a job the submit hook turned off."""
    job_id = atf.submit_job_sbatch(
        "--comment=relay-lua-off --hold -N1 -o /dev/null "
        '--wrap "echo lua-modified-it"',
        fatal=True,
    )
    # The submit hook already set relay_io to 0, so the modify hook has an
    # explicit refusal to override rather than only RelayIODefaultEnabled.
    assert (
        atf.get_job_parameter(job_id, "RelayIO") is None
    ), "the submit hook should have turned relaying off"

    # runs slurm_job_modify(), which sets relay_io on a job still pending
    atf.run_command(f"scontrol update jobid={job_id} comment=hi", fatal=True)
    assert (
        atf.get_job_parameter(job_id, "RelayIO") == "YES"
    ), "the modify hook should have turned relaying back on"

    atf.run_command(f"scontrol release {job_id}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    records = scraper.wait_for("job_end")
    assert any(
        "lua-modified-it" in b
        for b in [r["body"] for r in records if r["type"] == "log"]
    ), f"the modify hook should have turned relaying on, got {records}"


def test_a_modify_is_refused_once_the_job_is_running(scraper):
    """The hook writing relay_io refuses a running job rather than ignoring it.

    Relaying is settled when the job starts, so a value that arrives later
    cannot be applied to the run in flight. A hook that sets relay_io on
    every modify therefore makes every update of a running job fail, rather
    than letting one through that would quietly do nothing.
    """
    script = "relay_lua_modify.sh"
    atf.make_bash_script(script, "sleep 60\n")

    job_id = atf.submit_job_sbatch(
        f"--comment=relay-lua-off -N1 -o /dev/null {script}", fatal=True
    )
    atf.wait_for_job_state(job_id, "RUNNING", fatal=True)

    error = atf.run_command_error(f"scontrol update jobid={job_id} comment=hi")
    assert (
        "Job is no longer pending execution" in error
    ), f"the modify hook should not be able to change a running job, got {error!r}"

    atf.cancel_jobs([job_id])
