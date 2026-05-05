############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################

import atf
import pytest

# (archive-dump keyword, slurmdbd.conf parameter)
purge_types = [
    ("Events", "PurgeEventAfter"),
    ("Jobs", "PurgeJobAfter"),
    ("Reservations", "PurgeResvAfter"),
    ("Steps", "PurgeStepAfter"),
    ("Suspend", "PurgeSuspendAfter"),
    ("TXN", "PurgeTXNAfter"),
    ("Usage", "PurgeUsageAfter"),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_accounting(modify=True)
    for _, cfg_param in purge_types:
        atf.require_config_parameter(cfg_param, "1month", source="slurmdbd")
    atf.require_slurm_running()


# Ticket 24753: Verify that `sacctmgr archive dump` succeeds when a data type
# is given with no duration on the command line. Expected behavior is that
# slurmdbd falls back to the Purge<Type>After value from slurmdbd.conf.
@pytest.mark.parametrize("keyword,cfg_param", purge_types)
def test_archive_dump_uses_configured_purge(keyword, cfg_param):
    result = atf.run_command(
        f"sacctmgr -i archive dump Directory={atf.module_tmp_path} {keyword}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    assert result["exit_code"] == 0, (
        f"`archive dump {keyword}` failed without a duration; "
        f"expected fallback to {cfg_param}. stderr={result['stderr']!r}"
    )
    assert "Problem dumping archive" not in result["stderr"], (
        f"slurmdbd reported an archive dump problem for {keyword}: "
        f"{result['stderr']!r}"
    )
