############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os, re
from time import perf_counter

# SchedMD
from db.test_db import (
    insert_or_update_many,
)
from test_runners.runner_ui import (
    color_state,
    print_status_line,
    print_test_line,
)
from utils.log import (
    log_new_line,
)
from utils.cmds import (
    perform,
    run_cmd,
    run_cmd_or_exit,
)
from utils.fs import (
    write_str_to_new_file,
)

stats_dict = {}


def run_regressions_tests(
    db_name, test_data_list, APP_DIR, LOG_DIR, test_env, resume=False
):
    global stats_dict

    total_tests = len(test_data_list)
    result_data_list = []
    python_status_re = re.compile("=+\\n(PASSED|FAILED|SKIPPED|ERROR)")
    msg = "Updating regressions test records"
    fails = []

    stats_dict = {
        "completions": 0,
        "passes": 0,
        "skips": 0,
        "fails": [],
        "total": total_tests,
        "status": "",
    }

    # Handle already ran tests if resume mode is on
    if resume:
        stats_dict, test_data_list, result_data_list = filter_resume_data(
            stats_dict, test_data_list, result_data_list
        )
        print(f"~ Resuming from {len(result_data_list)} previously ran tests ~")

    # Prepare environment
    expect_suite_dir = "expect/"
    expect_dir = f"{APP_DIR}/{expect_suite_dir}"

    python_suite_dir = "python/tests/"
    python_dir = f"{APP_DIR}/{python_suite_dir}"

    cur_dir = os.getcwd()

    for test_data in test_data_list:
        _id, name, test_suite, duration, status, run_id = list(test_data)
        test_dir, rel_name = name.rsplit("/", 1)

        if test_suite == "expect":
            cmd = f"expect {rel_name} 2>&1"
            suite_dir = expect_suite_dir
            my_dir = expect_dir
        else:
            cmd = f"pytest-3 -s -rA -v {rel_name} 2>&1"
            cmd = f"bash -c '{cmd}'"
            suite_dir = python_suite_dir
            my_dir = python_dir

        # Run the test
        if cur_dir != my_dir:
            cur_dir = my_dir
            os.chdir(my_dir)

        start = perf_counter()
        print_status_line(
            f"{suite_dir}{rel_name}", stats_dict["completions"], stats_dict["total"]
        )

        try:
            output = run_cmd(
                cmd,
                env=test_env,
                quiet=True,
                print_output=False,
            )
        except KeyboardInterrupt:
            stats_dict["status"] = "ABORTED"
            break

        # Gather stats
        end = perf_counter()
        duration = round(end - start, 2)
        status = get_test_status(test_suite, output, python_status_re)
        run_id = 1

        # Print test result
        print_test_line(f"{suite_dir}{rel_name}", duration, status)
        my_tup = (name, test_suite, duration, status, run_id)

        # Update stats
        stats_dict["completions"] += 1

        if status == "FAILED":
            fails.append(my_tup)
            stats_dict["fails"].append(rel_name)
            filepath = f"{LOG_DIR}/{rel_name}.log.failed"
            write_str_to_new_file(f"{output.stdout}\n{output.stderr}", filepath)
        elif status == "SKIPPED":
            stats_dict["skips"] += 1
            filepath = f"{LOG_DIR}/{rel_name}.log.skipped"
            write_str_to_new_file(output.stdout, filepath)
        else:
            stats_dict["passes"] += 1

        result_data_list.append((name, test_suite, duration, status, run_id))

    # Prepare result if finished a complete run
    # (Mostly for updating the db with new durations)
    num_results = len(result_data_list)
    if num_results == total_tests:
        stats_dict["status"] = "COMPLETED"

    # Update the db only on a fail (with fail_fast) or complete run
    if num_results:
        perform(msg, insert_or_update_many, db_name, result_data_list, verbose=False)


def filter_resume_data(stats_dict, test_data_list, result_data_list):
    new_test_data_list = []
    for test_data in test_data_list:
        _id, name, test_suite, duration, status, run_id = list(test_data)
        test_dir, rel_name = name.rsplit("/", 1)

        # run_id = 0 -> fresh, run_id = 1 -> ran last time
        if run_id > 0:
            stats_dict["completions"] += 1

            if status == "FAILED":
                stats_dict["fails"].append(rel_name)
            elif status == "SKIPPED":
                stats_dict["skips"] += 1
            else:
                stats_dict["passes"] += 1

            result_data_list.append((name, test_suite, duration, status, run_id))
        else:
            new_test_data_list.append(test_data)
    return (stats_dict, new_test_data_list, result_data_list)


def get_regressions_run_stats():
    return stats_dict


def get_test_status(test_suite, output, status_re=""):
    status = "PASSED"
    if test_suite == "expect":
        if output.returncode != 0:
            status = "SKIPPED" if output.returncode > 127 else "FAILED"
    else:
        result = status_re.findall(output.stdout)[0]
        status = "FAILED" if result == "ERROR" else result

    return status
