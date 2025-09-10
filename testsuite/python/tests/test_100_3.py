############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
from datetime import datetime
import logging
import math
import pytest
import re
import threading
import time

# Global variables
cluster = "test_cluster1"

# total number of rows created for all tables in purge type
row_count_total = 500000

# purge for those rows should happen in less than this time
max_perf_time_default = 60

table_gen_info = [
    {
        "type": "Event",
        "tables": [
            {
                "name": f"{cluster}_event_table",
                "sql": "(time_start,time_end,reason) values (@seq,@seq,'')",
                # Ex. override row count and set fixed amount if needed
                #  'row_count'     : 100,
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "Job",
        "tables": [
            {
                "name": f"{cluster}_job_table",
                "sql": "(cpus_req,job_name,id_assoc,id_job,id_resv,id_wckey,id_user,id_group,het_job_id,het_job_offset,state_reason_prev,nodes_alloc,\\`partition\\`,priority,state,time_end,env_hash_inx,script_hash_inx) values (0,'',0,@seq,0,0,0,0,0,0,0,0,'',0,0,@seq,@seq,@seq)",
            },
            # job_table will be used to seed remaining job tables to speed things up
            {
                "name": f"{cluster}_job_env_table",
                "sql": f"(hash_inx,env_hash) select env_hash_inx,env_hash_inx from {cluster}_job_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_job_script_table",
                "sql": f"(hash_inx,script_hash) select script_hash_inx,script_hash_inx from {cluster}_job_table",
                "from_template": True,
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "Resv",
        "tables": [
            {
                "name": f"{cluster}_resv_table",
                "sql": "(id_resv,time_end,resv_name) values (@seq,@seq,'')",
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "Step",
        "tables": [
            {
                "name": f"{cluster}_step_table",
                "sql": "(job_db_inx,id_step,nodelist,nodes_alloc,state,step_name,task_cnt,time_end) values (@seq,@seq,'',0,0,'',0,@seq)",
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "Suspend",
        "tables": [
            {
                "name": f"{cluster}_suspend_table",
                "sql": "(job_db_inx,time_end,id_assoc) values (@seq,@seq,0)",
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "TXN",
        "tables": [
            {
                "name": "txn_table",
                "sql": f"(timestamp,cluster,action,name,actor) values (@seq,'{cluster}',0,'','')",
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
    {
        "type": "Usage",
        "tables": [
            # qos usage tables added in 24.11 but not purged until 25.05.3, list them first for row count calculation later
            {
                "name": f"{cluster}_qos_usage_hour_table",
                "sql": "(creation_time,mod_time,id,time_start) values (@seq,@seq,1,@seq)",
                "minversion": (25, 5, 3),
            },
            # qos_usage_hour_table will be used to seed remaining qos usage tables to speed things up
            {
                "name": f"{cluster}_qos_usage_day_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_qos_usage_hour_table",
                "from_template": True,
                "minversion": (25, 5, 3),
            },
            {
                "name": f"{cluster}_qos_usage_month_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_qos_usage_hour_table",
                "from_template": True,
                "minversion": (25, 5, 3),
            },
            {
                "name": f"{cluster}_assoc_usage_hour_table",
                "sql": "(creation_time,mod_time,id,time_start) values (@seq,@seq,@seq,@seq)",
            },
            # assoc_usage_hour_table will be used to seed remaining assoc usage tables to speed things up
            {
                "name": f"{cluster}_assoc_usage_day_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_assoc_usage_month_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_wckey_usage_hour_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_wckey_usage_day_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_wckey_usage_month_table",
                "sql": f"(creation_time,mod_time,id,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_usage_hour_table",
                "sql": f"(creation_time,mod_time,id_tres,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_usage_day_table",
                "sql": f"(creation_time,mod_time,id_tres,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
            {
                "name": f"{cluster}_usage_month_table",
                "sql": f"(creation_time,mod_time,id_tres,time_start) select creation_time,mod_time,id,time_start from {cluster}_assoc_usage_hour_table",
                "from_template": True,
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""

    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", None, source="slurmdbd")

    # if any purge types are to be skipped, comment them out below
    atf.require_config_parameter("PurgeEventAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeJobAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeResvAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeStepAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeSuspendAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeTXNAfter", "1h", source="slurmdbd")
    atf.require_config_parameter("PurgeUsageAfter", "1h", source="slurmdbd")

    # debug2 really only required
    atf.require_config_parameter("DebugLevel", "debug2", source="slurmdbd")
    # these can be helpful when debugging/tracing
    # atf.require_config_parameter("DebugLevel", "debug4", source="slurmdbd")
    # atf.require_config_parameter("DebugFlags", f"{atf.get_config(live=False, source="slurmdbd", quiet=True)["DebugFlags"]},DB_USAGE,DB_ARCHIVE", source="slurmdbd")

    # so database will exist
    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db(sql_statement_repeat):

    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        user=atf.properties["slurm-user"],
        fatal=True,
        # quiet=True,
    )

    yield

    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
        user=atf.properties["slurm-user"],
        # quiet=True,
    )


# we need to pass in a mutable object (list with index) so the thread results can be returned
def run_command_bg(command, result, index, **run_command_kwargs):
    result[index] = atf.run_command(command, **run_command_kwargs)


@pytest.mark.xfail(
    atf.get_version("sbin/slurmdbd") < (25, 5),
    reason="Fixed in !856 - Add index on the deleted column for use with archive and purge",
)
def test_purge_slurm_db_tables(sql_statement_repeat):
    """Test purging large number of rows"""

    # rollup beginning/ending log signatures
    rollup_beg_pat = "running rollup"
    rollup_end_pat = "everything rolled"

    dbd_log = atf.get_config_parameter(
        "LogFile", live=False, quiet=True, source="slurmdbd"
    )

    for entry in table_gen_info:
        if (
            atf.get_config_parameter(
                f"Purge{entry['type']}After", live=False, quiet=True, source="slurmdbd"
            )
            is None
        ):
            continue

        # stop so db can be altered
        atf.stop_slurmdbd()

        # make entries 2hrs old
        two_hours_ago = int(time.time()) - 2 * 3600

        count_sql = ""
        table_count = len(entry["tables"])
        for table_info in entry["tables"]:
            if (
                "minversion" in table_info
                and atf.get_version("sbin/slurmdbd") < table_info["minversion"]
            ):
                table_count -= 1
                continue

            # if row_count was given then use it, otherwise make each table equal size
            if "row_count" in table_info:
                row_count = int(table_info["row_count"])
            else:
                row_count = int(math.ceil(row_count_total / table_count))

            # populate the table
            if "from_template" in table_info and table_info["from_template"]:
                # data copied from another template table so no need to repeat insert
                # this makes things faster
                seq_start = 1
                seq_end = 1
            else:
                seq_start = two_hours_ago - row_count + 1
                seq_end = two_hours_ago
            mysql_command = sql_statement_repeat
            mysql_command += f" -e \"call statement_repeat(\\\"insert into {table_info['name']} {table_info['sql']}\\\", {seq_start}, {seq_end}, 1, 1)\""
            logging.info(
                f"Populating database with {row_count} rows in {table_info['name']}"
            )
            atf.run_command(
                mysql_command,
                user=atf.properties["slurm-user"],
                fatal=True,
            )

            count_sql += (
                f"{'+' if count_sql else ''}(select count(*) from {table_info['name']})"
            )

        # save starting sum of table row counts
        mysql_command = sql_statement_repeat + f' -Ns -e "select {count_sql}"'
        starting_row_count = atf.run_command_output(
            mysql_command,
            user=atf.properties["slurm-user"],
            # quiet=True,
            fatal=True,
        )

        # fabricate "last ran" time of 2hrs ago so hourly rollup/purge will be triggered
        mysql_command = (
            sql_statement_repeat
            + f' -e "delete ignore from {cluster}_last_ran_table; insert into {cluster}_last_ran_table (hourly_rollup, daily_rollup, monthly_rollup) values ({two_hours_ago}, {two_hours_ago}, {two_hours_ago})"'
        )
        atf.run_command(
            mysql_command,
            user=atf.properties["slurm-user"],
            fatal=True,
        )

        logging.info(
            f"Waiting {entry['max_perf_time']}s for {entry['type']} purge to complete (happens as part of rollup)"
        )

        # start watching the log before starting slurmdbd so all desired output can be captured
        results = [None]
        index = 0
        tail_thread = threading.Thread(
            target=run_command_bg,
            args=(
                # use "-n 1" so we don't match rollup_end_pat from previous run
                f"tail -n 1 -F {dbd_log} | grep -E --line-buffered -i -m2 '{rollup_beg_pat}|{rollup_end_pat}'",
                results,
                index,
            ),
            kwargs={
                "timeout": entry["max_perf_time"],
                "quiet": True,
                "user": atf.properties["slurm-user"],
            },
        )
        tail_thread.start()

        # wait for thread to start
        while tail_thread.ident is None:
            time.sleep(0.1)

        atf.start_slurmdbd()

        # wait for rollup (purge) to finish or timeout
        tail_thread.join()

        logging.info(
            f"Rollup lines identified in {dbd_log}:\n{results[index]['stdout'].rstrip()}"
        )

        search_result = re.search(
            rf"(\d\d\d\d-\d\d-\d\dT\d\d:\d\d:\d\d\.\d\d\d).*{rollup_beg_pat}.*\s*.*(\d\d\d\d-\d\d-\d\dT\d\d:\d\d:\d\d\.\d\d\d).*{rollup_end_pat}",
            results[index]["stdout"],
            re.IGNORECASE,
        )

        assert (
            search_result
        ), f"slurmdbd should be able to purge {entry['type']} tables in less than {entry['max_perf_time']}s"

        # count the actual number of rows removed by the purge
        mysql_command = sql_statement_repeat + f' -Ns -e "select {count_sql}"'
        ending_row_count = atf.run_command_output(
            mysql_command,
            user=atf.properties["slurm-user"],
            # quiet=True,
            fatal=True,
        )
        rows_removed = int(starting_row_count) - int(ending_row_count)

        start_obj = datetime.strptime(search_result.group(1), "%Y-%m-%dT%H:%M:%S.%f")
        end_obj = datetime.strptime(search_result.group(2), "%Y-%m-%dT%H:%M:%S.%f")
        time_sec = (end_obj - start_obj).total_seconds()
        rate = int(rows_removed / time_sec)

        # debug: list tables with leftover rows
        mysql_command = (
            sql_statement_repeat
            + f" -e \"select rpad(table_name,40,' '),lpad(table_rows,6,' ') from information_schema.tables where table_schema=database() and table_name like '%_{entry['type'].lower()}_%' and table_rows>0\""
        )
        atf.run_command_output(mysql_command, user=atf.properties["slurm-user"])

        logging.info(
            f"{entry['type']} purge took {time_sec}s to remove {rows_removed} rows from {table_count} tables ({rate} rows/sec)"
        )
