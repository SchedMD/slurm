############################################################################
# Copyright (C) SchedMD LLC.
############################################################################

import atf
from os import stat
import pytest
import re

cpu_total_cnt = 6
license_total_cnt = 50
shard_total_cnt = 7
custom_gres_total_cnt = 7
license_task_cnt = 7

# Dictionary with tres to be tested. Meant to make it easier to add more tres
# in the future. "count_needed" says if a number is required to be given to
# --tres-per-task or not
tres_dict = {
    "cpu": {"acct": "cpu=", "count_needed": True, "param": "cpu:", "num": 3},
    "gpu": {"acct": "gres/gpu=", "count_needed": False, "param": "gres/gpu:", "num": 1},
    "shard": {
        "acct": "gres/shard=",
        "count_needed": False,
        "param": "gres/shard:",
        "num": 3,
    },
    # Note that we do want to use "license" and "gres" words to ensure that the
    # logic handling license/ and gres/ are correct.
    "license": {
        "acct": "license/testing_license=",
        "count_needed": False,
        "param": "license/testing_license:",
        "num": license_task_cnt,
    },
    "custom_gres": {
        "acct": "gres/custom_gres=",
        "count_needed": False,
        "param": "gres/custom_gres:",
        "num": 2,
    },
}


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_auto_config("Wants to create custom gres.conf")
    # Requiring tty0 and tty1 to act as fake GPUs
    atf.require_tty(0)
    atf.require_tty(1)
    atf.require_config_parameter(
        "Name",
        {
            "gpu": {"File": "/dev/tty[0-1]"},
            "shard": {"Count": shard_total_cnt},
            "custom_gres": {"Count": custom_gres_total_cnt},
        },
        source="gres",
    )
    # Accounting is needed in order to keep track of tres
    atf.require_accounting()

    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter_includes(
        "Licenses", f"testing_license:{license_total_cnt}"
    )
    atf.require_config_parameter_includes(
        "AccountingStorageTRES",
        "gres/gpu,gres/shard,gres/custom_gres,license/testing_license",
    )
    atf.require_config_parameter_includes("GresTypes", "cpu")
    atf.require_config_parameter_includes("GresTypes", "gpu")
    atf.require_config_parameter_includes("GresTypes", "shard")
    atf.require_config_parameter_includes("GresTypes", "custom_gres")

    # Setup fake GPUs in gres.conf and add nodes to use the GPUs in slurm.conf
    atf.require_nodes(
        1,
        [
            (
                "Gres",
                f"gpu:2,shard:{shard_total_cnt},custom_gres:{custom_gres_total_cnt}",
            ),
            ("CPUs", cpu_total_cnt),
        ],
    )

    atf.require_slurm_running()


# Make job file that will output environmental information to a text file
def make_info_file(uniqueIdentifier):
    info_file = str(atf.module_tmp_path) + uniqueIdentifier
    job_file = info_file + ".sh"

    # Create bash script to output step information to a file
    atf.make_bash_script(
        job_file,
        f"""
        echo $(env) > {info_file}
        scontrol show lic >> {info_file}
    """,
    )

    return job_file, info_file


# Assure 'SLURM_TRES_PER_TASK' appears appropriately in environmental variables
@pytest.mark.parametrize("name", tres_dict.keys())
@pytest.mark.parametrize("command", ["salloc", "srun", "sbatch"])
def test_env_vars(name, command):
    tres_params = [f"{tres_dict[name]['param']}{tres_dict[name]['num']}"]

    # If this tres works without a count, test without a count as well
    if not tres_dict[name]["count_needed"]:
        tres_params.append(tres_dict[name]["param"][:-1])

    for tres_param in tres_params:
        job_file, info_file = make_info_file(f"{name}_{command}")
        job_param = f"--tres-per-task={tres_param} -n1"

        # Create a job
        job_id = atf.submit_job(
            command, job_param, job_file, wrap_job=False, fatal=True
        )
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

        # Get the SLURM_TRES_PER_TASK value
        env_value = re.search(
            rf"SLURM_TRES_PER_TASK=(\S*)",
            atf.run_command_output(f"cat {info_file}", fatal=True),
        ).group(1)

        # Check environmental variable "SLURM_TRES_PER_TASK" was set correctly
        assert (
            env_value == tres_param
        ), f"Environmental variable 'SLURM_TRES_PER_TASK' set to '{env_value}' instead of '{tres_param}' when running {command} command with parameters '{job_param}'"


