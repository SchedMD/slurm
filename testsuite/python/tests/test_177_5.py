# SPDX-License-Identifier: GPL-2.0-or-later
"""Current account membership controls optional sacct metadata access."""

import pytest

import atf


def test_current_account_metadata():
    reader, owner = atf.properties["test-user"], atf.properties["slurm-user"]
    if atf.properties["test-user-uid"] == 0 or reader == owner:
        pytest.skip("Requires an unprivileged test user")
    atf.require_auto_config("wants to modify accounting and slurmdbd parameters")
    atf.require_accounting(modify=True)
    atf.require_config_parameter("PrivateData", "jobs", source="slurmdbd")
    atf.require_config_parameter("Parameters", None, source="slurmdbd")
    atf.require_config_parameter("AccountingStoreFlags", "job_script,job_env,job_kv")
    atf.require_slurm_running()
    acct = "test-acct-177-5"
    for args in (
        f"add account {acct},{acct}-other",
        f"add user {owner} account={acct}",
        f"add user {reader} account={acct},{acct}-other",
        f"modify user {reader} set adminlevel=none",
        f"modify user {reader} set defaultaccount={acct}-other",
    ):
        atf.run_command(f"sacctmgr -i {args}", user=owner, fatal=True)
    job = atf.submit_job_sbatch(
        f"-A {acct} -o /dev/null --export=ALL "
        '--kv=\'{"marker":"account_query_marker"}\' '
        "--wrap 'echo account_query_marker'",
        user=owner,
        env_vars="ACCOUNT_QUERY_MARKER=account_query_marker",
        fatal=True,
    )
    atf.wait_for_job_state(job, "COMPLETED", user=owner, fatal=True)
    query = f"sacct -nP -j {job}"

    def metadata():
        return atf.run_command_output(
            f"{query} -X -o JobIDRaw", user=reader, fatal=True
        ).strip()

    for _ in atf.timer(fatal=True):
        if (
            str(job)
            == atf.run_command_output(f"{query} -X -o JobIDRaw", user=owner).strip()
        ):
            break
    assert metadata() == ""
    atf.set_config_parameter("Parameters", "AllowAccountJobQuery", source="slurmdbd")
    assert metadata() == str(job)
    for option in ("--batch-script", "--env-vars", "--kv"):
        for _ in atf.timer(fatal=True):
            if "account_query_marker" in atf.run_command_output(
                f"{query} {option}", user=owner
            ):
                break
        assert (
            atf.run_command_output(f"{query} {option}", user=reader, fatal=True).strip()
            == ""
        )
    for action in ("delete", "add"):
        atf.run_command(
            f"sacctmgr -i {action} user {reader} account={acct}",
            user=owner,
            fatal=True,
        )
        assert metadata() == (str(job) if action == "add" else "")
