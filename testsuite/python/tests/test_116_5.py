############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import re

node_num = 3


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_num + 2)
    atf.require_config_parameter("FrontendName", None)
    atf.require_slurm_running()


def write_host_file(matchs):
    host_file = atf.module_tmp_path / "host_file"
    with open(host_file, "w") as hf:
        for line in matchs:
            hf.write(line[1] + "\n")


def test_hostfile():
    """Test of hostfile option (-hostfile)."""

    host_file = atf.module_tmp_path / "host_file"
    HOSTFILE_ENV = "SLURM_HOSTFILE"

    output = atf.run_job_output(f"-N{node_num} -l printenv SLURMD_NODENAME", fatal=True)
    matches = re.findall(r"(\d+): (\S+)", output)
    matches += [matches.pop(0)]
    match_ordered = []
    for iter in range(len(matches)):
        match_ordered.append((str(iter), matches[iter][1]))
    write_host_file(matches)

    # Test pass 1
    output = atf.run_job_output(
        f"-l --distribution=arbitrary printenv SLURMD_NODENAME",
        env_vars=f"{HOSTFILE_ENV}={host_file}",
        fatal=True,
    )
    matches = re.findall(r"(\d+): (\S+)", output)
    for match in matches:
        assert (
            match in match_ordered
        ), f"On pass 1 Task {match[0]} not distributed by hostfile {match} not in {match_ordered}"
    assert (
        len(matches) == node_num
    ), f"On pass 1 {len(matches)} tasks were ran instead of {node_num} -- one task per node in the hostfile"

    matches += [matches.pop(0)]
    match_ordered = []
    for iter in range(len(matches)):
        match_ordered.append((str(iter), matches[iter][1]))
    write_host_file(matches)

    # Test pass 2
    output = atf.run_job_output(
        f"-l --distribution=arbitrary printenv SLURMD_NODENAME",
        env_vars=f"{HOSTFILE_ENV}={host_file}",
        fatal=True,
    )
    matches = re.findall(r"(\d+): (\S+)", output)
    for match in matches:
        assert (
            match in match_ordered
        ), f"On pass 2 Task {match[0]} not distributed by hostfile {match} not in {match_ordered}"
    assert (
        len(matches) == node_num
    ), f"On pass 2 {len(matches)} tasks were ran instead of {node_num} -- one task per node in the hostfile"