# Check '--tres-per-task' and '--licenses' behavior together
@pytest.mark.parametrize("command", ["salloc", "srun", "sbatch"])
def test_license_allocation(command):
    # Check all licenses are available before starting
    assert (
        match := re.search(
            r"Free=(\d+)",
            atf.run_command_output("scontrol show lic testing_license", fatal=True),
        )
    ) is not None and (
        available_lics := int(match.group(1))
    ) == license_total_cnt, f"Got {available_lics} testing_license licenses free instead of all free ({license_total_cnt})"

    tres_param = f"{tres_dict['license']['param']}{tres_dict['license']['num']}"

    job_file, info_file = make_info_file(f"license_{command}")
    job_param = (
        f"--tres-per-task={tres_param} "
        + f"--licenses=testing_license:{license_task_cnt} -n2"
    )

    # Create a job
    job_id = atf.submit_job(command, job_param, job_file, wrap_job=False, fatal=True)
    atf.wait_for_job_state(job_id, "DONE", fatal=True)

    # Get how many licenses were used during each job submission
    output = atf.run_command_output(f"cat {info_file}", fatal=True)
    used_lics = available_lics - int(re.search(r"Free=(\d+)", output).group(1))

    assert (
        used_lics == license_task_cnt * 3
    ), f"{used_lics} licenses were used instead of {license_task_cnt * 3} when running: {command} {job_param} {job_file}"


# Check requested TRES when requesting allowed amount in job step
@pytest.mark.parametrize("name", tres_dict.keys())
def test_step_tres_legal(name):
    env_file = atf.module_tmp_path / (name + "_env")
    tres_params = [f"{tres_dict[name]['param']}{tres_dict[name]['num']}"]

    # If this tres works without a count, test without a count as well
    if not tres_dict[name]["count_needed"]:
        tres_params.append(tres_dict[name]["param"][:-1])

    for tres_param in tres_params:
        # Submit job with job step requesting all TRES allocated to batch
        sbatch_job_id = atf.submit_job_sbatch(
            f"--tres-per-task={tres_param} -n1 "
            f"--wrap 'srun -n1 --tres-per-task={tres_param} env > {env_file}'",
            fatal=True,
        )
        atf.wait_for_job_state(sbatch_job_id, "DONE", fatal=True)

        # Get the SLURM_TRES_PER_TASK value
        env_value = re.search(
            rf"SLURM_TRES_PER_TASK=(\S*)",
            atf.run_command_output(f"cat {env_file}", fatal=True),
        ).group(1)

        # Check job step environmental variable "SLURM_TRES_PER_TASK" correct
        assert env_value == tres_param, (
            f"Environmental variable 'SLURM_TRES_PER_TASK' set to '{env_value}' instead of '{tres_param}' for job step with a legal --tres-per-task value when running:\n"
            f"--tres-per-task={tres_param} -n1 --wrap 'srun -n1 --tres-per-task={tres_param} env > {env_file}'"
        )


