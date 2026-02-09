##########################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
##########################################################################
import os

import pytest

import atf

from .conftest import cls1, cls2, cls3, run_sprio_test, submit_fed_job

pytestmark = pytest.mark.slow


def test_federated_sprio_output(federation_setup, cleanup):
    """Test federated sprio output."""
    original_sprio_format = os.environ.pop("SPRIO_FORMAT", None)

    # ji3 stays federation-wide so it has sibling records on all 3 clusters.
    ji0 = submit_fed_job(cls1)
    atf.wait_for_job_state(ji0, "RUNNING", siblings=True, clusters=cls1, fatal=True)
    ji1 = submit_fed_job(cls2)
    atf.wait_for_job_state(ji1, "RUNNING", siblings=True, clusters=cls2, fatal=True)
    ji2 = submit_fed_job(cls3)
    atf.wait_for_job_state(ji2, "RUNNING", siblings=True, clusters=cls3, fatal=True)
    ji3 = submit_fed_job()
    ji4 = submit_fed_job(cls1)
    ji5 = submit_fed_job(cls2)
    ji6 = submit_fed_job(cls3)

    # Test basic sprio
    run_sprio_test("", f"\\s+({ji6}|{ji5}|{ji4}|{ji3}).+\\n", 4)

    # Test sprio with sibling option
    run_sprio_test("--sibling", f"({ji3}|{ji4}|{ji5}|{ji6})", 6)

    # Test local and cluster-specific sprio
    # Each cluster has the federation-wide job and its own one, and no other
    for options, jobs in (
        ("--local", (ji3, ji4)),
        (f"-M{cls1}", (ji3, ji4)),
        (f"-M{cls2}", (ji3, ji5)),
        (f"-M{cls3}", (ji3, ji6)),
    ):
        for jid in jobs:
            run_sprio_test(options, f"\\s+{jid}\\b.+\\n", 1)
        run_sprio_test(options, "\\s+\\d+.+\\n", len(jobs))

    if original_sprio_format is not None:
        os.environ["SPRIO_FORMAT"] = original_sprio_format
