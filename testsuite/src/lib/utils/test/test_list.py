############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os, re

# SchedMD
from ..cmds import (
    perform,
    run_cmd,
)
from ..log import (
    log,
)


def get_unit_tl_from_build_dir(build_unit_base_dir):
    return perform("Running Make(s)", setup_unit_tests, build_unit_base_dir)


def setup_unit_tests(build_unit_base_dir):
    # Returns a list of unit tests via the Makefiles in the build/build dir
    test_list = []

    for subdir, dirs, files in os.walk(build_unit_base_dir):
        for file in files:
            if file == "Makefile":
                os.chdir(subdir)
                run_cmd("make -j", shell=True, quiet=True, print_output=False)

                cmd = f"""
                    echo 'print: ; @echo "$(TESTS)"' | make -f Makefile -f - print
                    """
                result = run_cmd(cmd, shell=True, quiet=True)
                output = result.stdout.strip().split(" ")
                if result.returncode == 0:
                    for name in output:
                        if len(name) > 0:
                            test_list.append(f"{subdir}/{name}.c")

                # NOTE: Uncomment for more verbosity if adding a -v option later
                # else:
                #    print(f"Warning bad make output: {result.stdout}{result.stderr}")

    return list(set(test_list))


def get_tl_from_dir(base_dir, re_pattern, append=""):
    file_re = re.compile(re_pattern)
    test_list = []

    for subdir, dirs, files in os.walk(base_dir):
        for file in files:
            if file in file_re.findall(file):
                test_list.append(f"{append}{file}")

    return list(set(test_list))
