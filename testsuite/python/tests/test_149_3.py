############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
############################################################################
import csv
import logging
from pathlib import Path

import atf
import pytest


acctg_freq = 2
sacct_base_fields = [
    "JobIDRaw",
    "ElapsedRaw",
    "TotalCPU",
]
hdf5_fields = [
    "RSS",
    "VMSize",
    "Pages",
]
sacct_ready_fields = sacct_base_fields + [f"Max{field}" for field in hdf5_fields]
sacct_fields = (
    sacct_ready_fields
    + [f"Max{field}Node" for field in hdf5_fields]
    + [f"Max{field}Task" for field in hdf5_fields]
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(2)
    atf.require_accounting()
    atf.require_config_parameter("AcctGatherProfileType", "acct_gather_profile/hdf5")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter(
        "ProfileHDF5Dir",
        Path("h5").absolute(),
        source="acct_gather",
    )

    atf.require_slurm_running()


@pytest.fixture(scope="module")
def job_id(use_memory_program):
    """Generate a task profile with stable memory values and return the job id"""

    # Asymmetric per-node peaks; sacct's continuous max-tracking and HDF5's sparse
    # sampling can otherwise disagree on which node won when peaks are near-identical.
    cmd = "alloc.sh"
    atf.make_bash_script(
        cmd,
        f"""
size_mb=64
if [ $SLURM_PROCID -eq 1 ]; then
    size_mb=96
fi

exec {use_memory_program} "$size_mb" 6
""",
    )

    return atf.submit_job_srun(
        f"--acctg-freq=task={acctg_freq} --profile=task -N2 --ntasks-per-node=1"
        f" ./{cmd}",
        fatal=True,
    )


@pytest.fixture(scope="module")
def hdf5_file(job_id):
    hdf5_file = Path("test-values.h5").absolute()
    atf.run_command(f"sh5util -j {job_id} -o {hdf5_file}", fatal=True)

    return hdf5_file


@pytest.fixture(scope="module")
def time_series_rows(job_id, hdf5_file):
    output_file = "time_series.csv"
    opts = f"--input={hdf5_file} --output={output_file} -j {job_id}"
    atf.run_command(
        f"sh5util {opts} --series Tasks -E --level=Node:TimeSeries",
        fatal=True,
    )

    with open(output_file, newline="") as csv_file:
        reader = csv.DictReader(csv_file)
        assert reader.fieldnames, f"Extracted TimeSeries ({output_file}) is empty"
        rows = list(reader)

    logging.debug(f"CSV contents: {rows}")
    assert rows, f"No data in output file ({output_file})"

    return rows


def parse_sacct_time_seconds(value):
    days_str, _, time_str = value.strip().rpartition("-")
    time_parts = time_str.split(":")

    if len(time_parts) == 2:
        hours_str = "0"
        minutes_str, seconds_str = time_parts
    elif len(time_parts) == 3:
        hours_str, minutes_str, seconds_str = time_parts
    else:
        pytest.fail(f"Unable to parse sacct time value: {value}")

    days = int(days_str) if days_str else 0
    hours = days * 24 + int(hours_str)
    minutes = hours * 60 + int(minutes_str)
    seconds = minutes * 60 + float(seconds_str)  # TotalCPU may include microseconds

    return seconds


def read_sacct_step(job_id):
    output = atf.run_command_output(
        f"sacct -nP -j {job_id} --noconvert --format={','.join(sacct_fields)}",
        fatal=True,
    )

    step_id = f"{job_id}.0"
    for line in output.strip().splitlines():
        row_values = line.split("|")
        assert len(row_values) == len(sacct_fields), f"Unexpected sacct output: {line}"

        row = dict(zip(sacct_fields, row_values))
        if row["JobIDRaw"] == step_id:
            return row

    return None


@pytest.fixture(scope="module")
def sacct_step(job_id):
    row = None
    for _ in atf.timer():
        row = read_sacct_step(job_id)
        if row and all(row[field].strip() for field in sacct_ready_fields):
            return row
    pytest.fail(f"sacct did not report all expected fields: {row}")


def grouped_by_id(rows):
    grouped = {}
    for row in rows:
        row_id = tuple(row[col] for col in ["Step", "Node", "Series"])
        if row_id not in grouped:
            grouped[row_id] = []
        grouped[row_id].append(row)
    return grouped


def max_rows(rows, field):
    max_value = max(int(row[field]) for row in rows)
    return [row for row in rows if int(row[field]) == max_value]


@pytest.mark.parametrize("field", hdf5_fields)
def test_hdf5_max_value_matches_sacct(field, time_series_rows, sacct_step):
    """HDF5 task max values agree with the corresponding sacct step values"""

    hdf5_rows = max_rows(time_series_rows, field)
    hdf5_value = int(hdf5_rows[0][field])
    sacct_field = f"Max{field}"

    # sacct --noconvert prints MaxRSS/MaxVMemSize in bytes;
    # the HDF5 task profile stores RSS/VMSize in KiB.
    raw = int(sacct_step[sacct_field].strip())
    sacct_value = raw // 1024 if field in ("RSS", "VMSize") else raw

    if field == "RSS":
        # With jobacct_gather/cgroup, HDF5 RSS is sampled from memory.current
        # while sacct MaxRSS may use memory.peak. Under memory pressure current can
        # drop below the retained peak, so HDF5_max can legitimately be less than
        # sacct MaxRSS — the comparison is a one-sided upper bound, not equality.
        assert (
            hdf5_value <= sacct_value
        ), f"RSS: hdf5={hdf5_value} exceeds sacct={sacct_value}"
    else:
        assert (
            hdf5_value == sacct_value
        ), f"HDF5 {field} max value should match {sacct_field}"

        if len(hdf5_rows) == 1:
            sacct_node = sacct_field + "Node"
            assert (
                hdf5_rows[0]["Node"] == sacct_step[sacct_node]
            ), f"HDF5 {field} max node should match {sacct_node}"

            sacct_task = sacct_field + "Task"
            assert (
                hdf5_rows[0]["Series"] == sacct_step[sacct_task]
            ), f"HDF5 {field} max task should match {sacct_task}"


def test_hdf5_elapsed_time_consistent(time_series_rows, sacct_step):
    """HDF5 elapsed values are internally consistent and bounded by sacct"""

    sacct_elapsed = int(sacct_step["ElapsedRaw"])

    max_elapsed = 0
    for identity, rows in grouped_by_id(time_series_rows).items():
        elapsed_values = [int(row["ElapsedTime"]) for row in rows]
        epoch_values = [int(row["EpochTime"]) for row in rows]

        assert elapsed_values == sorted(
            elapsed_values
        ), f"ElapsedTime for {identity} should be non-decreasing: {elapsed_values}"
        assert epoch_values == sorted(
            epoch_values
        ), f"EpochTime for {identity} should be non-decreasing: {epoch_values}"

        start_times = {
            epoch_value - elapsed_value
            for epoch_value, elapsed_value in zip(epoch_values, elapsed_values)
        }
        assert (
            len(start_times) == 1
        ), f"EpochTime - ElapsedTime for {identity} should be constant"

        series_elapsed = max(elapsed_values)
        max_elapsed = max(max_elapsed, series_elapsed)

    assert max_elapsed > 0, "HDF5 ElapsedTime should be non-zero"
    assert max_elapsed <= sacct_elapsed + 1, (
        "HDF5 ElapsedTime should not exceed sacct ElapsedRaw: "
        f"hdf5={max_elapsed}, sacct={sacct_elapsed}"
    )


def test_cpu_utilization_matches_cpu_time_delta(time_series_rows):
    """HDF5 CPUUtilization matches the sampled CPUTime over elapsed time"""

    for identity, rows in grouped_by_id(time_series_rows).items():
        ordered = sorted(rows, key=lambda r: float(r["ElapsedTime"]))
        checked = 0
        for prev, curr in zip(ordered, ordered[1:]):
            elapsed_delta = float(curr["ElapsedTime"]) - float(prev["ElapsedTime"])
            if elapsed_delta <= 0:
                continue
            # CPUUtilization is percent of one CPU; the job runs --ntasks-per-node=1.
            expected = 100.0 * float(curr["CPUTime"]) / elapsed_delta
            observed = float(curr["CPUUtilization"])
            assert observed == pytest.approx(
                expected, abs=0.01
            ), f"CPUUtilization for {identity}: observed={observed}, expected={expected}"
            checked += 1
        assert checked > 0, f"No CPU utilization samples for {identity}"


def test_sacct_total_cpu_bounds_cpu_time(time_series_rows, sacct_step):
    """HDF5 CPUTime per-interval deltas sum within sacct TotalCPU for the step"""

    hdf5_cpu_time = sum(float(row["CPUTime"]) for row in time_series_rows)
    sacct_total_cpu = parse_sacct_time_seconds(sacct_step["TotalCPU"])

    assert hdf5_cpu_time >= 0, "HDF5 CPUTime should be non-negative"
    assert sacct_total_cpu >= 0, "sacct TotalCPU should be non-negative"
    assert hdf5_cpu_time <= sacct_total_cpu + acctg_freq, (
        "HDF5 CPUTime should not exceed sacct TotalCPU beyond one sampling "
        f"interval: hdf5={hdf5_cpu_time}, sacct={sacct_total_cpu}"
    )
