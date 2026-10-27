############################################################################
# Copyright (C) 2026 Google LLC.
############################################################################
"""Test gres/tpu PCI sysfs discovery, multi-core chip grouping, and ordering."""

import pathlib
import re
import shlex

import pytest

import atf

TPU_YAML = """tpu_types:
  - type: tpu7x
    pci_device_id: "0x0076"
    shape_per_node: 2x2x1
    shape_per_segment: 4x4x4
    nodes_per_pod: 2304
  - type: v6e
    pci_device_id: "0x006f"
    shape_per_node: 2x2
    shape_per_segment: 16x16
    nodes_per_pod: 64
"""

SLURM_CONF = (
    "ControlMachine=test_machine\n"
    "ClusterName=test_cluster\n"
    "SlurmUser=root\n"
    "GresTypes=tpu,fake\n"
)


def _build_mock_sysfs(
    sysfs_dir: pathlib.Path,
    device_id: str,
    cores_per_chip: int,
    cpulist: str = "0-95",
):
    """Build a mock /sys/bus/pci/devices tree for gres/tpu discovery testing.

    IOMMU groups start at 8 so the set spans single and double digits
    (8..11 for 1-core chips, 8..15 for 2-core chips), ensuring a lexicographic
    sort produces a different order than a numeric sort. Every device reports
    cpulist as its local_cpulist. Also populates three negative entries that
    must be skipped:
      - 0000:00:09.0: matching vendor/device with no iommu_group symlink
      - 0000:00:0a.0: vendor 0x1ae0 with an unconfigured PCI device ID (0x00ff)
      - 0000:00:0b.0: matching PCI device ID with a non-matching vendor (0x9999)
    """
    groups_dir = sysfs_dir / ".groups"
    groups_dir.mkdir(parents=True, exist_ok=True)

    def make_dev(addr: str, dev_id: str, group: int | None, vendor_id: str = "0x1ae0"):
        dev_dir = sysfs_dir / addr
        dev_dir.mkdir(parents=True, exist_ok=True)
        (dev_dir / "vendor").write_text(f"{vendor_id}\n")
        (dev_dir / "device").write_text(f"{dev_id}\n")
        (dev_dir / "local_cpulist").write_text(f"{cpulist}\n")
        if group is not None:
            grp_target = groups_dir / str(group)
            grp_target.mkdir(parents=True, exist_ok=True)
            (dev_dir / "iommu_group").symlink_to(f"../.groups/{group}")

    group = 8
    for slot in ("05", "06", "07", "08"):
        for core in range(cores_per_chip):
            make_dev(f"0000:00:{slot}.{core}", device_id, group)
            group += 1

    make_dev("0000:00:09.0", device_id, None)
    make_dev("0000:00:0a.0", "0x00ff", 99)
    make_dev("0000:00:0b.0", device_id, 98, vendor_id="0x9999")


@pytest.fixture(scope="module")
def environment():
    atf.require_version(
        (26, 11),
        "sbin/slurmd",
        reason="gres/tpu plugin added in 26.11",
    )
    test_dir = pathlib.Path(atf.module_tmp_path)
    test_program = test_dir / "test_144_40"
    source_file = pathlib.Path(__file__).with_suffix(".c")
    atf.compile_against_libslurm(
        str(source_file), str(test_program), full=True, fatal=True
    )

    etc_dir = test_dir / "etc"
    etc_dir.mkdir(parents=True, exist_ok=True)
    (etc_dir / "slurm.conf").write_text(SLURM_CONF)
    (etc_dir / "tpu.yaml").write_text(TPU_YAML)
    (etc_dir / "gres.conf").write_text("AutoDetect=tpu\n")

    return test_program, test_dir, etc_dir


def _run_discovery(
    test_program: pathlib.Path, etc_dir: pathlib.Path, sysfs_dir: pathlib.Path
):
    result = atf.run_command(
        shlex.join([str(test_program), str(etc_dir), str(sysfs_dir)])
    )
    assert (
        result["exit_code"] == 0
    ), f"Discovery harness exited with {result['exit_code']}:\n{result['stderr']}"
    assert (
        "DISCOVERY_OK" in result["stdout"]
    ), f"Discovery harness did not finish:\n{result['stdout']}"
    return result["stderr"]


