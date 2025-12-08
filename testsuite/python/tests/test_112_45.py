############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import pytest
import getpass
import json
import random
import logging
import time
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
req_node_count = 10


@pytest.fixture(scope="module", autouse=True)
def setup():
    global slurmrestd_url, token, slurmrestd
    global local_cluster_name, partition_name

    atf.require_accounting(modify=True)
    atf.require_nodes(req_node_count)
    atf.require_config_parameter("AllowNoDefAcct", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes", source="slurmdbd")
    atf.require_config_parameter("TrackWCKey", "Yes")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt")
    atf.require_config_parameter("AuthAltTypes", "auth/jwt", source="slurmdbd")
    atf.require_slurmrestd("slurmctld,slurmdbd,util", "v0.0.45")
    atf.require_version((26, 5), "sbin/slurmdbd")
    atf.require_version((26, 5), "sbin/slurmctld")
    atf.require_version((26, 5), "sbin/slurmrestd")
    atf.require_slurm_running()

    # Setup OpenAPI client with OpenAPI-Generator once Slurm(restd) is running
    atf.require_openapi_generator("7.3.0")

    # Uncomment the following two lines to generate a new openapi_spec.json
    # file, then it could be moved to the testsuite_data_dir if we really
    # want to change the OpenAPI specs after .0.
    # with open("openapi_spec.json", "w") as f:
    #     json.dump(atf.properties["openapi_spec"], f, indent=2)

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
def non_admin(setup):
    atf.run_command(
        f"sacctmgr -i add user {local_cluster_name} defaultaccount=root",
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


@pytest.fixture(scope="function")
def dynamic_node(setup):
    """
    Sets the required MaxNodeCount + cons_tres, and returns a non-existing node.
    """
    nonexistent_node_name = "nonexistent_node"
    config = atf.get_config()

    atf.stop_slurm()
    atf.require_config_parameter("SelectType", "select/cons_tres")
    atf.require_config_parameter("SelectTypeParameters", "CR_CPU")
    atf.require_config_parameter("MaxNodeCount", str(req_node_count + 1))
    atf.start_slurm()

    atf.run_command(
        f"scontrol delete node {nonexistent_node_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
        xfail=True,
    )

    yield nonexistent_node_name

    atf.run_command(
        f"scontrol delete node {nonexistent_node_name}",
        user=atf.properties["slurm-user"],
        fatal=False,
    )

    atf.stop_slurm()
    atf.require_config_parameter("SelectType", config["SelectType"])
    atf.require_config_parameter("SelectTypeParameters", config["SelectTypeParameters"])
    atf.require_config_parameter("MaxNodeCount", None)
    atf.start_slurm()


def test_loaded_versions():
    r = atf.request_slurmrestd("openapi/v3")
    assert r.status_code == 200

    spec = r.json()

    # verify older plugins are not loaded
    assert "/slurm/v0.0.44/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.43/jobs" not in spec["paths"].keys()
    assert "/slurm/v0.0.42/jobs" not in spec["paths"].keys()
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
    assert "/slurm/v0.0.45/jobs/" in spec["paths"].keys()
    assert "/slurmdb/v0.0.45/jobs/" in spec["paths"].keys()


@pytest.mark.skipif(
    atf.get_version() <= (26, 5, 0), reason="Specs may change until .0 is released"
)
@pytest.mark.parametrize("openapi_spec", ["45"], indirect=True)
def test_specification(openapi_spec):
    if atf.get_version("sbin/slurmrestd") >= (27, 11):
        # This is expected to be deprecated in 27.11+
        patch = atf.get_deprecated_openapi_spec_patch(openapi_spec)
        patch.apply(openapi_spec, in_place=True)

    atf.assert_openapi_spec_eq(openapi_spec, atf.properties["openapi_spec"])


def test_db_accounts(slurm, slurmdb, create_wckeys, admin_level):
    from openapi_client import ApiClient as Client
    from openapi_client import Configuration as Config
    from openapi_client.models.v0045_openapi_accounts_resp import (
        V0045OpenapiAccountsResp,
    )
    from openapi_client.models.v0045_account import V0045Account
    from openapi_client.models.v0045_assoc_short import V0045AssocShort
    from openapi_client.models.v0045_coord import V0045Coord

    # make sure account doesn't already exist
    resp = slurmdb.slurmdb_v0045_get_account_with_http_info(account_name)
    assert resp.status_code == 200
    assert len(resp.data.accounts) == 0
    resp = slurmdb.slurmdb_v0045_get_account_with_http_info(account2_name)
    assert resp.status_code == 200
    assert len(resp.data.accounts) == 0

    # create account
    accounts = V0045OpenapiAccountsResp(
        accounts=[
            V0045Account(
                description="test description",
                name=account_name,
                organization="test organization",
            ),
            V0045Account(
                coordinators=[
                    V0045Coord(
                        name=coord_name,
                    )
                ],
                description="test description",
                name=account2_name,
                organization="test organization",
            ),
        ]
    )
    resp = slurmdb.slurmdb_v0045_post_accounts_with_http_info(accounts)
    assert resp.status_code == 200

    accounts = V0045OpenapiAccountsResp(
        accounts=[
            V0045Account(
                name=account2_name,
                description="fail description",
                organization="fail organization",
            )
        ]
    )
    resp = slurmdb.slurmdb_v0045_post_accounts_with_http_info(accounts)
    assert resp.status_code == 200

    # verify account matches modify request
    resp = slurmdb.slurmdb_v0045_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.flags

    # change account desc and org
    accounts = V0045OpenapiAccountsResp(
        accounts=[
            V0045Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )
    resp = slurmdb.slurmdb_v0045_post_accounts(accounts)
    assert not resp.warnings
    assert len(resp.errors) == 0
    resp = slurmdb.slurmdb_v0045_get_account(account2_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account2_name
        assert account.description == accounts.accounts[0].description
        assert account.organization == accounts.accounts[0].organization
        assert not account.coordinators

    resp = slurmdb.slurmdb_v0045_get_account(account_name)
    assert resp.accounts
    for account in resp.accounts:
        assert account.name == account_name

    # check full listing works
    resp = slurmdb.slurmdb_v0045_get_accounts(deleted="true")
    assert resp.accounts
    resp = slurmdb.slurmdb_v0045_get_accounts()
    assert resp.accounts

    accounts = V0045OpenapiAccountsResp(
        accounts=[
            V0045Account(
                coordinators=[],
                description="test description modified",
                name=account2_name,
                organization="test organization modified",
            )
        ]
    )

    resp = slurmdb.slurmdb_v0045_delete_account(account_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_delete_account(account2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_account(account_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts

    resp = slurmdb.slurmdb_v0045_get_account(account2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.accounts


def test_db_diag(slurmdb, admin_level):
    resp = slurmdb.slurmdb_v0045_get_diag()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.statistics.time_start > 0


def test_db_wckeys(slurmdb, create_coords, admin_level):
    from openapi_client.models.v0045_wckey import V0045Wckey
    from openapi_client.models.v0045_openapi_wckey_resp import V0045OpenapiWckeyResp

    wckeys = V0045OpenapiWckeyResp(
        wckeys=[
            V0045Wckey(
                cluster=local_cluster_name,
                name=wckey_name,
                user=user_name,
            ),
            V0045Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=user_name,
            ),
            V0045Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=coord_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_wckeys_with_http_info(
        v0045_openapi_wckey_resp=wckeys
    )
    assert resp.status_code == 200

    resp = slurmdb.slurmdb_v0045_get_wckeys()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert len(resp.wckeys) >= 1

    resp = slurmdb.slurmdb_v0045_get_wckey(wckey_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey_name or wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurmdb.slurmdb_v0045_get_wckey(wckey2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.wckeys
    for wckey in resp.wckeys:
        assert wckey.name == wckey2_name
        assert wckey.user == user_name or wckey.user == coord_name

    resp = slurmdb.slurmdb_v0045_delete_wckey(wckey_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_delete_wckey(wckey2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_wckey(wckey_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert len(resp.wckeys) == 0

    resp = slurmdb.slurmdb_v0045_get_wckey(wckey2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert len(resp.wckeys) == 0


def test_db_clusters(slurmdb, admin_level):
    from openapi_client.models.v0045_openapi_clusters_resp import (
        V0045OpenapiClustersResp,
    )
    from openapi_client.models.v0045_cluster_rec import V0045ClusterRec

    clusters = V0045OpenapiClustersResp(
        clusters=[
            V0045ClusterRec(
                name=cluster_name,
            ),
            V0045ClusterRec(
                name=cluster2_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_clusters(v0045_openapi_clusters_resp=clusters)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_clusters()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters

    resp = slurmdb.slurmdb_v0045_get_cluster(cluster_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster_name
        assert not cluster.nodes

    resp = slurmdb.slurmdb_v0045_get_cluster(cluster2_name)
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.clusters
    for cluster in resp.clusters:
        assert cluster.name == cluster2_name
        assert not cluster.nodes

    resp = slurmdb.slurmdb_v0045_delete_cluster(cluster_name)
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_delete_cluster(cluster2_name)
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_cluster(cluster_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters

    resp = slurmdb.slurmdb_v0045_get_cluster(cluster2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.clusters


def test_db_users(slurmdb, admin_level):
    from openapi_client.models.v0045_openapi_users_resp import V0045OpenapiUsersResp
    from openapi_client.models.v0045_assoc_short import V0045AssocShort
    from openapi_client.models.v0045_coord import V0045Coord
    from openapi_client.models.v0045_user import V0045User
    from openapi_client.models.v0045_user_default import V0045UserDefault
    from openapi_client.models.v0045_wckey import V0045Wckey

    users = V0045OpenapiUsersResp(
        users=[
            V0045User(
                administrator_level=["None"],
                default=dict(
                    wckey=wckey_name,
                ),
                name=user_name,
            ),
            V0045User(
                administrator_level=["Operator"],
                wckeys=[
                    V0045Wckey(
                        cluster=local_cluster_name, name=wckey_name, user=coord_name
                    ),
                    V0045Wckey(
                        cluster=local_cluster_name,
                        name=wckey2_name,
                        user=coord_name,
                    ),
                ],
                default=V0045UserDefault(
                    wckey=wckey2_name,
                ),
                name=coord_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_users(v0045_openapi_users_resp=users)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_users()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.users

    # Using query parameters (i.e. with_wckeys/with_deleted) results in warnings
    # Slurmrestd expected OpenAPI type=boolean but got OpenAPI type=string

    resp = slurmdb.slurmdb_v0045_get_user(user_name, with_wckeys="true")
    if resp.warnings:
        assert len(resp.warnings) == 1
        assert resp.warnings[0].source == "#/with_wckeys/"
    assert len(resp.errors) == 0
    assert resp.users
    for user in resp.users:
        assert user.name == user_name
        assert user.default.wckey == wckey_name

    resp = slurmdb.slurmdb_v0045_get_user(coord_name, with_wckeys="true")
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

    resp = slurmdb.slurmdb_v0045_delete_user(coord_name)
    assert len(resp.errors) == 0

    user_exists = False
    resp = slurmdb.slurmdb_v0045_get_user(coord_name, with_deleted="true")
    assert len(resp.errors) == 0
    for user in resp.users:
        assert user.name == coord_name
        assert user.flags[0] == "DELETED"
        user_exists = True

    if not user_exists:
        users = V0045OpenapiUsersResp(
            users=[
                V0045User(
                    administrator_level=["Administrator"],
                    default=dict(
                        wckey=wckey_name,
                    ),
                    old_name=user_name,
                    name=coord_name,
                )
            ]
        )

        resp = slurmdb.slurmdb_v0045_post_users(v0045_openapi_users_resp=users)
        assert not resp.warnings
        assert len(resp.errors) == 0

        resp = slurmdb.slurmdb_v0045_get_user(coord_name, with_wckeys="true")
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

        resp = slurmdb.slurmdb_v0045_delete_user(coord_name)
        assert len(resp.errors) == 0

        resp = slurmdb.slurmdb_v0045_get_user(coord_name)
        assert len(resp.warnings) > 0
        assert len(resp.errors) == 0
        assert not resp.users


def test_db_assoc(slurmdb, create_coords, create_qos, admin_level):
    from openapi_client.models.v0045_openapi_assocs_resp import V0045OpenapiAssocsResp
    from openapi_client.models.v0045_assoc import V0045Assoc
    from openapi_client.models.v0045_assoc_short import V0045AssocShort
    from openapi_client.models.v0045_coord import V0045Coord
    from openapi_client.models.v0045_user import V0045User
    from openapi_client.models.v0045_wckey import V0045Wckey

    from openapi_client.models.v0045_uint32_no_val_struct import (
        V0045Uint32NoValStruct as V0045Uint32NoVal,
    )

    associations = V0045OpenapiAssocsResp(
        associations=[
            V0045Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0045Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0045Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                partition=partition_name,
                priority=V0045Uint32NoVal(number=9, set=True),
                qos=[qos_name, qos2_name],
                shares_raw=23,
                user=user_name,
            ),
            V0045Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0045Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0045Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                priority=V0045Uint32NoVal(number=9, set=True),
                qos=[qos_name, qos2_name],
                shares_raw=23,
                user=user_name,
            ),
            V0045Assoc(
                account=account2_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos2_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0045Uint32NoVal(
                                set=True,
                                number=50,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0045Uint32NoVal(
                        set=True,
                        number=4,
                    )
                ),
                partition=partition_name,
                priority=V0045Uint32NoVal(number=90, set=True),
                qos=[qos2_name],
                shares_raw=1012,
                user=user_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_associations(
        v0045_openapi_assocs_resp=associations
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_associations()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.associations

    resp = slurmdb.slurmdb_v0045_get_association(
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

    associations = V0045OpenapiAssocsResp(
        associations=[
            V0045Assoc(
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
                        per=dict(wall_clock=V0045Uint32NoVal(set=True, number=250)),
                    ),
                ),
                min=dict(
                    priority_threshold=V0045Uint32NoVal(set=True, number=100),
                ),
                priority=V0045Uint32NoVal(number=848, set=True),
                shares_raw=230,
            )
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_associations(
        v0045_openapi_assocs_resp=associations
    )
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_association(
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

    resp = slurmdb.slurmdb_v0045_delete_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_association(
        cluster=local_cluster_name,
        account=account_name,
        user=user_name,
        partition=partition_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurmdb.slurmdb_v0045_delete_associations(
        cluster=local_cluster_name,
        user=user_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_delete_associations(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_association(
        cluster=local_cluster_name,
        account=account_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations

    resp = slurmdb.slurmdb_v0045_delete_associations(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_association(
        cluster=local_cluster_name,
        account=account2_name,
    )
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.associations


def test_db_qos(slurmdb, create_coords, admin_level):
    from openapi_client.models.v0045_qos import V0045Qos
    from openapi_client.models.v0045_tres import V0045Tres
    from openapi_client.models.v0045_openapi_slurmdbd_qos_resp import (
        V0045OpenapiSlurmdbdQosResp,
    )
    from openapi_client.models.v0045_float64_no_val_struct import (
        V0045Float64NoValStruct as V0045Float64NoVal,
    )

    from openapi_client.models.v0045_uint32_no_val_struct import (
        V0045Uint32NoValStruct as V0045Uint32NoVal,
    )

    qos = V0045OpenapiSlurmdbdQosResp(
        qos=[
            V0045Qos(
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
                                    V0045Tres(
                                        type="cpu",
                                        count=100,
                                    ),
                                    V0045Tres(
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
                    exempt_time=V0045Uint32NoVal(set=True, number=199),
                ),
                priority=V0045Uint32NoVal(number=180, set=True),
                usage_factor=V0045Float64NoVal(
                    set=True,
                    number=82382.23823,
                ),
                usage_threshold=V0045Float64NoVal(
                    set=True,
                    number=929392.33,
                ),
            ),
            V0045Qos(
                description="test QOS 2",
                name=qos2_name,
            ),
        ]
    )

    resp = slurmdb.slurmdb_v0045_post_qos(v0045_openapi_slurmdbd_qos_resp=qos)
    assert not resp.warnings
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_qos()
    assert not resp.warnings
    assert len(resp.errors) == 0
    assert resp.qos

    resp = slurmdb.slurmdb_v0045_get_single_qos(qos_name)
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

    resp = slurmdb.slurmdb_v0045_get_single_qos(qos2_name)
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

    resp = slurmdb.slurmdb_v0045_delete_single_qos(qos_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_single_qos(qos_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.qos

    resp = slurmdb.slurmdb_v0045_delete_single_qos(qos2_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_single_qos(qos2_name)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert not resp.qos


def test_db_tres(slurmdb):
    resp = slurmdb.slurmdb_v0045_get_tres()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_db_config(slurmdb, admin_level):
    resp = slurmdb.slurmdb_v0045_get_config()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_jobs(slurm, slurmdb, non_admin):
    from openapi_client.models.v0045_job_submit_req import V0045JobSubmitReq
    from openapi_client.models.v0045_job_desc_msg import V0045JobDescMsg
    from openapi_client.models.v0045_job_info import V0045JobInfo
    from openapi_client.models.v0045_job_modify import V0045JobModify
    from openapi_client.models.v0045_job_comment import V0045JobComment
    from openapi_client.models.v0045_process_exit_code_verbose import (
        V0045ProcessExitCodeVerbose,
    )
    from openapi_client.models.v0045_job_modify_tres import V0045JobModifyTres
    from openapi_client.models.v0045_tres import V0045Tres
    from openapi_client.models.v0045_uint32_no_val_struct import V0045Uint32NoValStruct
    from openapi_client.models.v0045_process_exit_code_verbose_signal import (
        V0045ProcessExitCodeVerboseSignal,
    )

    from openapi_client.models.v0045_uint32_no_val_struct import (
        V0045Uint32NoValStruct as V0045Uint32NoVal,
    )
    from openapi_client.models.v0045_openapi_job_modify_req import (
        V0045OpenapiJobModifyReq,
    )

    script = "#!/bin/bash\n/bin/true"
    env = ["PATH=/bin/:/sbin/:/usr/bin/:/usr/sbin/"]

    job = V0045JobSubmitReq(
        script=script,
        job=V0045JobDescMsg(
            partition=partition_name,
            name="runjob",
            environment=env,
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0045_post_job_submit(job)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid1 = int(resp.job_id)

    resp = slurm.slurm_v0045_get_jobs()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid1
        assert job.name == "runjob"
        assert job.partition == partition_name

    resp = slurm.slurm_v0045_get_job(str(jobid1))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid1
        assert job.name == "runjob"
        assert job.partition == partition_name

    job = V0045JobSubmitReq(
        script=script,
        job=V0045JobDescMsg(
            partition=partition_name,
            name="runjob",
            environment=env,
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0045_post_job_submit(job)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid2 = int(resp.job_id)

    resp = slurm.slurm_v0045_get_job(str(jobid2))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert len(resp.jobs) == 1
    for job in resp.jobs:
        assert job.job_id == jobid2
        assert job.name == "runjob"
        assert job.partition == partition_name

    # submit a HELD job to be able to update it
    job = V0045JobSubmitReq(
        script=script,
        job=V0045JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            priority=V0045Uint32NoVal(number=0, set=True),
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0045_post_job_submit(job)
    assert len(resp.warnings) > 0
    assert len(resp.errors) == 0
    assert resp.job_id
    assert resp.step_id
    jobid3 = int(resp.job_id)

    job = V0045JobDescMsg(
        environment=env,
        partition=partition_name,
        name="updated test job",
        priority=V0045Uint32NoVal(number=0, set=True),
    )

    resp = slurm.slurm_v0045_post_job(str(jobid3), v0045_job_desc_msg=job)
    assert not len(resp.warnings)
    assert not len(resp.errors)
    if resp.results:
        for result in resp.results:
            assert result.job_id == jobid3
            assert result.error_code == 0

    resp = slurm.slurm_v0045_get_job(str(jobid3))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid3
        assert job.name == "updated test job"
        assert job.partition == partition_name
        assert job.priority.set
        assert job.priority.number == 0
        assert job.user_name == local_user_name

    resp = slurm.slurm_v0045_delete_job(str(jobid3))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0045_get_job(str(jobid3))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    for job in resp.jobs:
        assert job.job_id == jobid3
        assert job.name == "updated test job"
        assert job.partition == partition_name
        assert job.user_name == local_user_name
        assert job.job_state == ["CANCELLED"]

    # Ensure that job is in the DB before querying it
    atf.wait_for_job_accounted(jobid3, fatal=True)

    resp = slurmdb.slurmdb_v0045_get_jobs()
    assert len(resp.errors) == 0

    requery = True
    while requery:
        resp = slurmdb.slurmdb_v0045_get_job(str(jobid3))
        assert len(resp.warnings) == 0
        assert len(resp.errors) == 0
        assert resp.jobs
        for job in resp.jobs:
            if job.name != "updated test job":
                # job change hasn't settled at slurmdbd yet
                requery = True
            else:
                requery = False
                assert job.job_id == jobid3
                assert job.name == "updated test job"
                assert job.partition == partition_name

    resp = slurmdb.slurmdb_v0045_get_jobs(users=local_user_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.jobs
    for job in resp.jobs:
        assert job.user == local_user_name

    # Update job in db -- posting to /job/jobid/
    atf.wait_for_job_accounted(jobid1, "End", fatal=True)
    atf.wait_for_job_accounted(jobid2, "End", fatal=True)

    atf.run_command(
        f"sacctmgr -i mod user {local_cluster_name} set AdminLevel=Admin",
        user=atf.properties["slurm-user"],
        fatal=True,
    )

    WIFEXIT_CODE = V0045Uint32NoValStruct(
        set=True, infinite=False, number=4  # 1024 >> 8
    )

    job_modify = V0045JobModify(
        comment=V0045JobComment(
            administrator="admin1",
            job="job1",
            system="system1",
        ),
        derived_exit_code=V0045ProcessExitCodeVerbose(
            return_code=V0045Uint32NoValStruct(set=True, infinite=False, number=1024),
        ),
        extra="extra1",
        tres=V0045JobModifyTres(
            allocated=[V0045Tres(type="energy", count=12345, id=3)]
        ),
        wckey="mywckey1",
    )

    resp = slurmdb.slurmdb_v0045_post_job(str(jobid1), v0045_job_modify=job_modify)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_jobs(step=str(jobid1))
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert len(resp.jobs) == 1
    for job in resp.jobs:
        logging.debug(job)
        assert job.comment == job_modify.comment
        assert job.derived_exit_code.return_code == WIFEXIT_CODE
        assert job.extra == job_modify.extra
        found_tres = False
        for tres in job.tres.allocated:
            if tres.type == "energy":
                found_tres = True
                tres == job_modify.tres.allocated[0]
        assert found_tres
        assert job.wckey.wckey == job_modify.wckey

    # Update jobs in db -- posting to /jobs/
    # Update values to be different
    WIFEXIT_CODE = V0045Uint32NoValStruct(
        set=True, infinite=False, number=8  # 2048 >> 8
    )

    job_modify = V0045JobModify(
        comment=V0045JobComment(
            administrator="admin2",
            job="job2",
            system="system2",
        ),
        derived_exit_code=V0045ProcessExitCodeVerbose(
            return_code=V0045Uint32NoValStruct(set=True, infinite=False, number=2048),
        ),
        extra="extra2",
        tres=V0045JobModifyTres(
            allocated=[V0045Tres(type="energy", count=54321, id=3)]
        ),
        wckey="mywckey2",
    )

    job_modify_req = V0045OpenapiJobModifyReq(
        job_id_list=[str(jobid1), str(jobid2)], job_rec=job_modify
    )
    resp = slurmdb.slurmdb_v0045_post_jobs(v0045_openapi_job_modify_req=job_modify_req)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurmdb.slurmdb_v0045_get_jobs(job_name="runjob")
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert len(resp.jobs) == 2
    for job in resp.jobs:
        logging.debug(job)
        assert job.comment == job_modify.comment
        assert job.derived_exit_code.return_code == WIFEXIT_CODE
        assert job.extra == job_modify.extra
        found_tres = False
        for tres in job.tres.allocated:
            if tres.type == "energy":
                found_tres = True
                tres == job_modify.tres.allocated[0]
        found_tres = True
        assert job.wckey.wckey == job_modify.wckey


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


def test_partitions(slurm):
    resp = slurm.slurm_v0045_get_partition(partition_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions
    for part in resp.partitions:
        assert part.name == partition_name

    resp = slurm.slurm_v0045_get_partitions()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.partitions


def test_nodes(slurm, admin_level):
    from openapi_client.models.v0045_update_node_msg import V0045UpdateNodeMsg

    node_name = None
    reasonuid = None
    resp = slurm.slurm_v0045_get_nodes()
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
        pytest.fail("No idle nodes are found")

    if reasonuid is None or len(reasonuid) <= 0:
        reasonuid = local_user_name

    node = V0045UpdateNodeMsg(
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

    resp = slurm.slurm_v0045_post_node(node_name, v0045_update_node_msg=node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0045_get_node(node_name)
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
    node = V0045UpdateNodeMsg(
        comment=ncomment,
        extra=extra,
        features=feat,
        features_act=actfeat,
        state=["RESUME"],
        reason=reason,
        reason_uid=reasonuid,
    )

    resp = slurm.slurm_v0045_post_node(node_name, node)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0

    resp = slurm.slurm_v0045_get_node(node_name)
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.nodes
    for node in resp.nodes:
        assert node.name == node_name
        assert node.comment == ncomment
        assert node.extra == extra


def test_ping(slurm):
    resp = slurm.slurm_v0045_get_ping()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0


def test_diag(slurm):
    resp = slurm.slurm_v0045_get_diag()
    assert len(resp.warnings) == 0
    assert len(resp.errors) == 0
    assert resp.statistics


def test_licenses(slurm):
    resp = slurm.slurm_v0045_get_licenses()
    assert len(resp.errors) == 0


@pytest.mark.parametrize(
    "flags",
    [[], ["IGNORE_JOBS"], ["IGNORE_JOBS", "MAGNETIC"]],
)
def test_reservations(slurm, flags, admin_level):
    from openapi_client.models.v0045_reservation_mod_req import V0045ReservationModReq
    from openapi_client.models.v0045_reservation_desc_msg import V0045ReservationDescMsg
    from openapi_client.models.v0045_uint64_no_val_struct import V0045Uint64NoValStruct
    from openapi_client.models.v0045_uint32_no_val_struct import V0045Uint32NoValStruct

    resv_name = "test_resv"
    users = ["root", "atf"]
    duration = V0045Uint32NoValStruct(number=300, set=True)
    start_time = V0045Uint64NoValStruct(number=int(time.time()) + 60, set=True)
    end_time = V0045Uint64NoValStruct(
        number=start_time.number + duration.number * 60, set=True
    )
    partition = "primary"
    node_list = ["node1", "node7"]

    # Create a reservation
    logging.debug(f"Creating reservation '{resv_name}'...")
    reservation_info = V0045ReservationDescMsg(
        name=resv_name,
        users=users,
        duration=duration,
        start_time=start_time,
        node_list=node_list,
        partition=partition,
        flags=flags,
    )
    resp = slurm.slurm_v0045_post_reservations(
        V0045ReservationModReq(reservations=[reservation_info])
    )
    assert resp.reservations, f"Reservation {resv_name} should be created"
    assert not resp.warnings and not resp.errors

    # Verify the fields of the response
    retrieved_reservation = resp.reservations[0]
    assert (
        retrieved_reservation.name == resv_name
    ), f"Field 'name' should be {resv_name}"
    assert (
        retrieved_reservation.partition == partition
    ), f"Field 'partition' should be {partition}"
    assert (
        retrieved_reservation.node_list == node_list
    ), f"Field 'node_list' should be {node_list}"
    assert retrieved_reservation.users == users, f"Field 'users' should be {users}"
    assert (
        retrieved_reservation.start_time.number == start_time.number
    ), f"Field 'start_time' should be {start_time}"
    assert (
        retrieved_reservation.duration.number == duration.number
    ), f"Field 'duration' should be {duration}"
    assert (
        # SPEC_NODES is automatically added
        set([flag for flag in retrieved_reservation.flags if flag != "SPEC_NODES"])
        == set(flags)
    ), f"Field 'flags' should be {flags}"

    # Verify fields of reservation created
    resp = slurm.slurm_v0045_get_reservation(resv_name)
    assert len(resp.reservations) == 1, f"Reservation '{resv_name}' should be returned"

    retrieved_reservation = resp.reservations[0]
    assert (
        retrieved_reservation.name == resv_name
    ), f"Field 'name' should be {resv_name}"
    assert (
        retrieved_reservation.partition == partition
    ), f"Field 'partition' should be {partition}"
    assert (
        atf.node_range_to_list(retrieved_reservation.node_list) == node_list
    ), f"Field 'node_list' should be {node_list}"
    assert (
        retrieved_reservation.users.split(",") == users
    ), f"Field 'users' should be {users}"
    assert (
        retrieved_reservation.start_time.number == start_time.number
    ), f"Field 'start_time' should be {start_time}"
    assert (
        retrieved_reservation.end_time.number == end_time.number
    ), f"Field 'end_time' should be {end_time}"
    assert (
        # SPEC_NODES is automatically added
        set([flag for flag in retrieved_reservation.flags if flag != "SPEC_NODES"])
        == set(flags)
    ), f"Field 'flags' should be {flags}"

    # Update reservation
    new_end_time = retrieved_reservation.end_time.number + 300
    new_users = ["root"]
    new_node_list = ["node2"]
    new_start_time = V0045Uint64NoValStruct(number=int(time.time()) + 90, set=True)
    new_flags = ["IGNORE_JOBS", "MAGNETIC", "USER_DELETE"]

    update_info = V0045ReservationDescMsg(
        name=resv_name,
        end_time=V0045Uint64NoValStruct(set=True, number=new_end_time),
        users=new_users,
        node_list=new_node_list,
        start_time=new_start_time,
        flags=new_flags,
    )
    resp = slurm.slurm_v0045_post_reservation(update_info)
    assert resp.reservations, "Reservation should be updated"
    assert not resp.warnings and not resp.errors

    # Validate update
    resp = slurm.slurm_v0045_get_reservation(resv_name)
    assert len(resp.reservations) == 1, f"Reservation '{resv_name}' should be returned"

    updated = resp.reservations[0]

    assert updated.end_time.number == new_end_time
    assert updated.users.split(",") == new_users, f"Field 'users' should be {new_users}"
    assert (
        atf.node_range_to_list(updated.node_list) == new_node_list
    ), f"Field 'node_list' should be {new_node_list}"
    assert (
        updated.start_time.number == new_start_time.number
    ), f"Field 'start_time' should be {new_start_time}"
    assert (
        # SPEC_NODES is automatically added
        set([flag for flag in updated.flags if flag != "SPEC_NODES"])
        == set(new_flags)
    ), f"Field 'flags' should be {flags}"

    # Delete reservation
    resp = slurm.slurm_v0045_delete_reservation(reservation_name=resv_name)
    assert not resp.warnings and not resp.errors
    assert resv_name not in [
        r.name for r in slurm.slurm_v0045_get_reservations().reservations
    ], f"Reservation {resv_name} should be deleted"


@pytest.mark.parametrize("legal_state", ["CLOUD", "FUTURE", "EXTERNAL"])
def test_legal_node_creation(slurm, admin_level, legal_state, dynamic_node):
    from openapi_client.models.v0045_openapi_create_node_req import (
        V0045OpenapiCreateNodeReq,
    )

    request_body = V0045OpenapiCreateNodeReq(
        node_conf=f"nodename={dynamic_node} State={legal_state}"
    )

    response = slurm.slurm_v0045_post_new_node(
        v0045_openapi_create_node_req=request_body
    )

    # Validate there are no errors or warnings, and the meta data exits
    assert not response.errors
    assert not response.warnings
    assert response.meta

    # Validate the node exists and is in the correct state
    node_states = atf.get_node_parameter(dynamic_node, "state")
    assert (
        legal_state in node_states
    ), f"Dynamic node should have {legal_state} in its state only has {node_states}"
    assert (
        "DYNAMIC_NORM" in node_states
    ), "Dynamic node should have DYNAMIC_NORM in its state"


@pytest.mark.parametrize(
    "illegal_state", ["DOWN", "DRAIN", "FAIL", "FAILING", "UNKNOWN"]
)
def test_illegal_node_creation(slurm, admin_level, illegal_state, dynamic_node):
    from openapi_client.api_response import ApiResponse
    from openapi_client.exceptions import ApiException
    from openapi_client.models.v0045_openapi_create_node_req import (
        V0045OpenapiCreateNodeReq,
    )

    request_body = V0045OpenapiCreateNodeReq(
        node_conf=f"nodename={dynamic_node} State={illegal_state}"
    )

    try:
        # This should throw an exception
        slurm.slurm_v0045_post_new_node_with_http_info(
            v0045_openapi_create_node_req=request_body
        )
        assert (
            False
        ), "slurm_v0045_post_new_node_with_http_info should have raised an exception"
    except ApiException as e:
        response = slurm.api_client.deserialize(
            response=ApiResponse(data=e.body), response_type="V0045OpenapiResp"
        )
        # Validate there is an error and no warnings
        assert e.status != 200
        assert len(response.errors) == 1  # error about wrong state
        assert not response.warnings
        assert response.meta

    # Validate the node doesn't exist
    atf.run_command(
        f"scontrol show NodeName={dynamic_node}",
        user="slurm",
        xfail=True,
        fatal=True,
    )


# Test until endpoints
@pytest.fixture
def util_api(setup):
    yield atf.openapi_util()


def test_util_hostnames(util_api):
    from openapi_client.models.v0045_openapi_hostlist_req_resp import (
        V0045OpenapiHostlistReqResp,
    )

    hostnames = ["node01", "node02", "node03"]
    request_body = V0045OpenapiHostlistReqResp(hostlist="node[01-03]")

    response = util_api.util_v0045_post_hostnames(
        v0045_openapi_hostlist_req_resp=request_body
    )
    assert response.hostnames is not None
    assert isinstance(response.hostnames, list)
    assert all(isinstance(host, str) for host in response.hostnames)
    assert hostnames == response.hostnames


def test_util_hostlist(util_api):
    from openapi_client.models.v0045_openapi_hostnames_req_resp import (
        V0045OpenapiHostnamesReqResp,
    )

    request_body = V0045OpenapiHostnamesReqResp(
        hostnames=["node01", "node02", "node03"]
    )
    response = util_api.util_v0045_post_hostlist(
        v0045_openapi_hostnames_req_resp=request_body
    )
    assert response.hostlist is not None
    assert isinstance(response.hostlist, str)
    assert "node[01-03]" == response.hostlist