# Check requested TRES when exceeding allowed amount or illegal format
@pytest.mark.parametrize("name", tres_dict.keys())
def test_step_tres_illegal(name):
    env_file = atf.module_tmp_path / (name + "_env")
    tres_param = f"{tres_dict[name]['param']}{tres_dict[name]['num']}"

    # Submit job with job step requesting twice as much TRES than that
    # allocated to batch
    sbatch_job_id = atf.submit_job_sbatch(
        f"--tres-per-task={tres_param} "
        "-n1 "
        "--wrap '"
        "srun -n1 "
        f"--tres-per-task={tres_dict[name]['param']}"
        f"{tres_dict[name]['num'] * 2} "
        f"env > {env_file} 2>&1"
        "'",
        fatal=True,
    )
    atf.wait_for_job_state(sbatch_job_id, "DONE", fatal=True)

    # Check job step failed (or licenses used the sbatch value with a warning)
    assert (
        re.search(
            rf"srun: error: (Unable to create step for job {sbatch_job_id})"
            r"|(Ignoring --tres-per-task license specification)",
            atf.run_command_output(f"cat {env_file}", fatal=True),
        )
        is not None
    ), (
        "Job step with illegal --tres-per-task did not throw an error at step creation:\n"
        f"sbatch --tres-per-task={tres_param} -n1 --wrap 'srun -n1 --tres-per-task={tres_dict[name]['param']}{tres_dict[name]['num'] * 2} env > {env_file} 2>&1'"
    )


# Test 'scontrol show job' shows correct accounting for salloc, srun, and sbatch
@pytest.mark.parametrize("name", tres_dict.keys())
@pytest.mark.parametrize("command", ["salloc", "srun", "sbatch"])
def test_accounting(name, command):
    tres_params = [f"{tres_dict[name]['param']}{tres_dict[name]['num']}"]
    expected_cnts = [tres_dict[name]["num"]]

    # If this tres works without a count, test with no count and keep track of
    # expected accounting
    if not tres_dict[name]["count_needed"]:
        tres_params.append(tres_dict[name]["param"][:-1])
        expected_cnts.append(1)

    for tres_param, expected_count in zip(tres_params, expected_cnts):
        job_param = f"--tres-per-task={tres_param} -n1"

        # Create a job
        job_id = atf.submit_job(command, job_param, "hostname", fatal=True)
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

        # Retrieve job, which includes TRES accounting statistics
        show = atf.run_command_output(f"scontrol show job {job_id}", fatal=True)

        reqTRES = re.search(rf"ReqTRES=(\S*)", show).group(1)
        allocTRES = re.search(rf"AllocTRES=(\S*)", show).group(1)

        # Assert accounting TRES records match with what we requested
        assert (
            f"{tres_dict[name]['acct']}{expected_count}" in reqTRES
        ), f"Can't find '{tres_dict[name]['acct']}{expected_count}' in ReqTRES of '{reqTRES}' when running {command} command with parameters '{job_param}'"
        assert (
            f"{tres_dict[name]['acct']}{expected_count}" in allocTRES
        ), f"Can't find '{tres_dict[name]['acct']}{expected_count}' in AllocTRES of '{allocTRES}' when running {command} command with parameters '{job_param}'"


# Make sure TRES is appropriately deallocated after jobs finish
@pytest.mark.parametrize("name", tres_dict.keys())
@pytest.mark.parametrize("command", ["salloc", "srun", "sbatch"])
def test_tres_leaking(name, command):
    tres_params = [f"{tres_dict[name]['param']}{tres_dict[name]['num']}"]

    # If this tres works without a count, test without a count as well
    if not tres_dict[name]["count_needed"]:
        tres_params.append(tres_dict[name]["param"][:-1])

    for tres_param in tres_params:
        job_param = f"--tres-per-task={tres_param} -n1"

        # Create a job
        job_id = atf.submit_job(command, job_param, "hostname", fatal=True)
        atf.wait_for_job_state(job_id, "DONE", fatal=True)

        # Get AllocTRES from each node
        allocTRES_list = atf.run_command_output(
            "scontrol show nodes | grep AllocTRES", fatal=True
        ).split("\n")

        # Assure TRES released after job (originally found in Bug 16356)
        assert all(
            [
                allocTRES.strip() == "AllocTRES=" or allocTRES == ""
                for allocTRES in allocTRES_list
            ]
        ), (
            f"One or more node AllocTRES's aren't empty after the job has completed (See Bug 16356)\n"
            f"AllocTRES list: {allocTRES_list}"
        )


