############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import csv
import logging
import re
from pathlib import Path

import pytest

import atf

id_cols = ["Step", "Node", "Series"]
summarized_cols = [
    "EpochTime",
    "CPUFrequency",
    "CPUUtilization",
    "CPUTime",
    "GPUMemMB",
    "GPUUtilization",
    "RSS",
    "VMSize",
    "Pages",
    "ReadMB",
    "WriteMB",
]
int_value_cols = [
    "EpochTime",
    "CPUFrequency",
    "GPUMemMB",
    "RSS",
    "VMSize",
    "Pages",
]
listed_cols = summarized_cols + ["ElapsedTime"]


@pytest.fixture(scope="module")
def hdf5_dir():
    return Path("h5").absolute()


@pytest.fixture(scope="module", autouse=True)
def setup(hdf5_dir):
    atf.require_nodes(2)
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter(
        "ProfileHDF5Dir",
        hdf5_dir,
        source="acct_gather",
    )
    # A default profile would make the extern step write its own files
    atf.require_config_parameter(
        "ProfileHDF5Default",
        [None, "None"],
        source="acct_gather",
    )
    atf.require_config_parameter(
        "JobAcctGatherType",
        "jobacct_gather/cgroup",
    )

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def user_profile_dir(hdf5_dir):
    return hdf5_dir / atf.properties["test-user"]


@pytest.fixture(scope="module")
def job_id(user_profile_dir):
    """Generate a profile and return the job id"""

    if atf.get_version("sbin/slurmd") < (26, 11):
        # Issue 51239: slurmstepds raced to create ProfileHDF5Dir
        atf.run_command(
            f"mkdir -p -m 0700 {user_profile_dir}",
            user=atf.properties["test-user"],
            fatal=True,
        )

    job_id = atf.submit_job_srun(
        "--acctg-freq=task=2 --profile=task -N2 --ntasks-per-node=1 sleep 6",
        fatal=True,
    )

    yield job_id

    atf.run_command(
        f"rm -f {user_profile_dir}/{job_id}_*.h5",
        quiet=True,
    )


def get_node_profiles(user_profile_dir, job_id):
    output = atf.run_command_output(
        f"ls -1 {user_profile_dir}/{job_id}_*.h5",
        quiet=True,
    )
    return sorted(Path(profile) for profile in output.splitlines())


def profile_test(args):
    return (
        atf.run_command_exit(
            f"test {args}",
            quiet=True,
        )
        == 0
    )


def wait_for_node_profiles(user_profile_dir, job_id, count=2):
    profiles = None
    for t in atf.timer():
        profiles = get_node_profiles(user_profile_dir, job_id)
        if len(profiles) == count:
            break
    else:
        pytest.fail(f"Unable to get {count} profiles: {profiles}")

    return profiles


@pytest.fixture(scope="module")
def node_profiles(user_profile_dir, job_id):
    return wait_for_node_profiles(user_profile_dir, job_id)


@pytest.fixture(scope="module")
def hdf5_file(job_id, node_profiles):
    hdf5_file = Path("test.h5").absolute()

    atf.run_command(f"sh5util --savefiles -j {job_id} -o {hdf5_file}", fatal=True)

    return hdf5_file


@pytest.fixture(scope="module")
def batch_job_id(user_profile_dir):
    """Generate a profile with a batch step and step 0, and return the job id"""

    job_id = atf.submit_job_sbatch(
        '--acctg-freq=task=1 --profile=task -N1 --wrap "sleep 2; srun sleep 2"',
        fatal=True,
    )
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)

    yield job_id

    atf.run_command(
        f"rm -f {user_profile_dir}/{job_id}_*.h5",
        quiet=True,
    )


@pytest.fixture(scope="module")
def batch_hdf5_file(user_profile_dir, batch_job_id):
    hdf5_file = Path("batch.h5").absolute()

    # One file for the batch step and one for step 0 on the single node
    wait_for_node_profiles(user_profile_dir, batch_job_id, count=2)
    atf.run_command(f"sh5util --savefiles -j {batch_job_id} -o {hdf5_file}", fatal=True)

    return hdf5_file


