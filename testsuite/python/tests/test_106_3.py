############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
import atf
import pytest
import os
import re


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()
    if atf.get_config_parameter("SlurmdUser") != "root(0)":
        pytest.skip("This test is incompatible with SlurmdUser != root")


@pytest.fixture(scope="module")
def sbcast_binary():
    """Use the sbcast binary itself as the file to broadcast"""

    return atf.run_command_output("which sbcast", fatal=True).strip()


def _run_sbcast_compress_test(sbcast_binary, compress_type, tmp_path):
    """broadcast sbcast binary with given compress type, verify integrity"""

    dest = f"/tmp/test_sbcast_compress_{compress_type}_{os.getpid()}"
    script = str(tmp_path / f"script_{compress_type}.sh")
    output = str(tmp_path / f"output_{compress_type}.txt")

    atf.make_bash_script(
        script,
        f"""
        srun rm -f {dest}
        sbcast --compress={compress_type} {sbcast_binary} {dest}
        srun diff {sbcast_binary} {dest}
        srun rm -f {dest}
    """,
    )

    job_id = atf.submit_job_sbatch(f"-N1 -t2 --output={output} {script}", fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)
    atf.wait_for_file(output, fatal=True)
    return atf.run_command_output(f"cat {output}")


def test_compress_none(sbcast_binary, tmp_path):
    """Verify sbcast --compress=none"""

    output = _run_sbcast_compress_test(sbcast_binary, "none", tmp_path)
    assert not re.search(r"differ", output)
    assert not re.search(r"[Ee]rror", output)


def test_compress_lz4(sbcast_binary, tmp_path):
    """Verify sbcast --compress=lz4"""

    output = _run_sbcast_compress_test(sbcast_binary, "lz4", tmp_path)
    assert not re.search(r"differ", output)
    assert not re.search(r"[Ee]rror", output)
