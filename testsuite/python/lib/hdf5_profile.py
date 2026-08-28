############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Shared helpers for the acct_gather_profile/hdf5 test modules.

The hdf5 profile modules that import this library run a profiled job and
assert against the same per-node profile files and the same sh5util CSV
extraction, so the profile directories, the profile listing, the node
profile poll, the merge, the schema check, and the extraction reader are
collected here. Each module still pins its own cluster configuration, whose
tracked TRES are what its own subject turns on, and judges its own columns.
"""

import csv
import io
import logging
from pathlib import Path

import atf


def profile_hdf5_dir():
    """The ProfileHDF5Dir a module configures, under its working directory.

    The value goes into acct_gather.conf, which the daemons read from
    elsewhere on the node, so the path is made absolute. Keeping it under the
    module's own working directory leaves the profiles where that module can
    clean them up.
    """
    return Path("h5").absolute()


def profile_user_dir(hdf5_dir):
    """The test user's own profile directory under a ProfileHDF5Dir.

    The plugin files each user's profiles under a directory named for that
    user, so a module lists the profiles of the user its jobs run as.
    """
    return hdf5_dir / atf.properties["test-user"]


def get_node_profiles(user_profile_dir, job_id):
    """The job's node step HDF5 profile files, sorted by path.

    acct_gather_profile/hdf5 writes one <job_id>_<step_id>_<node>.h5 file
    per node and step under the user's profile dir, with the literal batch
    in place of the step id for the batch step, so listing that pattern
    names every node step profile the job has produced so far.
    """
    output = atf.run_command_output(
        f"ls -1 {user_profile_dir}/{job_id}_*.h5",
        quiet=True,
    )
    return sorted(Path(profile) for profile in output.splitlines())


def merge_job_profiles(job_id, output_name="test.h5"):
    """Merge a job's node step profiles into one file and return its path.

    Runs sh5util in merge mode to gather every node step profile of the
    job into one file, with --savefiles keeping the per-node inputs in
    place so a caller can still assert against them. The merged file is
    written under output_name, test.h5 by default, in the module's working
    directory, so a module merging more than one job names each file.
    """
    hdf5_file = Path(output_name).absolute()

    atf.run_command(f"sh5util --savefiles -j {job_id} -o {hdf5_file}", fatal=True)

    return hdf5_file


def assert_schema(field_names, expected_cols):
    """Require every expected column among the field names, more are allowed."""
    missing_cols = set(expected_cols) - set(field_names)
    assert not missing_cols, f"Missing expected column(s): {missing_cols}"


def read_sh5util_csv(job_id, hdf5_file, opts, expected_cols):
    """Extract a job's merged HDF5 profile to CSV and return its rows.

    Runs sh5util over the merged profile with the given extraction options,
    asserts every expected column is present (further columns are allowed),
    and asserts the extraction holds at least one data row, so a caller
    judges values rather than the extraction plumbing.
    """
    output_file = "output"
    cmd = f"sh5util -i {hdf5_file} -o {output_file} -j {job_id} -s Tasks {opts}"
    atf.run_command(cmd, fatal=True)

    csv_text = atf.run_command_output(f"cat {output_file}", fatal=True)
    reader = csv.DictReader(io.StringIO(csv_text))
    cols = reader.fieldnames
    logging.debug(f"CSV cols: {cols}")
    assert cols, f"Output file ({output_file}) is empty"
    assert_schema(cols, expected_cols)

    rows = list(reader)

    logging.debug(f"CSV contents: {rows}")
    assert rows, f"No data in output file ({output_file})"

    return rows


def wait_for_node_profiles(user_profile_dir, job_id, expected_count):
    """Poll until a job has produced the expected node profile count.

    acct_gather_profile/hdf5 creates a node step profile file when the step
    starts on that node and completes it when the step ends, so the files
    appear over time and a caller polls for them, and reads one only after
    its step has ended. Returns the profile paths once the expected count is
    listed, and fails the test when the poll times out first.
    """
    profiles = None
    for _ in atf.timer(fatal=True):
        profiles = get_node_profiles(user_profile_dir, job_id)
        logging.debug(f"Node step profiles of job {job_id}: {profiles}")
        if len(profiles) == expected_count:
            break

    return profiles
