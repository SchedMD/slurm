############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest

node_count = 4


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_nodes(node_count)
    atf.require_slurm_running()


def test_exclusive(tmp_path):
    file_in = str(tmp_path / "exclusive.in")
    atf.make_bash_script(
        file_in,
        f"""
for i in $(seq {node_count}); do
    srun -n1 --exclusive sleep infinity &
done
wait
    """,
    )
    job_id = atf.submit_job_sbatch(f"-N{node_count} -t2 {file_in}", fatal=True)
    nodes = set()
    for i in range(node_count):
        atf.wait_for_step(job_id, i, fatal=True)
        step_str = f"{job_id}.{i}"
        nodes.add(atf.get_steps(step_str)[step_str].get("NodeList"))

    assert len(nodes) == node_count, f"Verify that all steps are in different nodes"
