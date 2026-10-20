############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test HealthCheckNodeState=CYCLE preserves the health-check interval."""

import shlex
from pathlib import Path

import pytest

import atf

pytestmark = pytest.mark.slow

health_check_interval = 30


@pytest.fixture(scope="module", autouse=True)
def setup(records):
    atf.require_version(
        (26, 11),
        "sbin/slurmctld",
        reason="Issue 51187: CYCLE health checks preserve the interval",
    )
    # Enough nodes for several chunks, making an extra interval measurable.
    atf.require_nodes(100)
    recorder = records / "recorder.sh"
    atf.make_bash_script(
        recorder,
        f'date +%s >> "{records}/${{SLURMD_NODENAME}}"\n',
    )
    atf.require_config_parameter("HealthCheckProgram", str(recorder))
    atf.require_config_parameter("HealthCheckInterval", health_check_interval)
    atf.require_config_parameter("HealthCheckNodeState", "CYCLE,ANY")
    atf.require_slurm_running()
    return records / sorted(atf.get_nodes(fatal=True))[0]


@pytest.fixture(scope="module")
def records():
    # Daemons need an absolute path independent of their working directory.
    records = Path("health_check_records").resolve()
    records.mkdir()
    yield records


def test_cycle_repeats_without_an_extra_interval(setup):
    """A node's next check is due from cycle start, not its final chunk."""
    record = setup
    # Startup runs are not periodic health checks.
    record.unlink(missing_ok=True)
    timestamps = []
    # Allow two complete cycles even when the extra-interval bug is present.
    for _ in atf.timer(timeout=4 * health_check_interval, fatal=True):
        if not record.exists():
            continue
        text = atf.run_command_output(
            f"cat {shlex.quote(str(record))}", user="root", fatal=True, quiet=True
        )
        if text.endswith("\n") and len(text.splitlines()) >= 2:
            timestamps = [int(value) for value in text.splitlines()[:2]]
            break

    elapsed = timestamps[1] - timestamps[0]
    # Allow dispatch jitter, but not another sweep's worth of chunk delays.
    assert elapsed <= health_check_interval + 10, (
        f"CYCLE health checks were {elapsed}s apart with "
        f"HealthCheckInterval={health_check_interval}"
    )