def h5dump_paths(hdf5_file):
    """Return the set of group and dataset paths in an HDF5 file"""

    output = atf.run_command_output(f"h5dump -n {hdf5_file}", fatal=True)
    paths = set()
    for line in output.splitlines():
        match = re.match(r"^\s*(group|dataset)\s+(/\S+)", line)
        if match and match.group(2) != "/":
            paths.add(match.group(2))

    logging.debug(f"h5dump paths in {hdf5_file}: {sorted(paths)}")
    return paths


def assert_schema(field_names, expected_cols):
    missing_cols = set(expected_cols) - set(field_names)
    assert not missing_cols, f"Missing expected column(s): {missing_cols}"


def read_sh5util_csv(job_id, hdf5_file, opts, expected_cols):
    output_file = "output"
    cmd = f"sh5util -i {hdf5_file} -o {output_file} -j {job_id} -s Tasks {opts}"
    atf.run_command(cmd, fatal=True)

    with open(output_file, newline="") as csv_file:
        reader = csv.DictReader(csv_file)
        cols = reader.fieldnames
        logging.debug(f"CSV cols: {cols}")
        assert cols, f"Output file ({output_file}) is empty"
        assert_schema(cols, expected_cols)

        rows = list(reader)

    logging.debug(f"CSV contents: {rows}")
    assert rows, f"No data in output file ({output_file})"

    return rows


@pytest.fixture(scope="module")
def time_series_rows(job_id, hdf5_file):
    return read_sh5util_csv(
        job_id, hdf5_file, "-E --level=Node:TimeSeries", id_cols + listed_cols
    )


@pytest.fixture(scope="module")
def totals_rows(job_id, hdf5_file):
    metrics = ["Min", "Max", "Sum", "Avg"]
    cols = id_cols + ["ElapsedTime"]

    for col in summarized_cols:
        for metric in metrics:
            cols.append(f"{metric}_{col}")

    return read_sh5util_csv(job_id, hdf5_file, "-E --level=Node:Totals", cols)


def test_profile_dir(hdf5_dir, user_profile_dir, job_id):
    """Profiles land in the user's subdirectory"""

    profiles = wait_for_node_profiles(user_profile_dir, job_id)

    assert profile_test(f"-d {user_profile_dir}")
    assert len(profiles) == 2, "Expected two profiles for a two node job"
    assert profiles == get_node_profiles(user_profile_dir, job_id)


def test_merge(hdf5_file, node_profiles):
    """Merging profiles produces a new, non-empty output file and saves inputs"""

    assert hdf5_file.exists(), f"Merge did not create {hdf5_file}"
    assert hdf5_file.stat().st_size > 0, f"Merge created empty {hdf5_file}"
    for profile in node_profiles:
        assert profile_test(f"-f {profile}"), f"Profile {profile} was not saved"


def test_hdf5_utils(job_id, node_profiles, hdf5_file):
    """Generated hdf5 files are compatible with common hdf5 utilities and use
    the documented group layout"""

    atf.require_tool("h5dump")

    for node_profile in node_profiles:
        assert atf.run_command_exit(f"h5dump -H {node_profile}") == 0

    assert atf.run_command_exit(f"h5dump -H {hdf5_file}") == 0

    job_nodes = set(
        atf.node_range_to_list(atf.get_job_parameter(job_id, "NodeList", fatal=True))
    )
    assert_count(job_nodes, 2, "job node(s)")

    # The merged file has /Steps/0/Nodes/<node>/Tasks/<taskid>, one dataset per
    # task that ran on the node
    paths = h5dump_paths(hdf5_file)
    node_tasks = {}
    for node in job_nodes:
        prefix = f"/Steps/0/Nodes/{node}/Tasks/"
        tasks = {path[len(prefix) :] for path in paths if path.startswith(prefix)}
        assert_count(tasks, 1, f"task dataset(s) for {node} in {hdf5_file}")
        node_tasks[node] = tasks.pop()

    assert sorted(node_tasks.values()) == [
        "0",
        "1",
    ], f"Expected tasks 0 and 1 across the nodes, saw: {node_tasks}"


