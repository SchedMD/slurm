############################################################################
# Copyright (C) 2026 IndraPutrabinH.
############################################################################
"""Zero tasks per socket in an API submission must not crash cons_tres."""

from pathlib import Path

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("submits a zero tasks-per-socket value through the API")
    atf.require_tool("gcc")
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_nodes(1, [("CPUs", 4)])
    atf.require_slurm_running()


@pytest.fixture(scope="module")
def submit_program(tmp_path_factory):
    program = tmp_path_factory.mktemp("submit") / "submit"
    atf.compile_against_libslurm(
        str(Path(__file__).with_suffix(".c")), str(program), fatal=True
    )
    return program


@pytest.mark.parametrize(
    "cpus,tasks_per_socket",
    [(1, 0), (2, 0), (2, 1), (2, 2), (2, 65534), (2, 65535)],
    ids=["one-cpu-zero", "two-cpu-zero", "one", "two", "unspecified", "infinite"],
)
def test_tasks_per_socket(submit_program, cpus, tasks_per_socket):
    """Zero and sentinel values leave the socket limit unspecified."""
    output = atf.run_command_output(
        f"{submit_program} {cpus} {tasks_per_socket}", fatal=True
    )
    job_id = int(output.strip())
    atf.properties["submitted-jobs"].append(job_id)
    atf.wait_for_job_state(job_id, "COMPLETED", fatal=True)
    assert atf.get_job_parameter(job_id, "ExitCode") == "0:0"
