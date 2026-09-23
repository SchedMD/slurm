############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test that scrontab builds a job description from a #SCRON line."""

import pytest

import atf

# 00:00 on January 1st fires at most once a year, so every job these tests
# submit stays PENDING for the whole run and never needs a working node.
schedule = "0 0 1 1 *"


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter_includes("ScronParameters", "enable")
    atf.require_slurm_running()


@pytest.fixture(autouse=True)
def clean_crontab():
    """Guarantees every test both starts and ends with no crontab.

    Cleaning up front matters as much as cleaning up after: several tests prove
    a crontab was rejected by checking that none is installed, and that only
    means anything if none was installed to begin with. Without the leading
    removal those tests borrow their precondition from whichever test ran
    before, so a crontab left behind by an interrupted run makes a correct
    scrontab look broken.

    Removing the crontab also cancels the jobs it submitted.
    """
    atf.run_command("scrontab -r", quiet=True)
    yield
    atf.run_command("scrontab -r", quiet=True)


def install_crontab(*lines):
    """Installs a crontab built from lines and returns the command result.

    scrontab reads its input from stdin when the file argument is "-", so the
    crontab never has to be staged on disk where the test user might not be
    able to read it.
    """
    crontab = "".join(f"{line}\n" for line in lines)
    return atf.run_command("scrontab -", input=crontab)


def require_shared_builder():
    """Skips unless scrontab builds its description with the shared builder.

    scrontab switched to slurm_opt_create_job_desc() in 26.11. Before that its
    private filler neither validated its input nor forwarded every option it
    accepted, so the behavior these tests pin does not exist on an older
    install. The tests that pass on both sides are deliberately left ungated,
    so they keep guarding the common path on every version.

    The version comes from a sibling client binary, not from scrontab and not
    from the default component. scrontab itself takes no -V, so it cannot be
    asked. slurmctld is the wrong thing to ask because these behaviors are all
    produced by the client, and doc/html/upgrades.shtml upgrades slurmctld
    before the login nodes, so a 26.11 controller can still be paired with a
    26.05 scrontab. Client commands ship as one package and are upgraded
    together, so any of them reports scrontab's own version.
    """
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="scrontab builds the job description with the shared builder in 26.11",
    )


def get_cron_job_id(name):
    """Returns the id of the cron job named name, once slurmctld reports it."""
    job_ids = []

    def query():
        job_ids[:] = atf.run_command_output(
            f"squeue --me --noheader --format=%A --name={name}", quiet=True
        ).split()
        return job_ids

    assert atf.repeat_until(
        query, lambda found: len(found) == 1
    ), f"Expected exactly one cron job named {name}, found {job_ids}"

    return int(job_ids[0])


def test_crontab_round_trip():
    """Verify a crontab can be installed, listed and removed."""

    assert (
        install_crontab("#SCRON --job-name=cron_round_trip", f"{schedule} sleep 1")[
            "exit_code"
        ]
        == 0
    ), "scrontab should accept a valid crontab"

    listed = atf.run_command_output("scrontab -l")
    assert schedule in listed, "scrontab -l should echo the installed schedule"
    assert (
        "#SCRON --job-name=cron_round_trip" in listed
    ), "scrontab -l should echo the installed #SCRON line"

    job_id = get_cron_job_id("cron_round_trip")
    assert (
        atf.get_job_parameter(job_id, "CronJob") == "Yes"
    ), "The submitted job should be marked as a cron job"

    atf.run_command("scrontab -r", fatal=True)
    assert "no crontab" in atf.run_command_output(
        "scrontab -l", xfail=True
    ), "scrontab -l should report no crontab once it is removed"


def test_job_name_defaults_to_the_command():
    """Verify scrontab names a job after its command when --job-name is absent.

    scrontab supplies this itself, so it has to keep working now that the
    shared builder fills in the rest of the description.

    The command only has to be distinctive, not runnable. Every job here stays
    PENDING for the whole run, so nothing ever executes it.
    """

    command = "cron_169_1_default_name"

    assert (
        install_crontab(f"{schedule} {command} 1")["exit_code"] == 0
    ), "scrontab should accept a crontab with no #SCRON line"

    job_id = get_cron_job_id(command)
    assert (
        atf.get_job_parameter(job_id, "JobName") == command
    ), "The job name should default to the first word of the command"


def test_scron_options_reach_slurmctld():
    """Verify #SCRON options are forwarded into the job description.

    Several options are read back from one entry rather than one, so the case
    guards the breadth of the forwarding path and not a single field. All of
    these are forwarded on every supported version, which is why the case is
    ungated.
    """

    assert (
        install_crontab(
            "#SCRON --job-name=cron_scron_options",
            "#SCRON --time=7",
            "#SCRON --comment=cron_169_1_comment",
            "#SCRON --nice=50",
            f"{schedule} sleep 1",
        )["exit_code"]
        == 0
    ), "scrontab should accept #SCRON options"

    job_id = get_cron_job_id("cron_scron_options")
    assert (
        atf.get_job_parameter(job_id, "TimeLimit") == "00:07:00"
    ), "--time from a #SCRON line should reach slurmctld"
    assert (
        atf.get_job_parameter(job_id, "Comment") == "cron_169_1_comment"
    ), "--comment from a #SCRON line should reach slurmctld"
    assert (
        atf.get_job_parameter(job_id, "Nice") == 50
    ), "--nice from a #SCRON line should reach slurmctld"