def test_batch_step_name(batch_hdf5_file):
    """The merged file names the batch step group 'batch'"""

    atf.require_tool("h5dump")

    paths = h5dump_paths(batch_hdf5_file)
    for step_path in ["/Steps/batch", "/Steps/0"]:
        assert step_path in paths, f"Missing {step_path} in {batch_hdf5_file}"


def test_listed_fields(job_id, hdf5_file):
    """Listing fields produces the correct schema"""

    output = atf.run_command_output(
        f"sh5util -i {hdf5_file} -j {job_id} --series=Tasks -L",
        fatal=True,
    )
    assert_schema(output.strip().splitlines(), listed_cols)


def assert_count(values, count, description):
    assert (
        len(values) == count
    ), f"Expected {count} {description}, saw: {len(values)} ({values})"


def row_identity(row):
    return tuple(row[col] for col in id_cols)


def grouped_by_id(rows):
    grouped = {}
    for row in rows:
        row_id = row_identity(row)
        if row_id not in grouped:
            grouped[row_id] = []
        grouped[row_id].append(row)
    return grouped


def assert_close(field, metric, identity, observed_value, expected_value, sample_count):
    if isinstance(expected_value, int):
        target = expected_value
    else:
        target = pytest.approx(
            expected_value, rel=1e-12, abs=max(1e-5, sample_count * 1e-6)
        )
    assert observed_value == target, f"{metric}_{field} for {identity}"


def assert_ids(ids):
    steps = {x[0] for x in ids}
    nodes = {x[1] for x in ids}
    series = {x[2] for x in ids}

    assert_count(steps, 1, "Step(s)")
    assert_count(nodes, 2, "Node(s)")
    assert_count(series, 2, "Series")


@pytest.mark.parametrize("field", summarized_cols)
def test_totals_match_time_series(field, time_series_rows, totals_rows):
    """Node:Totals values match the extracted Node:TimeSeries samples"""

    time_series_by_id = grouped_by_id(time_series_rows)
    totals_by_id = {row_identity(row): row for row in totals_rows}

    assert time_series_by_id.keys() == totals_by_id.keys()
    assert_ids(time_series_by_id)

    is_int_field = field in int_value_cols
    for identity, rows in time_series_by_id.items():
        values = [
            int(row[field]) if is_int_field else float(row[field]) for row in rows
        ]
        expected_values = {
            "Min": min(values),
            "Max": max(values),
            "Sum": sum(values),
            "Avg": sum(values) / len(values),
        }

        for metric, expected_value in expected_values.items():
            raw = totals_by_id[identity][f"{metric}_{field}"]
            totals_value = int(raw) if is_int_field and metric != "Avg" else float(raw)
            assert_close(
                field, metric, identity, totals_value, expected_value, len(values)
            )


@pytest.mark.xfail(
    atf.get_version("bin/sh5util") < (26, 5),
    reason="Ticket 25241: Item extract 'Malformed file' error fixed in sh5util >= 26.05",
)
def test_item_extract(job_id, hdf5_file, time_series_rows):
    """Item-extract mode data is consistent"""

    nodes = sorted({row["Node"] for row in time_series_rows})
    item_cols = [
        "MinNode",
        "MinValue",
        "MaxNode",
        "MaxValue",
        "Sum",
        "Avg",
        "NumNodes",
        *nodes,
    ]

    for col in summarized_cols:
        item_rows = read_sh5util_csv(job_id, hdf5_file, f"-I --data={col}", item_cols)
        for row in item_rows:
            num_nodes = row["NumNodes"]
            assert (
                num_nodes.isdigit()
            ), f"{col} item extract has invalid NumNodes: {num_nodes}"
            assert (
                1 <= int(num_nodes) <= len(nodes)
            ), f"{col} item extract has invalid NumNodes: {num_nodes}"
