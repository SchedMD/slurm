############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os, sqlite3 as db

# SchedMD
from utils.fs import (
    delete_file,
    file_exists,
)
from utils.log import (
    log,
)

TESTS_TABLE = "tests"
db_name = ""


# Debug tips:
# In the run-tests dir run .run-tests then
# sqlite3 src/test_database.db
# then SELECT * FROM tests; to see results


def get_connection(db_name):
    conn = db.connect(db_name)
    return conn


def execute_query(db_name, sql, values=None):
    conn = get_connection(db_name)
    if values:
        cur = conn.execute(sql, values)
    else:
        cur = conn.execute(sql)

    data = cur.fetchall()
    conn.commit()
    conn.close()
    return data or None


def execute_many(db_name, sql, values=None):
    conn = get_connection(db_name)
    conn.executemany(sql, values)
    conn.commit()
    conn.close()


def create_new_db(db_name, seed_data):
    """Creates a new test database to info for sorting

    Name paths are relative to the main app script
    (that lives in the relative testsuite dir)

    seed_data = [
        ('expect/test1.1', 'expect-23.11', 1.06, 'PASSED'),
        ('python/tests/test_111_1.py', 'python-23.11', 2.5, 'FAILED'),
        ('slurm_unit/common/log-test.c', 'slurm-unit', 0, 'SKIPPED'),
        etc
    ]
    """

    delete_file(db_name)

    sql = f"""
        CREATE TABLE {TESTS_TABLE} (
            id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
            name TEXT,
            test_suite TEXT,
            duration REAL,
            status TEXT,
            run_id INTEGER
        );
    """

    execute_query(db_name, sql)
    insert_or_update_many(db_name, seed_data)


def create_seed_data_from_file(seed_file):
    seed_data = []

    with open(seed_file) as f:
        for line in f:
            if line[0] != "#":
                # Expected tuple format:
                # (name, test_suite, duration, status)
                li = list(line.split(","))
                li[3] = ""  # Set status to ""
                # Append a base run_id=0 as last field (its not exported in seed data)
                li.append(0)
                seed_data.append(tuple(li))

    return seed_data


def list_to_quote_str(lst):
    return ",".join(["'{}'".format(val) for val in lst])


def get_sorted_test_list(
    db_name,
    suite_list,
    test_status_list=["", "FAILED", "PASSED", "SKIPPED"],
    name_list=[],
):
    """Retrieve an ordered test list based on duration and status (optional)

    Useful to grab all the FAILED test sorted first as a list and then
    append the others (with failed ommitted) in order to run the fails first
    """
    suite_vals = list_to_quote_str(suite_list)
    name_vals = list_to_quote_str(name_list)
    status_vals = list_to_quote_str(test_status_list)

    # If you want specific tests (using the -i option from cli.py)
    name_vals = list_to_quote_str(name_list)
    cond_test_query = f"""
        AND name IN ({name_vals})
    """
    do_tests = cond_test_query if len(name_list) > 0 else ""

    sql = f"""
        SELECT * from {TESTS_TABLE}
        WHERE test_suite IN ({suite_vals})
        {do_tests}
        AND status IN ({status_vals})
        ORDER BY duration
    """

    data = execute_query(db_name, sql)
    return data


def reset_suite_run_ids(db_name, suite_list):
    suite_vals = list_to_quote_str(suite_list)
    sql = f"""
        UPDATE {TESTS_TABLE}
        SET run_id=0
        WHERE run_id=1 AND test_suite IN ({suite_vals})
     """
    execute_query(db_name, sql)


def get_sorted_FAILED_list(db_name, suite_list, name_list=[]):
    return get_sorted_test_list(db_name, suite_list, ["FAILED"], name_list=name_list)


def get_sorted_not_FAILED_list(db_name, suite_list, name_list=[]):
    return get_sorted_test_list(
        db_name, suite_list, ["", "PASSED", "SKIPPED"], name_list=name_list
    )


def get_new_run_list(db_name, suite_list, name_list=[], fails_first=False):
    if fails_first:
        fails = (
            get_sorted_test_list(db_name, suite_list, ["FAILED"], name_list=name_list)
            or []
        )

        other = (
            get_sorted_test_list(
                db_name, suite_list, ["", "PASSED", "SKIPPED"], name_list=name_list
            )
            or []
        )
        result = fails + other

    else:
        result = get_sorted_test_list(db_name, suite_list, name_list=name_list) or []

    return result


def get_id(db_name, name, test_suite):
    sql = f"""
        SELECT id FROM {TESTS_TABLE}
        WHERE name='{name}' AND test_suite='{test_suite}'
    """

    result = execute_query(db_name, sql)
    data = result[0][0] if result else None
    return data


def insert_or_update_row(db_name, test_row_tup, verbose=False):
    name, test_suite, duration, status, run_id = test_row_tup
    up_sql = ""
    in_sql = ""

    if test_id := get_id(db_name, name, test_suite):
        up_sql = f"""
            UPDATE {TESTS_TABLE}
            SET name=?, test_suite=?, duration=?, status=?, run_id=?
            WHERE id=?
         """
        return (up_sql, (name, test_suite, duration, status, run_id, test_id), "", None)
    else:
        in_sql = f"""
            INSERT INTO {TESTS_TABLE} (name, test_suite, duration, status, run_id)
            VALUES(?,?,?,?,?)
        """
        return ("", None, in_sql, test_row_tup)

    if verbose:
        print(up_sql)


def insert_or_update_many(db_name, test_data):
    insert_sql = ""
    update_sql = ""
    insert_val_list = []
    update_val_list = []

    for test_row_tup in test_data:
        up_sql, up_vals, in_sql, in_vals = insert_or_update_row(db_name, test_row_tup)
        update_sql = up_sql
        insert_sql = in_sql
        if in_vals:
            insert_val_list.append(in_vals)
        if up_vals:
            update_val_list.append(up_vals)

    if len(insert_sql) > 0:
        execute_many(db_name, insert_sql, insert_val_list)

    if len(update_sql) > 0:
        execute_many(db_name, update_sql, update_val_list)


def insert_if_new_many(db_name, test_data):
    insert_val_list = []
    sql = f"""
        INSERT INTO {TESTS_TABLE} (name, test_suite, duration, status, run_id)
        VALUES(?,?,?,?,?)
        """

    for test_row_tup in test_data:
        name, test_suite, duration, status, run_id = test_row_tup
        test_id = get_id(db_name, name, test_suite)

        # Add values to insert if it doesn't exist
        if not test_id:
            insert_val_list.append(test_row_tup)

    execute_many(db_name, sql, insert_val_list)


def setup_db_if_new(_db_name, SEED_FILE, create_fresh_db=False):
    global db_name
    db_name = _db_name

    if not file_exists(db_name) or create_fresh_db:
        log("--Creating new db--")
        seed_data = create_seed_data_from_file(SEED_FILE)
        create_new_db(db_name, seed_data)


def update_records_from_dirs(db_name, dir_test_data_list):
    # Update the db for new dir data each time
    for dir_test_data in dir_test_data_list:
        insert_if_new_many(db_name, dir_test_data)