def test_v6e_single_core_discovery(environment):
    """Verify single-core TPU chips (v6e) produce 1 VFIO file per GRES record."""
    test_program, test_dir, etc_dir = environment
    sysfs_dir = test_dir / "mock_v6e"
    _build_mock_sysfs(sysfs_dir, "0x006f", cores_per_chip=1)

    stderr = _run_discovery(test_program, etc_dir, sysfs_dir)
    gres_lines = [line for line in stderr.splitlines() if "GRES[tpu]" in line]

    assert (
        "Global AutoDetect=tpu(256)" in stderr
    ), "gres.conf AutoDetect=tpu should be parsed as the TPU flag"
    assert "Gres Name=fake" in stderr, "GRES other than tpu should be kept"
    assert len(gres_lines) == 4, f"Expected 4 GRES[tpu] records, got:\n{stderr}"
    for line in gres_lines:
        assert "Type:v6e" in line, f"Expected a v6e chip: {line}"
        assert "Cores(96):0-95" in line, f"Expected the chip on CPUs 0-95: {line}"
        assert "AUTODETECT" in line, f"Expected an autodetected record: {line}"
        assert "HAS_MULT" not in line, f"Expected a single-core chip: {line}"
        assert re.search(
            r"File:/dev/vfio/\d+\s+UUID", line
        ), f"Expected single VFIO device file in record: {line}"

    assert (
        "has no IOMMU group" in stderr
    ), "A chip without an IOMMU group should be reported and skipped"
    assert not any(
        "/dev/vfio/99" in line for line in gres_lines
    ), "A device with an unconfigured PCI device ID should be skipped"
    assert not any(
        "/dev/vfio/98" in line for line in gres_lines
    ), "A device with a non-matching PCI vendor ID should be skipped"


def test_tpu7x_dual_core_grouping_and_numeric_iommu_order(environment):
    """Verify dual-core TPU chips (tpu7x) group 2 PCI functions per chip with
    HAS_MULT and sort chips numerically by lowest IOMMU group ID."""
    test_program, test_dir, etc_dir = environment
    sysfs_dir = test_dir / "mock_tpu7x"
    _build_mock_sysfs(sysfs_dir, "0x0076", cores_per_chip=2)

    stderr = _run_discovery(test_program, etc_dir, sysfs_dir)
    gres_lines = [line for line in stderr.splitlines() if "GRES[tpu]" in line]

    assert (
        len(gres_lines) == 4
    ), f"Expected 4 GRES[tpu] chip records from 8 PCI functions, got:\n{stderr}"
    for line in gres_lines:
        assert "Type:tpu7x" in line, f"Expected a tpu7x chip: {line}"
        assert "Cores(96):0-95" in line, f"Expected the chip on CPUs 0-95: {line}"
        assert "AUTODETECT" in line, f"Expected an autodetected record: {line}"
        assert "HAS_MULT" in line, f"Expected a multi-core chip: {line}"

    vfio_pairs = [
        re.search(r"File:(/dev/vfio/\d+,/dev/vfio/\d+)", line).group(1)
        for line in gres_lines
    ]
    assert vfio_pairs == [
        "/dev/vfio/8,/dev/vfio/9",
        "/dev/vfio/10,/dev/vfio/11",
        "/dev/vfio/12,/dev/vfio/13",
        "/dev/vfio/14,/dev/vfio/15",
    ], f"Expected numeric IOMMU group ordering, got: {vfio_pairs}"


def test_long_local_cpulist(environment):
    """Verify a local_cpulist longer than 255 bytes is read in full."""
    test_program, test_dir, etc_dir = environment
    sysfs_dir = test_dir / "mock_long_cpulist"
    cpulist = ",".join(str(cpu) for cpu in range(96))
    assert len(cpulist) > 255, "The cpulist should be long enough to truncate"
    _build_mock_sysfs(sysfs_dir, "0x006f", cores_per_chip=1, cpulist=cpulist)

    stderr = _run_discovery(test_program, etc_dir, sysfs_dir)
    gres_lines = [line for line in stderr.splitlines() if "GRES[tpu]" in line]

    assert len(gres_lines) == 4, f"Expected 4 GRES[tpu] records, got:\n{stderr}"
    for line in gres_lines:
        cores = re.search(r"Cores\(96\):(\S+)", line)
        assert cores, f"Expected a Cores field in record: {line}"
        assert (
            cores.group(1) == cpulist
        ), f"Expected the full local_cpulist, got: {cores.group(1)}"


def test_empty_sysfs_discovers_nothing(environment):
    """Verify a node with no TPU PCI devices returns 0 records cleanly."""
    test_program, test_dir, etc_dir = environment
    sysfs_dir = test_dir / "mock_empty"
    sysfs_dir.mkdir(parents=True, exist_ok=True)

    stderr = _run_discovery(test_program, etc_dir, sysfs_dir)
    assert "GRES[tpu]" not in stderr, "An empty sysfs should have no TPU records"


def test_autodetect_off_suppresses_sysfs_scan(environment):
    """Verify AutoDetect=off in gres.conf suppresses sysfs TPU discovery."""
    test_program, test_dir, _ = environment
    sysfs_dir = test_dir / "mock_autodetect_off"
    _build_mock_sysfs(sysfs_dir, "0x0076", cores_per_chip=2)

    etc_off = test_dir / "etc_off"
    etc_off.mkdir(parents=True, exist_ok=True)
    (etc_off / "slurm.conf").write_text(SLURM_CONF)
    (etc_off / "tpu.yaml").write_text(TPU_YAML)
    (etc_off / "gres.conf").write_text("AutoDetect=off\n")

    stderr = _run_discovery(test_program, etc_off, sysfs_dir)
    assert (
        "AutoDetect=off, not scanning for TPUs" in stderr
    ), "gres/tpu should skip the sysfs scan when AutoDetect=off"
    assert "GRES[tpu]" not in stderr, "No TPU records should be found"
