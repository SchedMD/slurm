############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import pytest
import atf

test_user = atf.properties["test-user"]
slurm_user = atf.properties["slurm-user"]
test_acct = "test_acct"
extra_acct = "extra_acct"
parent_acct = "parent_acct"
restricted_acct = "restricted_acct"


TEST_CASES = [
    dict(
        name="user_match1",
        res_account=None,
        res_user=test_user,
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="user_match2",
        res_account=None,
        res_user=f"{test_user},{slurm_user}",
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="user_match3",
        res_account=None,
        res_user=f"{slurm_user},{test_user}",
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="no_user_match",
        res_account=None,
        res_user=slurm_user,
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="account_match1",
        res_account=test_acct,
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="account_match2",
        res_account=f"{test_acct},{restricted_acct}",
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="account_match3",
        res_account=f"{restricted_acct},{test_acct}",
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="no_account_match",
        res_account=restricted_acct,
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="parent_account_match",
        res_account=parent_acct,
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="deny_user1",
        res_account=None,
        res_user=f"-{test_user}",
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="deny_user2",
        res_account=None,
        res_user=f"-{test_user},-{slurm_user}",
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="deny_user3",
        res_account=None,
        res_user=f"-{slurm_user}",
        expect_invalid_res=False,
        expect_show_res=True,
        expect_job_in_res=True,
    ),
    dict(
        name="acct_match_deny_user",
        res_account=test_acct,
        res_user=f"-{test_user}",
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="deny_acct",
        res_account=f"-{test_acct}",
        res_user=None,
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="user_match_deny_acct",
        res_account=f"-{test_acct}",
        res_user=test_user,
        expect_invalid_res=False,
        expect_show_res=False,
        expect_job_in_res=False,
    ),
    dict(
        name="mixed_access_invalid_res1",
        res_account=None,
        res_user=f"-{test_user},{slurm_user}",
        expect_invalid_res=True,
    ),
    dict(
        name="mixed_access_invalid_res2",
        res_account=None,
        res_user=f"{test_user},-{slurm_user}",
        expect_invalid_res=True,
    ),
    dict(
        name="mixed_access_invalid_res3",
        res_account=f"{test_acct},-{restricted_acct}",
        res_user=None,
        expect_invalid_res=True,
    ),
    dict(
        name="mixed_access_invalid_res4",
        res_account=f"-{test_acct},{restricted_acct}",
        res_user=None,
        expect_invalid_res=True,
    ),
]


@pytest.fixture(scope="module", autouse=True)
def setup():
    if test_user == slurm_user:
        pytest.skip(
            f"This test requires SlurmUser ({slurm_user}) and SlurmTestUser ({test_user}) to be different.",
            allow_module_level=True,
        )

    atf.require_accounting(modify=True)
    atf.require_config_parameter_includes("PrivateData", "reservations")
    atf.require_config_parameter_includes("AccountingStorageEnforce", "associations")
    atf.require_slurm_running()


@pytest.fixture(scope="module", autouse=True)
def setup_db():
    accounts = [
        f"{parent_acct}",
        f"{restricted_acct}",
        f"{test_acct} parent={parent_acct}",
        f"{extra_acct}",
    ]

    users = [
        f"{slurm_user} account={restricted_acct},{extra_acct}",
        f"{test_user} account={test_acct}",
    ]

    try:
        for account in accounts:
            atf.run_command(
                f"sacctmgr -i add account {account}",
                user=slurm_user,
                fatal=True,
            )
        for user in users:
            atf.run_command(
                f"sacctmgr -i add user {user}",
                user=slurm_user,
                fatal=True,
            )

        atf.run_command("sacctmgr show assoc tree", user=slurm_user, fatal=True)
        yield
    finally:
        atf.run_command("sacctmgr show assoc tree", user=slurm_user, fatal=True)
        for user in reversed(users):
            atf.run_command(
                f"sacctmgr -i delete user {user}",
                user=slurm_user,
                fatal=False,
            )
        for account in reversed(accounts):
            atf.run_command(
                f"sacctmgr -i delete account {account}",
                user=slurm_user,
                fatal=False,
            )


@pytest.fixture
def reservation(request):
    try:
        case = request.param
        res_name = f"res_{case['name']}"

        # Create reservation
        res_args = [
            f"reservationname={res_name}",
            "start=now",
            "duration=10",
            "nodecnt=1",
        ]
        if case.get("res_user"):
            res_args.append(f"user={case['res_user']}")
        if case.get("res_account"):
            res_args.append(f"account={case['res_account']}")

        create_cmd = f"scontrol create {' '.join(res_args)}"
        result = atf.run_command(create_cmd, user=slurm_user)
        if case["expect_invalid_res"]:
            assert (
                result["exit_code"] != 0
            ), f"Should not be able to create invalid reservation {res_name}"
            res_name = None
        else:
            assert result["exit_code"] == 0, f"Couldn't create reservation {res_name}"

        # Yield values for the test
        yield case, res_name

    finally:
        atf.cancel_all_jobs(fatal=True)

        if res_name:
            atf.run_command(
                f"scontrol delete reservationname={res_name}", user=slurm_user
            )


def show_as_slurm_user(res_name):
    show = atf.run_command(f"scontrol show reservation {res_name}", user=slurm_user)
    assert (
        f"ReservationName={res_name} StartTime=" in show["stdout"]
    ), "Reservation should always be visible as SlurmUser"


def show_as_test_user(case, res_name):
    show = atf.run_command(f"scontrol show reservation {res_name}", user=test_user)
    if case["expect_show_res"]:
        assert (
            f"ReservationName={res_name} StartTime=" in show["stdout"]
        ), "Reservation should be visible"
    else:
        assert (
            f"ReservationName={res_name} StartTime=" not in show["stdout"]
        ), "Reservation should not be visible"


def run_job_as_slurm_user(res_name):
    run = atf.run_command(
        f"srun -N1 --account={restricted_acct} --reservation={res_name} true",
        user=slurm_user,
    )
    assert run["exit_code"] == 0, "Job should have been allowed in reservation"


def run_job_as_test_user(case, res_name):
    run = atf.run_command(
        f"srun -N1  --account={test_acct} --reservation={res_name} true", user=test_user
    )
    if case["expect_job_in_res"]:
        assert run["exit_code"] == 0, "Job should have been allowed in reservation"
    else:
        assert run["exit_code"] != 0, "Job should have been denied for reservation"
        assert "Access denied" in run["stderr"]


@pytest.mark.parametrize(
    "reservation", TEST_CASES, indirect=True, ids=lambda c: c["name"]
)
def test_reservation_access(reservation):
    case, res_name = reservation

    if res_name is None:
        return

    show_as_slurm_user(res_name)
    show_as_test_user(case, res_name)
    run_job_as_slurm_user(res_name)
    run_job_as_test_user(case, res_name)
