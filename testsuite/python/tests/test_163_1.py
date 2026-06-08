############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
from pathlib import Path

import atf
import pytest

memory_tolerance = 0.05
low_mem_mib = 64
high_mem_mib = 192
job_mem_mib = 320


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_config_parameter("CgroupPlugin", "autodetect", source="cgroup")
    atf.require_config_parameter("JobAcctGatherType", "jobacct_gather/cgroup")
    atf.require_config_parameter_excludes("JobAcctGatherParams", "no_file_cache")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nojobs")
    atf.require_config_parameter_excludes("AccountingStorageEnforce", "nosteps")
    atf.require_accounting()

    # Assumes task cgroups expose memory.peak (cgroup/v2) or
    # memory.max_usage_in_bytes (cgroup/v1).

    # Need 2 nodes to have a meaningful max memory node.
    atf.require_nodes(2, [("RealMemory", job_mem_mib)])

    # Force termination-only sampling, so short peaks come from cgroup peak data.
    atf.require_config_parameter("JobAcctGatherFrequency", 0)
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def task_script(use_memory_program):
    """Per-task script that allocates a short memory peak and writes the record
    `<task_id> <node> <requested_bytes> <peak_kb> <final_kb>`.
    """
    path = Path("task.sh").absolute()
    atf.make_bash_script(
        path,
        f"""
output_dir="$1"

# Rank 1 holds the high peak so MaxRSSTask attribution is non-zero.
# A single task uses the high peak too for tolerance headroom.
if [[ $SLURM_NTASKS -eq 1 || $SLURM_PROCID -eq 1 ]]; then
    mib={high_mem_mib}
else
    mib={low_mem_mib}
fi

# Use exec so that use_memory_program is the only process in the cgroup
exec {use_memory_program} "$mib" 0 --time-mem-valley 5 \\
    --prefix "$SLURM_PROCID $SLURMD_NODENAME $((mib * 1024 * 1024))" \\
    > "$output_dir/mem.$SLURM_PROCID.out"
""",
    )

    return str(path)


def parse_output(output_files):
    records = []
    for output_file in output_files:
        record = None
        for t in atf.timer():
            content = atf.run_command_output(f"cat {output_file}", quiet=True)
            try:
                task_id, node, requested_bytes, vmhwm_kb, final_vmrss_kb = (
                    content.split()
                )
                record = {
                    "task_id": task_id,
                    "node": node,
                    "requested_bytes": int(requested_bytes),
                    "vmhwm_bytes": int(vmhwm_kb) * 1024,
                    "final_vmrss_bytes": int(final_vmrss_kb) * 1024,
                }
                break
            except ValueError:
                continue
        else:
            pytest.fail(f"Unable to read {output_file}")

        records.append(record)

    return records


def parse_mem(fields):
    """Get memory in bytes from sacct output"""
    for field in fields.split(","):
        key, _, value = field.partition("=")
        if key == "mem" and value.isdigit():
            return int(value)
    return None


def try_get_mem_obs(step_id):
    sacct_fields = {
        "tot": "TRESUsageInTot",
        "ave": "TRESUsageInAve",
        "max": "TRESUsageInMax",
        "ave_rss": "AveRSS",
        "max_rss": "MaxRSS",
        "max_node": "MaxRSSNode",
        "max_task": "MaxRSSTask",
    }
    output = atf.run_command_output(
        f"sacct -j {step_id} -nP --noconvert -o {','.join(sacct_fields.values())}",
        fatal=True,
    )

    lines = [line for line in output.splitlines() if line.strip()]
    if len(lines) != 1:
        return None

    values = lines[0].split("|")
    if len(values) != len(sacct_fields):
        return None
    values = dict(zip(sacct_fields, values))

    try:
        for field in ["tot", "ave", "max"]:
            values[field] = parse_mem(values[field])
        for field in ["ave_rss", "max_rss"]:
            values[field] = int(values[field])
        for field in ["max_node", "max_task"]:
            values[field] = values[field].strip()
    except ValueError:
        return None

    if any(values[field] is None for field in ["tot", "ave", "max"]):
        return None

    return values


def get_mem_obs(step_id):
    for t in atf.timer():
        mem_obs = try_get_mem_obs(step_id)
        if mem_obs is not None:
            return mem_obs
    pytest.fail("Unable to parse sacct output")