# Make sure no commas between TRES doesn't work
@pytest.mark.parametrize("name", tres_dict.keys())
def test_no_commas(name):
    env_file = atf.module_tmp_path / (name + "_env")
    tres_params = [f"{tres_dict[name]['param']}{tres_dict[name]['num']}"]

    # If this tres works without a count, test without a count as well
    if not tres_dict[name]["count_needed"]:
        tres_params.append(tres_dict[name]["param"][:-1])

    for tres_param in tres_params:
        job_param = f"--tres-per-task={tres_param * 2} -n1 hostname"

        # Test salloc
        assert not atf.submit_job_salloc(
            job_param, xfail=True
        ), f"salloc job created with no separating commas in --tres-per-task running: salloc {job_param}"
        # Test srun
        assert not atf.submit_job_srun(
            job_param, xfail=True
        ), f"srun job created with no separating commas in --tres-per-task running: srun {job_param}"
        # Test sbatch
        assert not atf.submit_job_sbatch(
            f"--tres-per-task={tres_param * 2} " "-n1 --wrap 'hostname'", xfail=True
        ), f"sbatch job created with no separating commas in --tres-per-task running: sbatch --tres-per-task={tres_param * 2} -n1 --wrap 'hostname'"
        # Test sbatch's step. Note sbatch is given legal --tres-per-task value
        job_id = atf.submit_job_sbatch(
            f"--tres-per-task={tres_param} -n1 " f"--wrap 'srun {job_param}'",
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "DONE", fatal=True)
        assert (
            atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
            == "FAILED"
        ), f"sbatch should have failed running with illegal step --tres-per-task: sbatch --tres-per-task={tres_param} -n1 --wrap 'srun {job_param}'"


# Make sure having extra colons and/or nothing between colons doesn't work
@pytest.mark.parametrize("name", tres_dict.keys())
def test_extra_colons(name):
    env_file = atf.module_tmp_path / (name + "_env")

    # Replace each slash with two
    tres_params = [
        f"{tres_dict[name]['param']}{tres_dict[name]['num']}".replace(
            "/", "//"
        ).replace(":", "::")
    ]

    # If this tres works without a count, replace slash to old colon
    if not tres_dict[name]["count_needed"]:
        # Replace each colon with two in new tres_param
        tres_params.append(tres_dict[name]["param"][:-1].replace("/", "//"))

    for tres_param in tres_params:
        job_param = f"--tres-per-task={tres_param} -n1 hostname"

        # Test salloc
        assert not atf.submit_job_salloc(
            job_param, xfail=True
        ), f"Job created with extra colons in --tres-per-task running: salloc {job_param}"
        # Test srun
        assert not atf.submit_job_srun(
            job_param, xfail=True
        ), f"Job created with extra colons in --tres-per-task running: srun {job_param}"
        # Test sbatch
        assert not atf.submit_job_sbatch(
            f"--tres-per-task={tres_param} -n1 " "--wrap 'hostname'", xfail=True
        ), f"Job created with extra colons in --tres-per-task running: sbatch --tres-per-task={tres_param} -n1 --wrap 'hostname'"
        # Test sbatch's step. Note sbatch is given legal --tres-per-task value
        job_id = atf.submit_job_sbatch(
            f"--tres-per-task={tres_param.replace('//', '/').replace('::',':')} -n1 "
            f"--wrap 'srun {job_param}'",
            fatal=True,
        )
        atf.wait_for_job_state(job_id, "DONE", fatal=True)
        assert (
            atf.get_job_parameter(job_id, "JobState", default="NOT_FOUND", quiet=True)
            == "FAILED"
        ), f"sbatch should have failed when its step had an illegal tres-per-task running: sbatch --tres-per-task={tres_param.replace('::', '/')} -n1 --wrap 'srun {job_param}'"
