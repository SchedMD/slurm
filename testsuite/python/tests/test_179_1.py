############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import os
import re

import pytest

import atf

unknown_option = "nonexistent_option"
srun_only_option = "pty"
job_name = "t25956"

# One rejected assignment of each kind, then a read-back proving the hook
# kept running. Returning slurm.ERROR makes sbatch exit before submitting.
cli_filter_lua = f"""
function slurm_cli_setup_defaults(options, early)
    return slurm.SUCCESS
end
function slurm_cli_pre_submit(options, offset)
    options["{unknown_option}"] = "x"
    options["{srun_only_option}"] = "x"
    options["job-name"] = "{job_name}"
    slurm.log_info("continued: job-name=%s", tostring(options["job-name"]))
    return slurm.ERROR
end
function slurm_cli_post_submit(offset, jobid, stepid)
end
"""


def cli_filter_lua_available():
    """Return True when cli_filter_lua.so is installed in PluginDir."""
    prefix = atf.properties["slurm-prefix"]
    # get_config_parameter() casefolds values, which would mangle the path.
    plugin_dir = next(
        (
            v
            for k, v in atf.get_config(live=False).items()
            if k.casefold() == "plugindir"
        ),
        f"{prefix}/lib/slurm:{prefix}/lib64/slurm",
    )
    return any(
        os.path.isfile(f"{directory}/cli_filter_lua.so")
        for directory in plugin_dir.split(":")
    )


@pytest.fixture(scope="module")
def slurm_conf():
    """Private slurm.conf enabling cli_filter/lua; the running cluster is untouched."""
    atf.require_version(
        (26, 11),
        "bin/sbatch",
        reason="cli_filter/lua logs rejected option assignments since 26.11",
    )
    if not cli_filter_lua_available():
        pytest.skip("Requires the cli_filter/lua plugin")

    lua_path = atf.module_tmp_path / "cli_filter.lua"
    lua_path.write_text(cli_filter_lua)

    contents = atf.run_command_output(
        f"cat {atf.properties['slurm-config-dir']}/slurm.conf", fatal=True, quiet=True
    )
    contents = re.sub(r"(?im)^\s*CliFilter(Plugins|Parameters)\s*=.*$", "", contents)
    conf_path = atf.module_tmp_path / "slurm.conf"
    conf_path.write_text(
        f"{contents}\nCliFilterPlugins=lua\n"
        f"CliFilterParameters=cli_filter_lua_path={lua_path}\n"
    )
    return conf_path


def test_rejected_assignment_logs_and_continues(slurm_conf):
    """Ticket 25956: a rejected assignment logs the option name, the hook continues.

    The hook assigns an unknown option, an option that exists only for srun,
    and a valid one. sbatch must log the first two, apply the third, reach the
    end of the hook, and then stop because the hook returns slurm.ERROR.
    """
    result = atf.run_command(
        "sbatch --wrap=true", env_vars=f"SLURM_CONF={slurm_conf}", xfail=True
    )
    stderr = result["stderr"]

    assert result["exit_code"] != 0, "sbatch must stop when the hook returns ERROR"
    assert "cli_filter plugin terminated with error" in stderr
    assert f"Unable to set option '{unknown_option}'" in stderr
    assert f"Unable to set option '{srun_only_option}'" in stderr
    assert "Unable to set option 'job-name'" not in stderr
    assert f"continued: job-name={job_name}" in stderr
