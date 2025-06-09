############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf


def test_check_conmgr():
    """Verify that conmgr test runs"""
    atf.run_check_test("test_148_1.c")
