############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os, pathlib, re, shutil, sys

# SchedMD
from utils.log import (
    log,
    log_header,
    log_new_line,
)


# Filesystem
def cat(filename):
    with open(filename) as file:
        return file.read().replace("\n", "")


def cp_files_by_re(src_dir, dest_dir, pattern):
    for filename in os.listdir(src_dir):
        filepath = os.join(source_dir, filename)
        if os.path.isfile(filepath):
            if re.search(rf"{pattern}", filepath):
                shutil.copy(filepath, dest_dir)


def cp_expect_logs(src_dir, dest_dir):
    log(f"Copying expect test logs from {src_dir} to {dest_dir}")
    cp_files_by_re(src_dir, dest_dir, "testrun-results.json")
    cp_files_by_re(src_dir, dest_dir, "test\D+\.log.*")


def create_dir(dir):
    log(f"Creating {dir}")
    pathlib.Path(dir).mkdir(parents=True, exist_ok=True)


def delete_file(filename):
    try:
        os.remove(filename)
    except OSError:
        pass


def write_str_to_new_file(string, filename):
    delete_file(filename)
    with open(filename, "w") as f:
        f.write(string)


def file_exists(filename):
    return os.path.isfile(filename)


def exit_if_not_exists(filename):
    if not file_exists(filename):
        log_header("CONFIGURATION ERROR")
        log(f"ERROR: {filename} not found:")
        sys.exit("\nExiting..")


def remove_dir(dir):
    log("Removing: " + dir)
    if os.path.isdir(dir):
        uid = os.getuid()
        gid = os.getgid()
        cmd = f"sudo chown -R {uid}:{gid} {dir}"
        run_cmd(cmd)

        shutil.rmtree(dir)
        if not os.path.isdir(dir):
            log(f"Removing: - Done! - {dir}")
    else:
        log(f"Removing: - Not Found - {dir}")
