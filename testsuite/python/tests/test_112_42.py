############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import getpass
import json
import random
import logging
import os

random.seed()

cluster_name = f"test-cluster-{random.randrange(0, 99999999999)}"
cluster2_name = f"{cluster_name}-2"
user_name = f"test-user-{random.randrange(0, 99999999999)}"
user_name2 = f"{user_name}-2"
local_user_name = getpass.getuser()
account_name = f"test-account-{random.randrange(0, 99999999999)}"
account2_name = f"{account_name}-2"
account3_name = f"{account_name}-3"
coord_name = f"{user_name}-commander"
wckey_name = f"test-wckey-{random.randrange(0, 99999999999)}"
wckey2_name = f"{wckey_name}-2"
qos_name = f"test-qos-{random.randrange(0, 99999999999)}"
qos2_name = f"{qos_name}-2"
resv_name = f"test-reservation-{random.randrange(0, 99999999999)}"

slurmrestd = None


@pytest.fixture(scope="module", autouse=True)
def setup():
    global slurmrestd_url, token, slurmrestd
    global local_cluster_name, partition_name

    atf.require_accounting(modify=True)
    atf.require_config_parameter("AllowNoDefAcct", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt", source="slurmdbd")
    atf.require_slurmrestd("slurmctld,slurmdbd", "v0.0.42+prefer_refs")
    atf.require_version((24, 11), "sbin/slurmdbd")
    atf.require_version((24, 11), "sbin/slurmctld")
    atf.require_version((24, 11), "sbin/slurmrestd")
    atf.require_slurm_running()

    # Setup OpenAPI client with OpenAPI-Generator once Slurm(restd) is running
    atf.require_openapi_generator("7.3.0")

    # Conf reliant variables (put here to avert --auto-config errors)
    local_cluster_name = atf.get_config_parameter("ClusterName")

    partition_name = atf.default_partition()
    if not partition_name:
        partition_name = "debug"


@pytest.fixture(scope="function", autouse=True)
def cancel_jobs(setup):
    yield
    atf.cancel_all_jobs()


@pytest.fixture(scope="function")
def slurm(setup):
    yield atf.openapi_slurm()


@pytest.fixture(scope="function")
def slurmdb(setup):
    yield atf.openapi_slurmdb()


@pytest.fixture(scope="function")
def admin_level(setup):
    atf.run_command(
        f"sacctmgr -i add user {local_cluster_name} defaultaccount=root AdminLevel=Admin",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    yield
    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i delete user {local_cluster_name}",
        user=atf.properties["slurm-user"],
    )


@pytest.fixture(scope="function")
def create_accounts():
    atf.run_command(
        f"sacctmgr -i create account {account_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"sacctmgr -i create account {account2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )

    yield

    atf.run_command(
        f"sacctmgr -i delete account {account_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"sacctmgr -i delete account {account2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function")
def create_users(create_accounts):
    atf.run_command(
        f"sacctmgr -i create user {user_name} cluster={local_cluster_name} account={account_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )

    yield

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i delete user {user_name} cluster={local_cluster_name} account={account_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function")
def create_coords(create_users):
    atf.run_command(
        f"sacctmgr -i create user {coord_name} cluster={local_cluster_name} account={account2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )

    yield

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i delete user {coord_name} cluster={local_cluster_name} account={account2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function")
def create_wckeys():
    atf.run_command(
        f"sacctmgr -i create user {user_name} cluster={local_cluster_name} wckey={wckey_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )
    atf.run_command(
        f"sacctmgr -i create user {coord_name} cluster={local_cluster_name} wckey={wckey_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.cancel_all_jobs()
    atf.run_command(
        f"sacctmgr -i delete user {user_name} cluster={local_cluster_name} wckey={wckey_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"sacctmgr -i delete user {coord_name} cluster={local_cluster_name} wckey={wckey_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


@pytest.fixture(scope="function")
def create_qos(create_coords):
    atf.run_command(
        f"sacctmgr -i create qos {qos_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"sacctmgr -i create qos {qos2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )

    yield

    atf.run_command(
        f"sacctmgr -i delete qos {qos_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )
    atf.run_command(
        f"sacctmgr -i delete qos {qos2_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


def test_loaded_versions():
    r = atf.request_slurmrestd("openapi/v3")
    assert r.status_code == 200

    spec = r.json()

    # verify older plugins are not loaded
    assert "/slurm/v0.0.41/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.40/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.39/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.38/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.38/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.37/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.37/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.36/jobs" not in spec["paths"].keys()
    assert "/slurmdb/v0.0.36/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.35/jobs" not in spec["paths"].keys()

    # verify current plugins are loaded
    assert "/slurm/v0.0.42/jobs/" in spec["paths"].keys()
    assert "/slurmdb/v0.0.42/jobs/" in spec["paths"].keys()


@pytest.mark.xfail(
    atf.get_version("sbin/slurmrestd") >= (25, 11)
    or atf.get_version("sbin/slurmrestd") <= (24, 11, 6),
    reason="Ticket 23807: Schema changed",
)
@pytest.mark.parametrize("openapi_spec", ["42"], indirect=True)
def test_specification(openapi_spec):
    atf.assert_openapi_spec_eq(openapi_spec, atf.properties["openapi_spec"])


def test_db_accounts(slurm, slurmdb, create_wckeys, admin_level):
    from openapi_client import ApiClient as Client
    from openapi_client import Configuration as Config
    from openapi_client.models.v0042_openapi_accounts_resp import (
        V0042OpenapiAccountsResp,
    )
    from openapi_client.models.v0042_account import V0042Account
    from openapi_client.models.v0042_assoc_short import V0042AssocShort
    from openapi_client.models.v0042_coord import V0042Coord

    # make sure account doesn't already exist
    resp = slurmdb.slurmdb_v0042_get_account_with_http_info(account_name)
    assert resp.status_code == 200
    assert len(resp.data.accounts) == 0
    resp = slurmdb.slurmdb_v0042_get_account_with_http_info(account2_name)
    assert resp.status_code == 200
    assert len(resp.data.accounts) == 0

    # create account
    accounts = V0042OpenapiAccountsResp(
        accounts=[
            V0042Account(
                description="test description",
                name=account_name,
                organization="test organization",
            ),
            V0042Account(
                coordinators=[
                    V0042Coord(
                        name=coord_name,
                    )
                ],
                description="test description",
                name=account2_name,
                organization="test organization",
            ),
        ]
    )
    resp = slurmdb.slurmdb_v0042_post_accounts_with_http_info(accounts)
    assert resp.status_code == 200

    accounts = V0042OpenapiAccountsResp(
        accounts=[
            V0042Account(
                name=account2_name,
                description="fail description",
                organization="fail organization",
            )
        ]
    )
    resp = slurmdb.slurmdb_v0042_post_accounts_with_http_info(accounts)
    assert resp.status_code == 200

    # verify account matches modify request
    resp = slurmdb.slurmdb_v0042_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.flags

    # change account desc and org
    accounts = V0042OpenapiAccountsResp(
        accounts=[
            V0042Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )
    resp = slurmdb.slurmdb_v0042_post_accounts(accounts)
    assert not resp.warnings
    assert len(resp.errors) == 0
    resp = slurmdb.slurmdb_v0042_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.coordinators

    resp = slurmdb.slurmdb_v0042_get_account(account_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account_name

    # check full listing works
    resp = slurmdb.slurmdb_v0042_get_accounts(deleted="true")
    assert resp.accounts
    resp = slurmdb.slurmdb_v0042_get_accounts()
    assert resp.accounts

    accounts = V0042OpenapiAccountsResp(
        accounts=[
            V0042Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )

    resp = slurmdb.slurmdb_v0042_delete_account(account_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_delete_account(account2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_account(account_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts

    resp = slurmdb.slurmdb_v0042_get_account(account2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts


def test_db_diag(slurmdb, admin_level):
    resp = slurmdb.slurmdb_v0042_get_diag()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.statistics.time_start > 0


def test_db_wckeys(slurmdb, create_coords, admin_level):
    from openapi_client.models.v0042_wckey import V0042Wckey
    from openapi_client.models.v0042_openapi_wckey_resp import V0042OpenapiWckeyResp

    wckeys = V0042OpenapiWckeyResp(
        wckeys=[
            V0042Wckey(
                cluster=local_cluster_name,
                name=wckey_name,
                user=user_name,
            ),
            V0042Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=user_name,
            ),
            V0042Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=coord_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_wckeys_with_http_info(
        v0042_openapi_wckey_resp=wckeys
    )
    assert resp.status_code == 200

    resp = slurmdb.slurmdb_v0042_get_wckeys()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert len(resp.wckeys) >= 1

    resp = slurmdb.slurmdb_v0042_get_wckey(wckey_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey_name or wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurmdb.slurmdb_v0042_get_wckey(wckey2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurmdb.slurmdb_v0042_delete_wckey(wckey_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_delete_wckey(wckey2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_wckey(wckey_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert len(resp.wckeys) == 0

    resp = slurmdb.slurmdb_v0042_get_wckey(wckey2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert len(resp.wckeys) == 0


def test_db_clusters(slurmdb, admin_level):
    from openapi_client.models.v0042_openapi_clusters_resp import (
        V0042OpenapiClustersResp,
    )
    from openapi_client.models.v0042_cluster_rec import V0042ClusterRec

    clusters = V0042OpenapiClustersResp(
        clusters=[
            V0042ClusterRec(
                name=cluster_name,
            ),
            V0042ClusterRec(
                name=cluster2_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_clusters(v0042_openapi_clusters_resp=clusters)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_clusters()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters

    resp = slurmdb.slurmdb_v0042_get_cluster(cluster_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster_name
        assert not cluster.nodes

    resp = slurmdb.slurmdb_v0042_get_cluster(cluster2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster2_name
        assert not cluster.nodes

    resp = slurmdb.slurmdb_v0042_delete_cluster(cluster_name)
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_delete_cluster(cluster2_name)
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_cluster(cluster_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters

    resp = slurmdb.slurmdb_v0042_get_cluster(cluster2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters


def test_db_users(slurmdb, admin_level):
    from openapi_client.models.v0042_openapi_users_resp import V0042OpenapiUsersResp
    from openapi_client.models.v0042_assoc_short import V0042AssocShort
    from openapi_client.models.v0042_coord import V0042Coord
    from openapi_client.models.v0042_user import V0042User
    from openapi_client.models.v0042_user_default import V0042UserDefault
    from openapi_client.models.v0042_wckey import V0042Wckey

    users = V0042OpenapiUsersResp(
        users=[
            V0042User(
                administrator_level=["None"],
                default=dict(
                    wckey=wckey_name,
                ),
                name=user_name,
            ),
            V0042User(
                administrator_level=["Operator"],
                wckeys=[
                    V0042Wckey(
                        cluster=local_cluster_name, name=wckey_name, user=coord_name
                    ),
                    V0042Wckey(
                        cluster=local_cluster_name,
                        name=wckey2_name,
                        user=coord_name,
                    ),
                ],
                default=V0042UserDefault(
                    wckey=wckey2_name,
                ),
                name=coord_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_users(v0042_openapi_users_resp=users)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_users()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.users

    # Using query parameters (i.e. with_wckeys/with_deleted) results in warnings
    # Slurmrestd expected OpenAPI type=boolean but got OpenAPI type=string

    resp = slurmdb.slurmdb_v0042_get_user(user_name, with_wckeys="true")
    if resp.warnings:
        assert len(resp.warnings) == 1
        assert resp.warnings[0].source == "#/with_wckeys/"
    assert len(resp.errors) == 0
    assert resp.users
    for user in resp.users:
        assert user.name == user_name
        assert user.default.wckey == wckey_name

    resp = slurmdb.slurmdb_v0042_get_user(coord_name, with_wckeys="true")
    if resp.warnings:
        assert len(resp.warnings) == 1
        assert resp.warnings[0].source == "#/with_wckeys/"
    assert len(resp.errors) == 0
    assert resp.users
    for user in resp.users:
        assert user.name == coord_name
        assert user.default.wckey == wckey2_name
        for wckey in user.wckeys:
            assert wckey.name == wckey_name or wckey.name == wckey2_name
            assert wckey.user == coord_name
            assert wckey.cluster == local_cluster_name

    resp = slurmdb.slurmdb_v0042_delete_user(coord_name)
    assert len(resp.errors) == 0

    user_exists = False
    resp = slurmdb.slurmdb_v0042_get_user(coord_name, with_deleted="true")
    assert len(resp.errors) == 0
    for user in resp.users:
        assert user.name == coord_name
        assert user.flags[0] == "DELETED"
        user_exists = True

    if not user_exists:
        users = V0042OpenapiUsersResp(
            users=[
                V0042User(
                    administrator_level=["Administrator"],
                    default=dict(
                        wckey=wckey_name,
                    ),
                    old_name=user_name,
                    name=coord_name,
                )
            ]
        )

        resp = slurmdb.slurmdb_v0042_post_users(v0042_openapi_users_resp=users)
        assert not resp.warnings
        assert len(resp.errors) == 0

        resp = slurmdb.slurmdb_v0042_get_user(coord_name, with_wckeys="true")
        if resp.warnings:
            assert len(resp.warnings) == 1
            assert resp.warnings[0].source == "#/with_wckeys/"
        assert len(resp.errors) == 0
        assert resp.users
        for user in resp.users:
            assert user.name == coord_name
            assert not user.old_name
            assert user.default.wckey == wckey_name
            for wckey in user.wckeys:
                assert wckey.name == wckey_name
                assert wckey.user == coord_name
                assert wckey.cluster == local_cluster_name

        resp = slurmdb.slurmdb_v0042_delete_user(coord_name)
        assert len(resp.errors) == 0

        resp = slurmdb.slurmdb_v0042_get_user(coord_name)
        assert len(resp.warnings) > 0
        assert len(resp.errors) == 0
        assert not resp.users


def test_db_assoc(slurmdb, create_coords, create_qos, admin_level):
    from openapi_client.models.v0042_openapi_assocs_resp import V0042OpenapiAssocsResp
    from openapi_client.models.v0042_assoc import V0042Assoc
    from openapi_client.models.v0042_assoc_short import V0042AssocShort
    from openapi_client.models.v0042_coord import V0042Coord
    from openapi_client.models.v0042_user import V0042User
    from openapi_client.models.v0042_wckey import V0042Wckey

    from openapi_client.models.v0042_uint32_no_val_struct import (
        V0042Uint32NoValStruct as V0042Uint32NoVal,
    )

    associations = V0042OpenapiAssocsResp(
        associations=[
            V0042Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0042Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0042Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                partition=partition_name,
                priority=V0042Uint32NoVal(number=9, set=True),
                qos=[qos_name, qos2_name],
                shares_raw=23,
                user=user_name,
            ),
            V0042Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0042Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0042Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                priority=V0042Uint32NoVal(number=9, set=True),
                qos=[qos_name, qos2_name],
                shares_raw=23,
                user=user_name,
            ),
            V0042Assoc(
                account=account2_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos2_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0042Uint32NoVal(
                                set=True,
                                number=50,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0042Uint32NoVal(
                        set=True,
                        number=4,
                    )
                ),
                partition=partition_name,
                priority=V0042Uint32NoVal(number=90, set=True),
                qos=[qos2_name],
                shares_raw=1012,
                user=user_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_associations(
        v0042_openapi_assocs_resp=associations
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_associations()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations

    resp = slurmdb.slurmdb_v0042_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations
    for assoc in resp.associations:
        assert assoc.cluster == local_cluster_name
        assert assoc.account == account_name
        assert assoc.user == user_name
        assert assoc.partition == partition_name
        assert assoc.default.qos == qos_name
        assert not assoc.flags
        assert assoc.max.jobs.per.wall_clock.set
        assert assoc.max.jobs.per.wall_clock.number == 150
        assert assoc.min.priority_threshold.set
        assert assoc.min.priority_threshold.number == 10
        assert assoc.priority.set
        assert assoc.priority.number == 9
        for qos in assoc.qos:
            assert qos == qos_name or qos == qos2_name
        assert assoc.shares_raw == 23

    associations = V0042OpenapiAssocsResp(
        associations=[
            V0042Assoc(
                account=account_name,
                cluster=local_cluster_name,
                partition=partition_name,
                user=user_name,
                default=dict(
                    qos=qos2_name,
                ),
                qos=[qos2_name],
                max=dict(
                    jobs=dict(
                        per=dict(wall_clock=V0042Uint32NoVal(set=True, number=250)),
                    ),
                ),
                min=dict(
                    priority_threshold=V0042Uint32NoVal(set=True, number=100),
                ),
                priority=V0042Uint32NoVal(number=848, set=True),
                shares_raw=230,
            )
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_associations(
        v0042_openapi_assocs_resp=associations
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations
    for assoc in resp.associations:
        assert assoc.cluster == local_cluster_name
        assert assoc.account == account_name
        assert assoc.user == user_name
        assert assoc.partition == partition_name
        assert assoc.default.qos == qos2_name
        assert not assoc.flags
        assert assoc.max.jobs.per.wall_clock.set
        assert assoc.max.jobs.per.wall_clock.number == 250
        assert assoc.min.priority_threshold.set
        assert assoc.min.priority_threshold.number == 100
        assert assoc.priority.set
        assert assoc.priority.number == 848
        for qos in assoc.qos:
            assert qos == qos2_name
        assert assoc.shares_raw == 230

    resp = slurmdb.slurmdb_v0042_delete_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurmdb.slurmdb_v0042_delete_associations(
        cluster=local_cluster_name,
        user=user_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_delete_associations(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_association(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurmdb.slurmdb_v0042_delete_associations(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_association(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations


def test_db_qos(slurmdb, create_coords, admin_level):
    from openapi_client.models.v0042_qos import V0042Qos
    from openapi_client.models.v0042_tres import V0042Tres
    from openapi_client.models.v0042_openapi_slurmdbd_qos_resp import (
        V0042OpenapiSlurmdbdQosResp,
    )
    from openapi_client.models.v0042_float64_no_val_struct import (
        V0042Float64NoValStruct as V0042Float64NoVal,
    )

    from openapi_client.models.v0042_uint32_no_val_struct import (
        V0042Uint32NoValStruct as V0042Uint32NoVal,
    )

    qos = V0042OpenapiSlurmdbdQosResp(
        qos=[
            V0042Qos(
                description="test QOS",
                flags=[
                    "PARTITION_MAXIMUM_NODE",
                    "PARTITION_TIME_LIMIT",
                    "ENFORCE_USAGE_THRESHOLD",
                    "NO_RESERVE",
                    "DENY_LIMIT",
                    "OVERRIDE_PARTITION_QOS",
                    "NO_DECAY",
                ],
                limits=dict(
                    min=dict(
                        tres=dict(
                            per=dict(
                                job=[
                                    V0042Tres(
                                        type="cpu",
                                        count=100,
                                    ),
                                    V0042Tres(
                                        type="memory",
                                        count=100000,
                                    ),
                                ],
                            ),
                        ),
                    ),
                ),
                name=qos_name,
                preempt=dict(
                    exempt_time=V0042Uint32NoVal(set=True, number=199),
                ),
                priority=V0042Uint32NoVal(number=180, set=True),
                usage_factor=V0042Float64NoVal(
                    set=True,
                    number=82382.23823,
                ),
                usage_threshold=V0042Float64NoVal(
                    set=True,
                    number=929392.33,
                ),
            ),
            V0042Qos(
                description="test QOS 2",
                name=qos2_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0042_post_qos(v0042_openapi_slurmdbd_qos_resp=qos)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_qos()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.qos

    resp = slurmdb.slurmdb_v0042_get_single_qos(qos_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.qos
    for qos in resp.qos:
        assert qos.description == "test QOS"
        assert qos.flags
        for flag in qos.flags:
            assert flag in [
                "PARTITION_MAXIMUM_NODE",
                "PARTITION_TIME_LIMIT",
                "ENFORCE_USAGE_THRESHOLD",
                "NO_RESERVE",
                "DENY_LIMIT",
                "OVERRIDE_PARTITION_QOS",
                "NO_DECAY",
            ]
        assert qos.limits.min.tres.per.job
        for tres in qos.limits.min.tres.per.job:
            assert tres.type == "cpu" or tres.type == "memory"
            if tres.type == "cpu":
                assert tres.count == 100
            if tres.type == "memory":
                assert tres.count == 100000
        assert qos.name == qos_name
        assert qos.preempt.exempt_time.set
        assert qos.preempt.exempt_time.number == 199
        assert qos.priority.set
        assert qos.priority.number == 180
        assert qos.usage_factor.set
        assert qos.usage_factor.number == 82382.23823
        assert qos.usage_threshold.set
        assert qos.usage_threshold.number == 929392.33

    resp = slurmdb.slurmdb_v0042_get_single_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.qos
    for qos in resp.qos:
        assert qos.description == "test QOS 2"
        assert not qos.flags
        assert not qos.limits.min.tres.per.job
        assert qos.name == qos2_name
        assert not qos.preempt.exempt_time.set
        assert qos.priority.set
        assert qos.priority.number == 0
        assert qos.usage_factor.set
        assert qos.usage_factor.number == 1
        assert not qos.usage_threshold.set

    resp = slurmdb.slurmdb_v0042_delete_single_qos(qos_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_single_qos(qos_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.qos

    resp = slurmdb.slurmdb_v0042_delete_single_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0042_get_single_qos(qos2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.qos


def test_db_tres(slurmdb):
    resp = slurmdb.slurmdb_v0042_get_tres()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_db_config(slurmdb, admin_level):
    resp = slurmdb.slurmdb_v0042_get_config()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


@pytest.mark.xfail(
    reason="Ticket 20394 about jobs without associations, fixed for v43+"
)
def test_jobs(slurm, slurmdb):
    from openapi_client.models.v0042_job_submit_req import V0042JobSubmitReq
    from openapi_client.models.v0042_job_desc_msg import V0042JobDescMsg
    from openapi_client.models.v0042_job_info import V0042JobInfo

    from openapi_client.models.v0042_uint32_no_val_struct import (
        V0042Uint32NoValStruct as V0042Uint32NoVal,
    )

    script = "#!/bin/bash\n/bin/true"
    env = ["PATH=/bin/:/sbin/:/usr/bin/:/usr/sbin/"]

    job = V0042JobSubmitReq(
        script=script,
        job=V0042JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0042_post_job_submit(job)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid = int(resp.job_id)

    resp = slurm.slurm_v0042_get_jobs()

    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0042_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "test job"
        assert job.partition == partition_name

    # submit a HELD job to be able to update it
    job = V0042JobSubmitReq(
        script=script,
        job=V0042JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            priority=V0042Uint32NoVal(number=0, set=True),
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0042_post_job_submit(job)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid = int(resp.job_id)

    job = V0042JobDescMsg(
        environment=env,
        partition=partition_name,
        name="updated test job",
        priority=V0042Uint32NoVal(number=0, set=True),
    )

    resp = slurm.slurm_v0042_post_job(str(jobid), v0042_job_desc_msg=job)
    assert not len(resp.warnings)
    assert not len(resp.errors)
    # Not all changes populate "results" field
    if resp.results is not None:
        for result in resp.results:
            assert result.job_id == jobid
            assert result.error_code == 0

    resp = slurm.slurm_v0042_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "updated test job"
        assert job.partition == partition_name
        assert job.priority.set
        assert job.priority.number == 0
        assert job.user_name == local_user_name

    resp = slurm.slurm_v0042_delete_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0042_get_job(str(jobid))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid
        assert job.name == "updated test job"
        assert job.partition == partition_name
        assert job.user_name == local_user_name
        assert job.job_state == ["CANCELLED"]

    # Ensure that job is in the DB before querying it
    atf.wait_for_job_accounted(jobid, fatal=True)

    resp = slurmdb.slurmdb_v0042_get_jobs()
    assert len(resp.errors) == 0

    requery = True
    while requery:
        resp = slurmdb.slurmdb_v0042_get_job(str(jobid))
        assert len(resp.warnings) == 0
        assert len(resp.errors) == 0
        assert resp.jobs
        for job in resp.jobs:
            if job.name != "updated test job":
                # job change hasn't settled at slurmdbd yet
                requery = True
            else:
                requery = False
                assert job.job_id == jobid
                assert job.name == "updated test job"
                assert job.partition == partition_name

    resp = slurmdb.slurmdb_v0042_get_jobs(users=local_user_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.jobs
    for job in resp.jobs:
        assert job.user == local_user_name


@pytest.fixture(scope="function")
def reservation(setup):
    # Ensure that ALL nodes are idle before reserving them (or scontrol may return an error)
    nodes = atf.get_nodes()
    for node in nodes:
        atf.wait_for_node_state(node, "IDLE", fatal=True)

    atf.run_command(
        f"scontrol create reservation starttime=now duration=120 user=root nodes=ALL ReservationName={resv_name}",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    yield

    atf.run_command(
        f"scontrol delete ReservationName={resv_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )


def test_resv(slurm, reservation, admin_level):
    resp = slurm.slurm_v0042_get_reservation(resv_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.reservations
    for resv in resp.reservations:
        assert resv.name == resv_name

    resp = slurm.slurm_v0042_get_reservations()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.reservations

    # Delete reservation
    if atf.get_version("sbin/slurmrestd") >= (25, 5):
        resp = slurm.slurm_v0042_delete_reservation(resv_name)
        assert not resp.warnings and not resp.errors
        assert resv_name not in [
            r.name for r in slurm.slurm_v0042_get_reservations().reservations
        ], f"Reservation {resv_name} should be deleted"


def test_partitions(slurm):
    resp = slurm.slurm_v0042_get_partition(partition_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions
    for part in resp.partitions:
        assert part.name == partition_name

    resp = slurm.slurm_v0042_get_partitions()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions


def test_nodes(slurm, admin_level):
    from openapi_client.models.v0042_update_node_msg import V0042UpdateNodeMsg

    node_name = None
    reasonuid = None
    resp = slurm.slurm_v0042_get_nodes()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        if "IDLE" in node.state:
            node_name = node.name
            # comment = node.comment
            extra = node.extra
            feat = node.features
            actfeat = node.active_features
            # state = node.state
            reason = node.reason
            reasonuid = node.reason_set_by_user
            break

    # Skip if no idle nodes are found
    if node_name is None:
        pytest.skip("No idle nodes are found")

    if reasonuid is None or len(reasonuid) <= 0:
        reasonuid = local_user_name

    node = V0042UpdateNodeMsg(
        comment="test node comment",
        extra="test node extra",
        features=[
            "feat1",
            "feat2",
            "feat3",
        ],
        features_act=[
            "feat1",
            "feat3",
        ],
        state=["DRAIN"],
        reason="testing",
        reason_uid=local_user_name,
    )

    resp = slurm.slurm_v0042_post_node(node_name, v0042_update_node_msg=node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0042_get_node(node_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        assert node.name == node_name
        assert node.comment == "test node comment"
        assert node.extra == "test node extra"
        assert "DRAIN" in node.state
        assert node.reason == "testing"
        assert node.reason_set_by_user == local_user_name

    ncomment = "test comment comment 2"
    node = V0042UpdateNodeMsg(
        comment=ncomment,
        extra=extra,
        features=feat,
        features_act=actfeat,
        state=["RESUME"],
        reason=reason,
        reason_uid=reasonuid,
    )

    resp = slurm.slurm_v0042_post_node(node_name, node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0042_get_node(node_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        assert node.name == node_name
        assert node.comment == ncomment
        assert node.extra == extra


def test_ping(slurm):
    resp = slurm.slurm_v0042_get_ping()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_diag(slurm):
    resp = slurm.slurm_v0042_get_diag()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.statistics


def test_licenses(slurm):
    resp = slurm.slurm_v0042_get_licenses()
    assert len(resp.errors) == 0
