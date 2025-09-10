############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import logging
import pytest

# Global variables

cluster = "test_cluster"

# Note: assoc table row count will the sum of these (plus a couple for root)
acct_table_rows = 5000
user_table_rows = 5000

acct_table = "acct_table"
assoc_table = f"{cluster}_assoc_table"
user_table = "user_table"

start_acct_id = 1
start_user_id = 1

# default values to insert below
acct_desc = ""
acct_org = ""
assoc_id_parent = 1
assoc_is_def = 1
assoc_parent_acct = "root"
assoc_shares = 1

table_gen_info = [
    {
        "name": acct_table,
        "sql": f"(creation_time, mod_time, name, description, organization) values (@seq, @seq, concat('acct', @seq), '{acct_desc}', '{acct_org}')",
        "ignore": "ignore",
        "seq_start": start_acct_id,
        "seq_end": start_acct_id + acct_table_rows - 1,
    },
    {
        "name": assoc_table,
        "sql": f"(creation_time, mod_time, acct, parent_acct, id_parent, lineage, shares) values (@seq, @seq, concat('acct', @seq), '{assoc_parent_acct}', {assoc_id_parent}, concat('/acct', @seq, '/'), {assoc_shares})",
        "ignore": "",
        "seq_start": start_acct_id,
        "seq_end": start_acct_id + acct_table_rows - 1,
    },
    {
        "name": user_table,
        "sql": "(creation_time, mod_time, name) values (@seq, @seq, concat('user', @seq))",
        "ignore": "ignore",
        "seq_start": start_user_id,
        "seq_end": start_user_id + user_table_rows - 1,
    },
    {
        "name": assoc_table,
        "sql": f"(creation_time, mod_time, user, acct, is_def, id_parent, lineage, shares) values (@seq, @seq, concat('user', {start_user_id}+@seq), concat('acct', {start_acct_id}+@seq), {assoc_is_def}, {assoc_id_parent}, concat('/acct', {start_acct_id}+@seq, '/0-user', {start_user_id}+@seq, '/'), {assoc_shares})",
        "ignore": "",
        "seq_start": 0,
        "seq_end": user_table_rows - 1,
    },
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", None, source="slurmdbd")
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db(sql_statement_repeat):
    #
    # Create many accounts, users and assoc table entries.
    #
    # Do this outside slurm since it's way faster.
    #
    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        user=atf.properties["slurm-user"],
        # quiet=True,
    )

    logging.info(
        f"Populating the accounting database with {acct_table_rows} accounts and {user_table_rows} users"
    )
    atf.stop_slurmdbd(quiet=True)
    for table_info in table_gen_info:
        mysql_command = sql_statement_repeat + ' -e "'
        mysql_command += f"call statement_repeat(\\\"insert {table_info['ignore']} into {table_info['name']} {table_info['sql']}\\\", {table_info['seq_start']}, {table_info['seq_end']}, 1, 1)"

        mysql_command += '"'
        atf.run_command(
            mysql_command,
            user=atf.properties["slurm-user"],
            fatal=True,
        )
    atf.start_slurmdbd(quiet=True)

    yield

    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
        user=atf.properties["slurm-user"],
        # quiet=True,
    )


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (25, 5),
    reason="Fixed in !648 speed up account deletion",
)
def test_remove_user_account():
    """Test removing default account of a user"""
    # acct1 is default account of user1
    max_perf_time = 10
    assert (
        atf.run_command_exit(
            "sacctmgr -i remove acct acct1",
            user=atf.properties["slurm-user"],
            timeout=max_perf_time,
        )
        == 0
    ), f"sacctmgr should be able to remove the account in less than {max_perf_time}s"