def assert_mem_released(records):
    for record in records:
        mem_released = record["vmhwm_bytes"] - record["final_vmrss_bytes"]
        assert (
            mem_released > record["requested_bytes"] * 0.5
        ), f"RSS should go down after the peak memory was released, but got: {records}"


def max_peak_record(records):
    return max(records, key=lambda record: record["vmhwm_bytes"])


def assert_peak_not_final(mem_obs, records):
    """Assert accounting captured the short RSS peak, not post-release RSS."""

    peak_record = max_peak_record(records)
    final_tot = sum(record["final_vmrss_bytes"] for record in records)
    final_ave = final_tot / len(records)
    requested_tot = sum(record["requested_bytes"] for record in records)
    requested_ave = requested_tot / len(records)

    expected = {
        "tot": ("TRESUsageInTot", final_tot, requested_tot * 0.5),
        "max": (
            "TRESUsageInMax",
            peak_record["final_vmrss_bytes"],
            peak_record["requested_bytes"] * 0.5,
        ),
        "max_rss": (
            "MaxRSS",
            peak_record["final_vmrss_bytes"],
            peak_record["requested_bytes"] * 0.5,
        ),
        "ave": ("TRESUsageInAve", final_ave, requested_ave * 0.5),
        "ave_rss": ("AveRSS", final_ave, requested_ave * 0.5),
    }

    for field, (label, final_value, min_delta) in expected.items():
        accounted_delta = mem_obs[field] - final_value
        assert (
            accounted_delta > min_delta
        ), f"{label} should be a peak, but got like final RSS"


def assert_max_ids(mem_obs, records):
    peak_record = max_peak_record(records)
    node_map = {record["node"]: record["task_id"] for record in records}
    node = mem_obs["max_node"]
    task = mem_obs["max_task"]

    assert node in node_map, f"Unknown MaxRSSNode {node}"
    assert (
        node_map[node] == peak_record["task_id"]
    ), f"MaxRSSNode {node} should match highest VmHWM"
    assert (
        task == peak_record["task_id"]
    ), f"MaxRSSTask {task} should match highest VmHWM"


def assert_mem_matches_records(mem_obs, records):
    # Tot/Ave/Max reflect per-task peaks only in completed-job (sacct) accounting
    expected_peaks = [record["vmhwm_bytes"] for record in records]
    expected_tot = sum(expected_peaks)
    expected_max = max(expected_peaks)
    expected_ave = expected_tot / len(expected_peaks)

    expected = {
        "tot": ("TRESUsageInTot", expected_tot),
        "max": ("TRESUsageInMax", expected_max),
        "max_rss": ("MaxRSS", expected_max),
        "ave": ("TRESUsageInAve", expected_ave),
        "ave_rss": ("AveRSS", expected_ave),
    }
    for field, (label, value) in expected.items():
        assert mem_obs[field] == pytest.approx(
            value, rel=memory_tolerance
        ), f"{label} should be {value}, but got {mem_obs[field]}"


def assert_peak_accounted(srun_cmd, node_count, task_count, output_dir):
    job_id = atf.submit_job_sbatch(
        f"-N{node_count} --mem={job_mem_mib} -o mem.%j.out -e mem.%j.err "
        f'--wrap="{srun_cmd}"',
        fatal=True,
    )

    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_step_accounted(job_id, 0, fatal=True)

    output_files = [output_dir / f"mem.{task_id}.out" for task_id in range(task_count)]
    records = parse_output(output_files)
    mem_obs = get_mem_obs(f"{job_id}.0")

    assert_mem_released(records)
    assert_peak_not_final(mem_obs, records)
    assert_mem_matches_records(mem_obs, records)

    return mem_obs, records


def test_mem_sacct(task_script):
    """Test completed accounting preserves a short single-task peak."""

    output_dir = Path(".").absolute()
    srun_cmd = f"srun -N1 -n1 {task_script} {output_dir}"
    assert_peak_accounted(
        srun_cmd,
        node_count=1,
        task_count=1,
        output_dir=output_dir,
    )


def test_mem_sacct_nodes(task_script):
    """Test completed accounting attributes the max peak task and node."""

    output_dir = Path(".").absolute()
    srun_cmd = f"srun -N2 -n2 --ntasks-per-node=1 {task_script} {output_dir}"
    mem_obs, records = assert_peak_accounted(
        srun_cmd,
        node_count=2,
        task_count=2,
        output_dir=output_dir,
    )

    assert_max_ids(mem_obs, records)
