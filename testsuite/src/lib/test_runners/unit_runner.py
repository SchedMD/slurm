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


def run_unit_tests(db_name, test_data_list, UNIT_DIR, LOG_DIR, resume=False):
    global stats_dict

    total_tests = len(test_data_list)
    result_data_list = []
    fail_name_re = re.compile("(slurm_unit.*)")
    msg = "Updating unit test records"
    fails = []

    stats_dict = {
        "completions": 0,
        "passes": 0,
        "skips": 0,
        "fails": [],
        "total": len(test_data_list),
        "status": "",
    }

    # Handle already ran tests if resume mode is on
    if resume:
        stats_dict, test_data_list, result_data_list = filter_resume_data(
            stats_dict, test_data_list, result_data_list
        )
        print(f"~ Resuming from {len(result_data_list)} previously ran tests ~")

    # Change to slurm_build_dir/testsuite/slurm_unit & run make
    os.chdir(UNIT_DIR)
    run_cmd("make -j clean", quiet=True)
    make_output = run_cmd_or_exit(
        "make -j", "ERROR: unable to perform make", quiet=True
    )

    for test_data in test_data_list:
        _id, name, test_suite, duration, status, run_id = list(test_data)
        test_dir, rel_name = name.rsplit("/", 1)
        rel_name = rel_name.rsplit(".", 1)[0]  # Remove '.c' extension

        # Run the test
        start = perf_counter()
        os.chdir(test_dir)
        print_status_line(rel_name, stats_dict["completions"], stats_dict["total"])

        try:
            # Set 'SUBDIRS= ' so check doesnt descend into dirs without tests
            chk_output = run_cmd(f"make check TESTS='{rel_name}' SUBDIRS= ", quiet=True)
        except KeyboardInterrupt:
            stats_dict["status"] = "ABORTED"
            break

        # Gather stats
        end = perf_counter()
        duration = round(end - start, 2)
        status = "FAILED" if chk_output.returncode else "PASSED"
        run_id = 1

        # Print test result
        print_test_line(rel_name, duration, status)

        my_tup = (name, test_suite, duration, status, run_id)

        # Update stats
        stats_dict["completions"] += 1

        if status == "FAILED":
            total_out = (
                f"{make_output.stdout}"
                f"{make_output.stderr}"
                f"{chk_output.stdout}"
                f"{chk_output.stderr}"
            )
            fails.append(my_tup)
            fail_name = fail_name_re.findall(name)[0].rsplit(".", 1)[0]
            stats_dict["fails"].append(fail_name)

            # Save log file
            filepath = f"{LOG_DIR}/{rel_name}.log.failed"
            write_str_to_new_file(f"{chk_output.stdout}\n{chk_output.stderr}", filepath)

        elif status == "SKIPPED":
            # Save log file
            filepath = f"{LOG_DIR}/{rel_name}.log.skipped"
            write_str_to_new_file(total_out, filepath)
            stats_dict["skips"] += 1
        else:
            stats_dict["passes"] += 1

        result_data_list.append((name, test_suite, duration, status, run_id))

    # Clean make leftovers
    run_cmd("make clean", quiet=True)

    # Prepare result if finished a complete run
    # (Mostly for updating the db with new durations)
    num_results = len(result_data_list)
    if num_results == total_tests:
        result = result_data_list
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


def get_unit_run_stats():
    return stats_dict
