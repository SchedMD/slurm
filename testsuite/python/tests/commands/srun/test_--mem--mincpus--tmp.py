############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest


# Setup
@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_slurm_running()


class Test_mem_mincpus_tmp:

    def test_mincpus(self):
        """Verify node configuration specification --mincpus option"""

        # Submit a job with invalid mincpus requirement
        min_cpus = 999999
        exit_code = atf.run_command_exit(f"srun --mincpus={min_cpus} true")
        assert exit_code != 0, "srun ran with an invalid value for --mincpus"

        # Submit a job with valid mincpus requirement
        min_cpus = 1
        exit_code = atf.run_command_exit(f"srun --mincpus={min_cpus} true")
        assert exit_code == 0, f"srun did not run with a valid value for --mincpus"


    def test_mem(self):
        """Verify node configuration specification --mem option"""

        # Submit a job with invalid mem requirement
        mem = 999999
        exit_code = atf.run_command_exit(f"srun --mem={mem} true")
        assert exit_code != 0, "srun ran with an invalid value for --mem"

        # Submit a job with valid mem requirement
        mem = 1
        exit_code = atf.run_command_exit(f"srun --mem={mem} true")
        assert exit_code == 0, "srun did not run with a valid value for --mem"


    def test_tmp(self):
        """Verify node configuration specification --tmp option"""

        # Submit a job with invalid tmp requirement
        tmp_disk = 999999
        exit_code = atf.run_command_exit(f"srun --tmp={tmp_disk} true")
        assert exit_code != 0, "srun ran with an invalid value for --tmp"

        # Submit a job with valid tmp requirement
        tmp_disk = 0
        exit_code = atf.run_command_exit(f"srun --tmp={tmp_disk} true")
        assert exit_code == 0, f"srun did not run with a valid value for --tmp"
