############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test 100.5: Accounting storage usage rollup performance.

Seeds a synthetic cluster with a realistic workload directly in MySQL
(boundary-spanning, suspended, and reserved jobs), triggers sacctmgr rollup,
and asserts the rollup completes within a time budget.

See test_115_1.py for sreport tests.

Run this only against a throwaway accounting DB.  sacctmgr rollup takes no
cluster argument, so every rollup here recomputes and overwrites usage rows
for every cluster in the DB over the window it covers.
"""

import logging
import re
from datetime import datetime, timedelta

import pytest

import atf

pytestmark = pytest.mark.slow

# Global variables
cluster = "test1005rollup"
NO_VAL = 0xFFFFFFFE

# total number of rows created for all tables in rollup
row_count_total = 1000000

# usage rollup for those rows should happen in less than this time
max_perf_time_default = 600

# 50% pre-rollup jobs filtered by WHERE, remaining 500k split across
# three time-clamp branches (inside/span/running).
id_assoc = "floor(2+pow(rand(), 3)*19999)"
id_qos = "floor(1+rand()*10)"
id_wckey = "floor(1+rand()*100)"
id_resv = "if(rand()<0.2, floor(1+rand()*5), 0)"
tres_alloc = "1=8,2=32000,4=1,5=1,1001=1"
tres_req = "1=8,2=32000,4=1,5=1,1001=1"
SUSPEND_MODULO = 20
suspended_job_count = (row_count_total // 2) // SUSPEND_MODULO
time_suspended_in_period = f"if(@seq % {SUSPEND_MODULO} = 0, floor(60+rand()*1800), 0)"

row_count_inside = 150000
row_count_span = 250000
row_count_running = 100000

time_inside_start = "@ps+floor(rand()*1800)"
time_inside_end = "@ps+1800+floor(rand()*1799)"
time_span_start = "@ps+floor(rand()*3600)"
time_span_end = "@pe+1+floor(rand()*3600)"
time_running_start = "@ps+floor(rand()*3600)"
time_running_end = "0"

# Job state IDs: 1 = JOB_RUNNING, 3 = JOB_COMPLETE
state_complete = 3
state_running = 1

table_gen_info = [
    {
        "type": "Job",
        "tables": [
            {
                "name": f"{cluster}_job_table",
                "sql": f"set job_db_inx=@seq,account='root',nodelist='n1',node_inx=0,time_submit=@ps-1,tres_req='{tres_req}', tres_alloc='{tres_alloc}',id_qos={id_qos},cpus_req=8,job_name='',id_assoc={id_assoc},id_job=@seq,id_resv={id_resv},id_wckey={id_wckey},id_user=0,id_group=0,het_job_id=0,het_job_offset={NO_VAL},state_reason_prev=0,nodes_alloc=1,\\`partition\\`='part1',priority=0,state={state_complete},time_start=@ps,time_eligible=@ps-1,time_end=@pe,env_hash_inx=0,script_hash_inx=0",
                "pre": "yes",
                "row_count": int(row_count_total / 2),
            },
            {
                "name": f"{cluster}_event_table",
                "sql": f"set cluster_nodes='n1',time_start=@ps,time_end=0,tres='1={row_count_total * 32}',reason='Cluster proc count'",
                "pre": "yes",
                "row_count": 1,
            },
            {
                "name": f"{cluster}_resv_table",
                "sql": (
                    "set id_resv=@seq-500001,"
                    "assoclist='1,2,3,4,5,6,7,8,9,10',"
                    "nodelist='n1',"
                    "resv_name=concat('resv',@seq-500001),"
                    "time_start=@ps,time_end=@pe,"
                    "tres='1=100,2=512000,1001=8'"
                ),
                "pre": "no",
                "row_count": 5,
            },
            {
                "name": f"{cluster}_job_table",
                "sql": f"set job_db_inx=@seq,account='root',nodelist='n1',node_inx=0,time_submit=@ps-1,tres_req='{tres_req}', tres_alloc='{tres_alloc}',id_qos={id_qos},cpus_req=8,job_name='',id_assoc={id_assoc},id_job=@seq,id_resv={id_resv},id_wckey={id_wckey},id_user=0,id_group=0,het_job_id=0,het_job_offset={NO_VAL},state_reason_prev=0,nodes_alloc=1,\\`partition\\`='part1',priority=0,state={state_complete},time_start={time_inside_start},time_eligible=@ps-1,time_end={time_inside_end},time_suspended={time_suspended_in_period},env_hash_inx=0,script_hash_inx=0",
                "pre": "no",
                "row_count": row_count_inside,
            },
            {
                "name": f"{cluster}_job_table",
                "sql": f"set job_db_inx=@seq,account='root',nodelist='n1',node_inx=0,time_submit=@ps-1,tres_req='{tres_req}', tres_alloc='{tres_alloc}',id_qos={id_qos},cpus_req=8,job_name='',id_assoc={id_assoc},id_job=@seq,id_resv={id_resv},id_wckey={id_wckey},id_user=0,id_group=0,het_job_id=0,het_job_offset={NO_VAL},state_reason_prev=0,nodes_alloc=1,\\`partition\\`='part1',priority=0,state={state_complete},time_start={time_span_start},time_eligible=@ps-1,time_end={time_span_end},time_suspended={time_suspended_in_period},env_hash_inx=0,script_hash_inx=0",
                "pre": "no",
                "row_count": row_count_span,
            },
            {
                "name": f"{cluster}_job_table",
                "sql": f"set job_db_inx=@seq,account='root',nodelist='n1',node_inx=0,time_submit=@ps-1,tres_req='{tres_req}', tres_alloc='{tres_alloc}',id_qos={id_qos},cpus_req=8,job_name='',id_assoc={id_assoc},id_job=@seq,id_resv={id_resv},id_wckey={id_wckey},id_user=0,id_group=0,het_job_id=0,het_job_offset={NO_VAL},state_reason_prev=0,nodes_alloc=1,\\`partition\\`='part1',priority=0,state={state_running},time_start={time_running_start},time_eligible=@ps-1,time_end={time_running_end},time_suspended={time_suspended_in_period},env_hash_inx=0,script_hash_inx=0",
                "pre": "no",
                "row_count": row_count_running,
            },
            {
                "name": f"{cluster}_suspend_table",
                "sql": (
                    f"set job_db_inx=500000+(@seq-1000006)*{SUSPEND_MODULO},"
                    f"id_assoc=0,"
                    f"time_start=@ps+300,time_end=@ps+600"
                ),
                "pre": "no",
                "row_count": suspended_job_count,
            },
        ],
        "max_perf_time": max_perf_time_default,
    },
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    """Test setup with required configurations."""

    atf.require_version((26, 11), "sbin/slurmdbd", reason="Ticket 24919 rollup perf")
    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_config_parameter("AllowNoDefAcct", None, source="slurmdbd")
    # Without this the rollup skips the wckey id_usage batch entirely, leaving
    # the wckey half of the chunked INSERT unexercised at any cardinality.
    atf.require_config_parameter("TrackWCKey", "yes", source="slurmdbd")

    atf.require_config_parameter("PurgeEventAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeJobAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeResvAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeStepAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeSuspendAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeTXNAfter", None, source="slurmdbd")
    atf.require_config_parameter("PurgeUsageAfter", None, source="slurmdbd")

    # Need debug level to capture "Everything rolled up in usec=N".
    atf.require_config_parameter("DebugLevel", "debug", source="slurmdbd")

    atf.require_slurm_running()


@pytest.fixture(scope="function", autouse=True)
def setup_db(sql_statement_repeat):

    if not sql_statement_repeat:
        pytest.skip(
            "Ticket 24919: needs the conftest mysql helper, which requires auto-config"
        )

    slurm_user = atf.properties["slurm-user"]

    # Truncate before the remove.  Running rows (time_end=0,
    # state=JOB_RUNNING) left by an interrupted run count as active jobs and
    # make the remove refuse, which would then fail the add below.  The tables
    # do not exist on a fresh database, so this is best effort.
    for t in ["job_table", "event_table", "resv_table", "suspend_table"]:
        atf.run_command(
            f'{sql_statement_repeat} -e "TRUNCATE TABLE {cluster}_{t}"',
            user=slurm_user,
            quiet=True,
        )
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
        user=slurm_user,
    )
    atf.run_command(
        f"sacctmgr -i add cluster {cluster}",
        user=slurm_user,
        fatal=True,
    )

    yield

    # Mark synthetic state=JOB_RUNNING rows as completed so the
    # sacctmgr remove-cluster pre-check doesn't see them as active jobs.
    atf.run_command(
        f"{sql_statement_repeat} -e "
        f'"update {cluster}_job_table set state=3 where state=1"',
        user=slurm_user,
        quiet=True,
        fatal=True,
    )

    # Removing a cluster cascades a delete across every per-cluster table, and
    # this one holds up to 1M seeded rows, so it needs longer than the default.
    atf.run_command(
        f"sacctmgr -i remove cluster {cluster}",
        user=slurm_user,
        timeout=600,
        fatal=True,
    )


def _split_set_assignments(s):
    """Split a comma-separated list of "col=expr" pairs, respecting parens,
    backticks, and single/double-quoted string literals."""
    parts = []
    cur = []
    depth = 0
    in_sq = in_dq = in_bq = False
    for ch in s:
        if in_sq:
            cur.append(ch)
            if ch == "'":
                in_sq = False
        elif in_dq:
            cur.append(ch)
            if ch == '"':
                in_dq = False
        elif in_bq:
            cur.append(ch)
            if ch == "`":
                in_bq = False
        elif ch == "'":
            in_sq = True
            cur.append(ch)
        elif ch == '"':
            in_dq = True
            cur.append(ch)
        elif ch == "`":
            in_bq = True
            cur.append(ch)
        elif ch == "(":
            depth += 1
            cur.append(ch)
        elif ch == ")":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if cur:
        parts.append("".join(cur))
    return [p.split("=", 1) for p in parts if "=" in p]


def _set_to_insert_select(set_clause, seq_start, seq_end):
    """Convert 'set col=expr, ...' into '(col, ...) SELECT expr, ... FROM
    seq_X_to_Y'. Replaces @seq references with the seq column from MariaDB's
    sequence storage engine. Used on MariaDB where the sequence engine
    produces ~5x faster bulk inserts than statement_repeat."""
    s = set_clause.strip()
    if s.lower().startswith("set "):
        s = s[4:].lstrip()
    cols, exprs = [], []
    for col, expr in _split_set_assignments(s):
        cols.append(col.strip())
        exprs.append(expr.strip().replace("@seq", "seq"))
    return (
        f"({','.join(cols)}) SELECT {','.join(exprs)} "
        f"FROM seq_{seq_start}_to_{seq_end}"
    )


def _detect_mariadb(sql_statement_repeat):
    """Return True iff the backing SQL server is MariaDB (sequence engine
    available); False for upstream MySQL or on detection failure."""
    result = atf.run_command(
        f"{sql_statement_repeat} -Ns -e 'select version()'",
        user=atf.properties["slurm-user"],
        quiet=True,
    )
    return result["exit_code"] == 0 and "MariaDB" in result.get("stdout", "")


def test_usage_rollup_via_sacctmgr(sql_statement_repeat):
    """Usage rollup triggered via sacctmgr rollup."""

    dbd_log = atf.get_config_parameter(
        "LogFile", live=False, quiet=True, source="slurmdbd"
    )
    log_present = dbd_log and atf.run_command_exit(
        f"test -f {dbd_log}", user=atf.properties["slurm-user"], quiet=True
    )
    if not dbd_log or log_present != 0:
        pytest.skip(
            "Ticket 24919: LogFile not configured or not present in slurmdbd.conf; "
            "cannot measure rollup duration"
        )

    use_seq_engine = _detect_mariadb(sql_statement_repeat)
    logging.info(
        f"Bulk insert path: {'MariaDB seq engine' if use_seq_engine else 'statement_repeat'}"
    )

    for entry in table_gen_info:
        row_count_tally = 0

        # Compute the rollup window once: previous full hour.
        rounded_last_hour = datetime.now().replace(
            minute=0, second=0, microsecond=0
        ) - timedelta(hours=1)
        in_period_start = int(rounded_last_hour.timestamp())
        in_period_end = in_period_start + 3600

        # Pre-rollup data sits 3 hours back so it's filtered out by the
        # rollup's WHERE clause.
        pre_period_start = in_period_start - 2 * 3600
        pre_period_end = pre_period_start + 3600

        for table_info in entry["tables"]:
            row_count = int(table_info["row_count"])

            if "pre" in table_info and table_info["pre"] == "yes":
                seq_start = 1
                seq_end = row_count
                per_start = pre_period_start
                per_end = pre_period_end
            else:
                seq_start = row_count_tally + 1
                seq_end = seq_start + row_count - 1
                per_start = in_period_start
                per_end = in_period_end

            if use_seq_engine:
                insert_clause = _set_to_insert_select(
                    table_info["sql"], seq_start, seq_end
                )
                mysql_command = (
                    f"{sql_statement_repeat} -e "
                    f'"set @ps={per_start}; set @pe={per_end}; '
                    f"insert into {table_info['name']} {insert_clause}\""
                )
            else:
                mysql_command = sql_statement_repeat
                mysql_command += f" -e \"set @ps={per_start}; set @pe={per_end}; call statement_repeat(\\\"insert into {table_info['name']} {table_info['sql']}\\\", {seq_start}, {seq_end}, 1, 1)\""
            logging.info(
                f"Populating database with {row_count} rows in {table_info['name']}"
            )
            # A single insert here seeds up to 500k rows, which takes longer
            # than the default timeout on a loaded database.
            atf.run_command(
                mysql_command,
                user=atf.properties["slurm-user"],
                timeout=600,
                fatal=True,
            )

            row_count_tally = row_count_tally + row_count

        logging.info(
            f"Waiting {entry['max_perf_time']}s for {entry['type']} usage rollup to complete"
        )

        log_line_start = int(
            atf.run_command_output(
                f"wc -l < {dbd_log}",
                user=atf.properties["slurm-user"],
                fatal=True,
            ).strip()
        )

        # Trigger the rollup synchronously. sacctmgr -i sets rollback_flag=0
        # so the "Would you like to commit rollup?" prompt is bypassed.
        rollup_start_str = rounded_last_hour.strftime("%Y-%m-%dT%H:%M:%S")
        rollup_end_str = (rounded_last_hour + timedelta(hours=1)).strftime(
            "%Y-%m-%dT%H:%M:%S"
        )
        atf.run_command(
            f"sacctmgr -i rollup {rollup_start_str} {rollup_end_str}",
            user=atf.properties["slurm-user"],
            # Give the command timeout headroom over max_perf_time so a
            # genuine perf regression trips the time_sec assertion below
            # (with its useful message) instead of an opaque command
            # timeout.
            timeout=2 * entry["max_perf_time"],
            fatal=True,
        )

        log_slice = atf.run_command_output(
            f"tail -n +{log_line_start + 1} {dbd_log}",
            user=atf.properties["slurm-user"],
            quiet=True,
            fatal=True,
        )
        logging.info(f"Rollup lines from {dbd_log}:\n{log_slice.rstrip()}")

        err_lines = [ln for ln in log_slice.splitlines() if " error: " in ln]
        assert not err_lines, "slurmdbd logged errors during rollup:\n" + "\n".join(
            err_lines
        )

        # A concurrent periodic rollup could log its own "Everything rolled
        # up" line in this window; taking the first match could measure that
        # unrelated, likely much faster, rollup instead of the one this test
        # triggered. Require exactly one match so a concurrent rollup surfaces
        # as an explicit failure rather than a wrong measurement.
        matches = re.findall(r"Everything rolled up in usec=(\d+)", log_slice)
        assert len(matches) == 1, (
            f"expected exactly one 'Everything rolled up' log line within "
            f"{entry['max_perf_time']}s, got {len(matches)}"
        )
        rows_processed = row_count_inside + row_count_span + row_count_running
        time_sec = int(matches[0]) / 1_000_000.0
        rate = int(rows_processed / time_sec)

        logging.info(
            f"{entry['type']} usage rollup took {time_sec}s to process {rows_processed} job table rows ({rate} rows/sec)"
        )
        assert (
            time_sec < entry["max_perf_time"]
        ), f"rollup took {time_sec}s, expected < {entry['max_perf_time']}s"