def test_options_do_not_leak_between_entries():
    """Verify a #SCRON option applies only to the entry that follows it.

    scrontab.1 promises "Options are always reset in between each crontab
    entry". That is the one invariant the man page states about #SCRON, and it
    is the one a single-entry crontab can never observe: if option state leaked
    forward, every other test here would still pass while a user's first entry
    silently imposed its --time on all the later ones.
    """

    assert (
        install_crontab(
            "#SCRON --job-name=cron_reset_first",
            "#SCRON --time=7",
            f"{schedule} sleep 1",
            "#SCRON --job-name=cron_reset_second",
            f"{schedule} sleep 1",
        )["exit_code"]
        == 0
    ), "scrontab should accept a crontab holding two entries"

    first = get_cron_job_id("cron_reset_first")
    second = get_cron_job_id("cron_reset_second")
    assert first != second, "The two entries should submit two distinct jobs"

    assert (
        atf.get_job_parameter(first, "TimeLimit") == "00:07:00"
    ), "--time should apply to the entry that follows it"
    assert (
        atf.get_job_parameter(second, "TimeLimit") != "00:07:00"
    ), "--time should not carry over into the next crontab entry"


def test_oom_kill_step_reaches_slurmctld():
    """Verify an option scrontab parsed but used to discard now arrives.

    scrontab accepted --oom-kill-step in a #SCRON line and then dropped it,
    because its private filler never copied the field into the job
    description. Building the description with the shared builder forwards it.
    """

    require_shared_builder()

    assert (
        install_crontab(
            "#SCRON --job-name=cron_oom_kill_step",
            "#SCRON --oom-kill-step=1",
            f"{schedule} sleep 1",
        )["exit_code"]
        == 0
    ), "scrontab should accept --oom-kill-step in a #SCRON line"

    job_id = get_cron_job_id("cron_oom_kill_step")
    assert (
        atf.get_job_parameter(job_id, "OOMKillStep") == 1
    ), "--oom-kill-step from a #SCRON line should reach slurmctld"


def test_oom_kill_step_is_left_to_the_configuration():
    """Verify a crontab that says nothing about OOM killing follows the config.

    scrontab used to leave oom_kill_step at the zero that
    slurm_init_job_desc_msg() writes, which slurmctld reads as an explicit
    request not to kill the whole step and so overrides TaskPluginParam. The
    shared builder sends NO_VAL16 instead, and scontrol prints the field only
    when it is not NO_VAL16.
    """

    require_shared_builder()

    assert (
        install_crontab(
            "#SCRON --job-name=cron_oom_default",
            f"{schedule} sleep 1",
        )["exit_code"]
        == 0
    ), "scrontab should accept a crontab that sets no options"

    job_id = get_cron_job_id("cron_oom_default")
    assert (
        atf.get_job_parameter(job_id, "OOMKillStep", default=None) is None
    ), "A cron job should not pin OOMKillStep when the crontab does not set it"


def test_arbitrary_distribution_without_nodelist_is_rejected():
    """Verify arbitrary distribution with no node list fails the whole crontab.

    The shared builder validates this where scrontab's private filler did not,
    so _entry_to_job() can now fail and the caller has to report it.
    """

    require_shared_builder()

    result = install_crontab(
        "#SCRON --distribution=arbitrary",
        f"{schedule} sleep 1",
    )

    assert result["exit_code"] != 0, "scrontab should reject the crontab"
    assert (
        "Arbitrary distribution" in result["stderr"]
    ), "scrontab should say why the job description could not be built"
    assert (
        "errors in your crontab" in result["stdout"]
    ), "scrontab should report the crontab as containing errors"
    assert "no crontab" in atf.run_command_output(
        "scrontab -l", xfail=True
    ), "A rejected crontab should not be installed"


def test_invalid_nodelist_is_rejected():
    """Verify an unparsable --nodelist fails the whole crontab."""

    require_shared_builder()

    result = install_crontab(
        "#SCRON --nodelist=node[",
        f"{schedule} sleep 1",
    )

    assert result["exit_code"] != 0, "scrontab should reject the crontab"
    assert (
        "Invalid node list" in result["stderr"]
    ), "scrontab should say why the job description could not be built"
    assert "no crontab" in atf.run_command_output(
        "scrontab -l", xfail=True
    ), "A rejected crontab should not be installed"


@pytest.mark.parametrize("option", ["--clusters=cluster1", "--immediate"])
def test_options_scrontab_cannot_set_are_rejected(option):
    """Verify options only salloc, sbatch and srun define stay unavailable.

    These reach neither the job description nor slurmctld, because scrontab
    never puts them in its option table. The #SCRON parser rejects the line
    outright rather than accepting the option and then ignoring it.
    """

    result = install_crontab(f"#SCRON {option}", f"{schedule} sleep 1")

    assert result["exit_code"] != 0, f"scrontab should reject {option}"
    assert (
        "Invalid option found in #SCRON line" in result["stderr"]
    ), f"scrontab should report {option} as an invalid #SCRON option"
    assert "no crontab" in atf.run_command_output(
        "scrontab -l", xfail=True
    ), "A rejected crontab should not be installed"


def test_invalid_cron_spec_is_rejected():
    """Verify a malformed time specification fails the whole crontab."""

    result = install_crontab("99 * * * * sleep 1")

    assert result["exit_code"] != 0, "scrontab should reject a bad time spec"
    assert "no crontab" in atf.run_command_output(
        "scrontab -l", xfail=True
    ), "A rejected crontab should not be installed"
