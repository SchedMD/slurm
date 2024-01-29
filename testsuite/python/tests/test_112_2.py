############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import atf
import getpass
import json
import math
import os
import pathlib
import pytest
import re
import requests
import sys
import time
from pprint import pprint

# avoid JWT for scontrol/sacctmgr calls
if "SLURM_JWT" in os.environ:
    del os.environ["SLURM_JWT"]

cluster_name = "test-cluster-taco"
cluster2_name = "test-cluster-taco2"
user_name = "test-user-taco"
local_user_name = getpass.getuser()
account_name = "test-account-taco"
account2_name = "test-account-taco2"
coord_name = "test-user-taco-commander"
wckey_name = "test-wckey-taco"
wckey2_name = "test-wckey-taco2"
qos_name = "test-qos-taco"
qos2_name = "test-qos-taco2"
resv_name = "test-reservation-taco"


@pytest.fixture(scope="module", autouse=True)
def setup():
    global slurmrestd_url, token
    global local_cluster_name, partition_name

    # Check skips early
    if not "SLURM_TESTSUITE_SLURMRESTD_URL" in os.environ:
        pytest.skip(
            "test requires env SLURM_TESTSUITE_SLURMRESTD_URL", allow_module_level=True
        )
    slurmrestd_url = os.environ["SLURM_TESTSUITE_SLURMRESTD_URL"]

    ogc_version = atf.run_command_output("openapi-generator-cli version")
    if ogc_version == "":
        pytest.skip("test requires openapi-generator-cli", allow_module_level=True)

    m = re.match(r"^([0-9]+)[.]\S+", ogc_version)
    if not m or m.group(1) != "6":
        pytest.skip(
            "incompatible openapi-generator-cli version %s" % ogc_version,
            allow_module_level=True,
        )

    # Conf reliant variables (put here to avert --auto-config errors)
    local_cluster_name = atf.get_config_parameter("ClusterName")

    partition_name = atf.default_partition()
    if not partition_name:
        partition_name = "debug"

    atf.require_accounting()
    atf.require_slurm_running()

    token = (
        atf.run_command_output("scontrol token lifespan=600", fatal=False)
        .replace("SLURM_JWT=", "")
        .replace("\n", "")
    )
    if token == "":
        pytest.skip("auth/jwt must be configured", allow_module_level=True)

    headers = {
        "X-SLURM-USER-NAME": getpass.getuser(),
        "X-SLURM-USER-TOKEN": token,
    }
    r = requests.get("{}/openapi/v3".format(slurmrestd_url), headers=headers)

    assert r.status_code == 200

    if not "SLURM_TESTSUITE_OPENAPI_CLIENT" in os.environ:
        # allow pointing to an existing OpenAPI generated client
        pyapi_path = "{}/pyapi/".format(atf.module_tmp_path)
        spec_path = "{}/openapi.json".format(atf.module_tmp_path)
    else:
        pyapi_path = "{}/pyapi/".format(os.environ["SLURM_TESTSUITE_OPENAPI_CLIENT"])
        spec_path = "{}/openapi.json".format(
            os.environ["SLURM_TESTSUITE_OPENAPI_CLIENT"]
        )
        pathlib.Path(pyapi_path).mkdir(parents=True, exist_ok=True)

    spec = r.json()

    # verify older plugins are not loaded
    if "/slurm/v0.0.38/jobs" in spec["paths"].keys():
        pytest.skip("plugin v0.0.38 not supported", allow_module_level=True)
    if "/slurmdb/v0.0.38/jobs" in spec["paths"].keys():
        pytest.skip("plugin dbv0.0.38 not supported", allow_module_level=True)
    if "/slurm/v0.0.37/jobs" in spec["paths"].keys():
        pytest.skip("plugin v0.0.37 not supported", allow_module_level=True)
    if "/slurmdb/v0.0.37/jobs" in spec["paths"].keys():
        pytest.skip("plugin dbv0.0.37 not supported", allow_module_level=True)
    if "/slurm/v0.0.36/jobs" in spec["paths"].keys():
        pytest.skip("plugin v0.0.36 not supported", allow_module_level=True)
    if "/slurmdb/v0.0.36/jobs" in spec["paths"].keys():
        pytest.skip("plugin dbv0.0.36 not supported", allow_module_level=True)
    if "/slurm/v0.0.35/jobs" in spec["paths"].keys():
        pytest.skip("plugin v0.0.36 not supported", allow_module_level=True)

    # verify current plugins are loaded
    if not "/slurm/v0.0.39/jobs" in spec["paths"].keys():
        pytest.skip("plugin v0.0.39 required", allow_module_level=True)
    if not "/slurmdb/v0.0.39/jobs" in spec["paths"].keys():
        pytest.skip("plugin dbv0.0.39 required", allow_module_level=True)

    if not os.path.exists(spec_path):
        with open(spec_path, "w") as f:
            f.write(r.text)
            f.close()
        atf.run_command(
            "openapi-generator-cli generate -i '{}' -g python --strict-spec=true -o '{}'".format(
                spec_path, pyapi_path
            ),
            fatal=True,
            timeout=9999,
        )

    sys.path.insert(0, pyapi_path)


def purge():
    atf.run_command("scontrol delete reservation {}".format(resv_name), fatal=False)
    atf.run_command("sacctmgr -i delete wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i delete wckey {}".format(wckey2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete account {} cluster={}".format(account2_name, cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete account {} cluster={}".format(account_name, cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i delete qos {}".format(qos_name), fatal=False)
    atf.run_command("sacctmgr -i delete qos {}".format(qos2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(coord_name, cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(user_name, cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i delete user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i delete user {}".format(coord_name), fatal=False)
    atf.run_command("sacctmgr -i delete user {}".format(user_name), fatal=False)
    atf.run_command("sacctmgr -i delete cluster {}".format(cluster_name), fatal=False)
    atf.run_command("sacctmgr -i delete cluster {}".format(cluster2_name), fatal=False)


@pytest.fixture(scope="function", autouse=True)
def cleanup():
    purge()
    yield
    purge()


def test_db_accounts():
    import openapi_client
    from openapi_client import ApiClient as Client
    from openapi_client import Configuration as Config
    from openapi_client.model.dbv0039_account_info import Dbv0039AccountInfo
    from openapi_client.model.v0039_account_list import V0039AccountList
    from openapi_client.model.v0039_account import V0039Account
    from openapi_client.model.v0039_assoc_short import V0039AssocShort
    from openapi_client.model.v0039_assoc_short_list import V0039AssocShortList
    from openapi_client.model.v0039_coord import V0039Coord
    from openapi_client.model.v0039_coord_list import V0039CoordList

    slurm = get_slurm()

    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )

    # make sure account doesnt already exist
    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account2_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["accounts"]

    # create account
    accounts = Dbv0039AccountInfo(
        accounts=V0039AccountList(
            [
                V0039Account(
                    description="test description",
                    name=account_name,
                    organization="test organization",
                ),
                V0039Account(
                    coordinators=V0039CoordList(
                        [
                            V0039Coord(
                                name=coord_name,
                            )
                        ]
                    ),
                    description="test description",
                    name=account2_name,
                    organization="test organization",
                ),
            ]
        ),
    )
    resp = slurm.slurmdb_v0039_update_accounts(body=accounts)
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    # test setting DELETE flag is warned about
    accounts = Dbv0039AccountInfo(
        accounts=V0039AccountList(
            [
                V0039Account(
                    coordinators=V0039CoordList(
                        [
                            V0039Coord(
                                name=coord_name,
                            )
                        ]
                    ),
                    name=account2_name,
                    description="fail description",
                    organization="fail organization",
                    flags=["DELETED"],
                )
            ]
        ),
    )
    resp = slurm.slurmdb_v0039_update_accounts(body=accounts)
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]

    # verify account matches modifiy request
    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account2_name})
    assert resp.response.status == 200
    assert resp.body["accounts"]
    for account in resp.body["accounts"]:
        assert account["name"] == account2_name
        assert account["description"] == accounts["accounts"][0]["description"]
        assert account["organization"] == accounts["accounts"][0]["organization"]
        assert account["coordinators"]
        for coord in account["coordinators"]:
            assert coord["name"] == coord_name
        assert not account["flags"]

    # change account desc and org
    accounts = Dbv0039AccountInfo(
        accounts=V0039AccountList(
            [
                V0039Account(
                    coordinators=V0039CoordList([]),
                    description="test description modified",
                    name=account2_name,
                    organization="test organization modified",
                )
            ]
        ),
    )
    resp = slurm.slurmdb_v0039_update_accounts(body=accounts)
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account2_name})
    assert resp.response.status == 200
    assert resp.body["accounts"]
    for account in resp.body["accounts"]:
        assert account["name"] == account2_name
        assert account["description"] == accounts["accounts"][0]["description"]
        assert account["organization"] == accounts["accounts"][0]["organization"]
        assert not account["coordinators"]

    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account_name})
    assert resp.response.status == 200
    assert resp.body["accounts"]
    for account in resp.body["accounts"]:
        assert account["name"] == account_name

    # check full listing works
    resp = slurm.slurmdb_v0039_get_accounts(query_params={"with_deleted": "true"})
    assert resp.response.status == 200
    assert resp.body["accounts"]
    resp = slurm.slurmdb_v0039_get_accounts()
    assert resp.response.status == 200
    assert resp.body["accounts"]

    accounts = Dbv0039AccountInfo(
        accounts=V0039AccountList(
            [
                V0039Account(
                    coordinators=V0039CoordList([]),
                    description="test description modified",
                    name=account2_name,
                    organization="test organization modified",
                )
            ]
        ),
    )

    resp = slurm.slurmdb_v0039_delete_account(
        path_params={
            "account_name": account_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_delete_account(
        path_params={
            "account_name": account2_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["accounts"]

    resp = slurm.slurmdb_v0039_get_account(path_params={"account_name": account2_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["accounts"]


def get_slurm():
    import openapi_client
    from openapi_client.apis.tags.slurm_api import SlurmApi
    from openapi_client import ApiClient as Client
    from openapi_client import Configuration as Config

    c = Config()
    c.host = slurmrestd_url
    c.access_token = token
    return SlurmApi(Client(c))


def test_db_diag():
    import openapi_client
    from openapi_client.model.dbv0039_diag import Dbv0039Diag
    from openapi_client.model.status import Status

    slurm = get_slurm()
    resp = slurm.slurmdb_v0039_diag()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["statistics"]


def test_db_wckeys():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.v0039_wckey_list import V0039WckeyList
    from openapi_client.model.v0039_wckey import V0039Wckey
    from openapi_client.model.dbv0039_wckey_info import Dbv0039WckeyInfo

    slurm = get_slurm()

    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )

    wckeys = V0039WckeyList(
        [
            V0039Wckey(
                cluster=local_cluster_name,
                name=wckey_name,
                user=user_name,
            ),
            V0039Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=user_name,
            ),
            V0039Wckey(
                cluster=local_cluster_name,
                name=wckey2_name,
                user=coord_name,
            ),
        ]
    )

    resp = slurm.slurmdb_v0039_add_wckeys(body=Dbv0039WckeyInfo(wckeys=wckeys))
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_wckeys()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["wckeys"]

    resp = slurm.slurmdb_v0039_get_wckey(path_params={"wckey": wckey_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["wckeys"]
    for wckey in resp.body["wckeys"]:
        assert wckey["name"] == wckey_name
        assert wckey["user"] == user_name

    resp = slurm.slurmdb_v0039_get_wckey(path_params={"wckey": wckey2_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["wckeys"]
    for wckey in resp.body["wckeys"]:
        assert wckey["name"] == wckey2_name
        assert wckey["user"] == user_name or wckey["user"] == coord_name

    resp = slurm.slurmdb_v0039_delete_wckey(path_params={"wckey": wckey_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_delete_wckey(path_params={"wckey": wckey2_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_wckey(path_params={"wckey": wckey_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["wckeys"]

    resp = slurm.slurmdb_v0039_get_wckey(path_params={"wckey": wckey2_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["wckeys"]


def test_db_clusters():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.dbv0039_clusters_info import Dbv0039ClustersInfo
    from openapi_client.model.v0039_cluster_rec import V0039ClusterRec
    from openapi_client.model.v0039_cluster_rec_list import V0039ClusterRecList
    from openapi_client.model.v0039_cluster_rec import V0039ClusterRec

    slurm = get_slurm()

    clusters = V0039ClusterRecList(
        [
            V0039ClusterRec(
                name=cluster_name,
            ),
            V0039ClusterRec(
                name=cluster2_name,
            ),
        ]
    )

    resp = slurm.slurmdb_v0039_add_clusters(body=Dbv0039ClustersInfo(clusters=clusters))
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_clusters()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["clusters"]

    resp = slurm.slurmdb_v0039_get_cluster(path_params={"cluster_name": cluster_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["clusters"]
    for cluster in resp.body["clusters"]:
        assert cluster["name"] == cluster_name
        assert not cluster["nodes"]

    resp = slurm.slurmdb_v0039_get_cluster(path_params={"cluster_name": cluster2_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["clusters"]
    for cluster in resp.body["clusters"]:
        assert cluster["name"] == cluster2_name
        assert not cluster["nodes"]

    resp = slurm.slurmdb_v0039_delete_cluster(
        path_params={"cluster_name": cluster_name}
    )
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_delete_cluster(
        path_params={"cluster_name": cluster2_name}
    )
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_cluster(path_params={"cluster_name": cluster_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["clusters"]

    resp = slurm.slurmdb_v0039_get_cluster(path_params={"cluster_name": cluster2_name})
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["clusters"]


def test_db_users():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.dbv0039_update_users import Dbv0039UpdateUsers
    from openapi_client.model.v0039_assoc_short import V0039AssocShort
    from openapi_client.model.v0039_assoc_short_list import V0039AssocShortList
    from openapi_client.model.v0039_coord import V0039Coord
    from openapi_client.model.v0039_coord_list import V0039CoordList
    from openapi_client.model.v0039_user import V0039User
    from openapi_client.model.v0039_user_list import V0039UserList
    from openapi_client.model.v0039_wckey_list import V0039WckeyList
    from openapi_client.model.v0039_wckey import V0039Wckey

    slurm = get_slurm()

    atf.run_command("sacctmgr -i create wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i create wckey {}".format(wckey2_name), fatal=False)

    users = V0039UserList(
        [
            V0039User(
                administrator_level=["None"],
                default=dict(
                    wckey=wckey_name,
                ),
                name=user_name,
            ),
            V0039User(
                administrator_level=["Operator"],
                wckeys=V0039WckeyList(
                    [
                        V0039Wckey(
                            cluster=local_cluster_name, name=wckey_name, user=coord_name
                        ),
                        V0039Wckey(
                            cluster=local_cluster_name,
                            name=wckey2_name,
                            user=coord_name,
                        ),
                    ]
                ),
                default=dict(
                    wckey=wckey2_name,
                ),
                name=coord_name,
            ),
        ]
    )

    resp = slurm.slurmdb_v0039_update_users(body=Dbv0039UpdateUsers(users=users))
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_users()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["users"]

    resp = slurm.slurmdb_v0039_get_user(
        path_params={"user_name": user_name}, query_params={}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["users"]
    for user in resp.body["users"]:
        assert user["name"] == user_name
        assert user["default"]["wckey"] == wckey_name

    resp = slurm.slurmdb_v0039_get_user(
        path_params={"user_name": coord_name}, query_params={}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["users"]
    for user in resp.body["users"]:
        assert user["name"] == coord_name
        assert user["default"]["wckey"] == wckey2_name
        for wckey in user["wckeys"]:
            assert wckey["name"] == wckey_name or wckey["name"] == wckey2_name
            assert wckey["user"] == coord_name
            assert wckey["cluster"] == local_cluster_name

    resp = slurm.slurmdb_v0039_delete_user(path_params={"user_name": coord_name})
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_user(
        path_params={"user_name": coord_name}, query_params={}
    )
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["users"]

    users = V0039UserList(
        [
            V0039User(
                administrator_level=["Administrator"],
                default=dict(
                    wckey=wckey_name,
                ),
                old_name=user_name,
                name=coord_name,
            )
        ]
    )

    resp = slurm.slurmdb_v0039_update_users(body=Dbv0039UpdateUsers(users=users))
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_user(
        path_params={"user_name": coord_name}, query_params={}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["users"]
    for user in resp.body["users"]:
        assert user["name"] == coord_name
        assert not user["old_name"]
        assert user["default"]["wckey"] == wckey_name
        for wckey in user["wckeys"]:
            assert wckey["name"] == wckey_name
            assert wckey["user"] == coord_name
            assert wckey["cluster"] == local_cluster_name

    resp = slurm.slurmdb_v0039_delete_user(path_params={"user_name": coord_name})
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_user(
        path_params={"user_name": coord_name}, query_params={}
    )
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["users"]


def test_db_assoc():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.dbv0039_update_users import Dbv0039UpdateUsers
    from openapi_client.model.dbv0039_associations_info import Dbv0039AssociationsInfo
    from openapi_client.model.v0039_assoc_list import V0039AssocList
    from openapi_client.model.v0039_assoc import V0039Assoc
    from openapi_client.model.v0039_assoc_short import V0039AssocShort
    from openapi_client.model.v0039_assoc_short_list import V0039AssocShortList
    from openapi_client.model.v0039_coord import V0039Coord
    from openapi_client.model.v0039_coord_list import V0039CoordList
    from openapi_client.model.v0039_user import V0039User
    from openapi_client.model.v0039_user_list import V0039UserList
    from openapi_client.model.v0039_wckey_list import V0039WckeyList
    from openapi_client.model.v0039_wckey import V0039Wckey
    from openapi_client.model.v0039_qos_string_id_list import V0039QosStringIdList
    from openapi_client.model.v0039_uint32_no_val import V0039Uint32NoVal

    slurm = get_slurm()

    atf.run_command("sacctmgr -i create account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i create account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(user_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={}".format(coord_name, local_cluster_name),
        fatal=False,
    )
    atf.run_command("sacctmgr -i create wckey {}".format(wckey_name), fatal=False)
    atf.run_command("sacctmgr -i create wckey {}".format(wckey2_name), fatal=False)
    atf.run_command("sacctmgr -i create qos {}".format(qos_name), fatal=False)
    atf.run_command("sacctmgr -i create qos {}".format(qos2_name), fatal=False)

    associations = V0039AssocList(
        [
            V0039Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0039Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0039Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                partition=partition_name,
                priority=V0039Uint32NoVal(number=9, set=True),
                qos=V0039QosStringIdList([qos_name, qos2_name]),
                shares_raw=23,
                user=user_name,
            ),
            V0039Assoc(
                account=account_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0039Uint32NoVal(
                                set=True,
                                number=150,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0039Uint32NoVal(
                        set=True,
                        number=10,
                    )
                ),
                priority=V0039Uint32NoVal(number=9, set=True),
                qos=V0039QosStringIdList([qos_name, qos2_name]),
                shares_raw=23,
                user=user_name,
            ),
            V0039Assoc(
                account=account2_name,
                cluster=local_cluster_name,
                default=dict(
                    qos=qos2_name,
                ),
                flags=[],
                max=dict(
                    jobs=dict(
                        per=dict(
                            wall_clock=V0039Uint32NoVal(
                                set=True,
                                number=50,
                            )
                        ),
                    ),
                ),
                min=dict(
                    priority_threshold=V0039Uint32NoVal(
                        set=True,
                        number=4,
                    )
                ),
                partition=partition_name,
                priority=V0039Uint32NoVal(number=90, set=True),
                qos=V0039QosStringIdList([qos2_name]),
                shares_raw=1012,
                user=user_name,
            ),
        ]
    )

    resp = slurm.slurmdb_v0039_update_associations(
        body=Dbv0039AssociationsInfo(associations=associations)
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_associations()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["associations"]

    resp = slurm.slurmdb_v0039_get_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
            "user": user_name,
            "partition": partition_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["associations"]
    for assoc in resp.body["associations"]:
        assert assoc["cluster"] == local_cluster_name
        assert assoc["account"] == account_name
        assert assoc["user"] == user_name
        assert assoc["partition"] == partition_name
        assert assoc["default"]["qos"] == qos_name
        assert not assoc["flags"]
        assert assoc["max"]["jobs"]["per"]["wall_clock"]["set"]
        assert assoc["max"]["jobs"]["per"]["wall_clock"]["number"] == 150
        assert assoc["min"]["priority_threshold"]["set"]
        assert assoc["min"]["priority_threshold"]["number"] == 10
        assert assoc["priority"]["set"]
        assert assoc["priority"]["number"] == 9
        for qos in assoc["qos"]:
            assert qos == qos_name or qos == qos2_name
        assert assoc["shares_raw"] == 23

    associations = V0039AssocList(
        [
            V0039Assoc(
                account=account_name,
                cluster=local_cluster_name,
                partition=partition_name,
                user=user_name,
                default=dict(
                    qos=qos2_name,
                ),
                qos=V0039QosStringIdList([qos2_name]),
                max=dict(
                    jobs=dict(
                        per=dict(wall_clock=V0039Uint32NoVal(set=True, number=250)),
                    ),
                ),
                min=dict(
                    priority_threshold=V0039Uint32NoVal(set=True, number=100),
                ),
                priority=V0039Uint32NoVal(number=848, set=True),
                shares_raw=230,
            )
        ]
    )

    resp = slurm.slurmdb_v0039_update_associations(
        body=Dbv0039AssociationsInfo(associations=associations)
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
            "user": user_name,
            "partition": partition_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["associations"]
    for assoc in resp.body["associations"]:
        assert assoc["cluster"] == local_cluster_name
        assert assoc["account"] == account_name
        assert assoc["user"] == user_name
        assert assoc["partition"] == partition_name
        assert assoc["default"]["qos"] == qos2_name
        assert not assoc["flags"]
        assert assoc["max"]["jobs"]["per"]["wall_clock"]["set"]
        assert assoc["max"]["jobs"]["per"]["wall_clock"]["number"] == 250
        assert assoc["min"]["priority_threshold"]["set"]
        assert assoc["min"]["priority_threshold"]["number"] == 100
        assert assoc["priority"]["set"]
        assert assoc["priority"]["number"] == 848
        for qos in assoc["qos"]:
            assert qos == qos2_name
        assert assoc["shares_raw"] == 230

    resp = slurm.slurmdb_v0039_delete_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
            "user": user_name,
            "partition": partition_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
            "user": user_name,
            "partition": partition_name,
        }
    )
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["associations"]

    resp = slurm.slurmdb_v0039_delete_associations(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account_name,
        }
    )
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["associations"]

    resp = slurm.slurmdb_v0039_delete_associations(
        query_params={
            "cluster": local_cluster_name,
            "account": account2_name,
        }
    )
    assert resp.response.status == 200
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_association(
        query_params={
            "cluster": local_cluster_name,
            "account": account2_name,
        }
    )
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["associations"]


def test_db_qos():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.v0039_qos import V0039Qos
    from openapi_client.model.v0039_qos_list import V0039QosList
    from openapi_client.model.v0039_tres_list import V0039TresList
    from openapi_client.model.v0039_tres import V0039Tres
    from openapi_client.model.dbv0039_update_qos import Dbv0039UpdateQos
    from openapi_client.model.v0039_float64_no_val import V0039Float64NoVal
    from openapi_client.model.v0039_uint32_no_val import V0039Uint32NoVal

    slurm = get_slurm()

    atf.run_command("sacctmgr -i create account {}".format(account_name), fatal=False)
    atf.run_command("sacctmgr -i create account {}".format(account2_name), fatal=False)
    atf.run_command(
        "sacctmgr -i create user {} cluster={} acccount={}".format(
            user_name, local_cluster_name, account_name
        ),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create user {} cluster={} account={}".format(
            coord_name, local_cluster_name, account2_name
        ),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create wckey {} account={}".format(wckey_name, account_name),
        fatal=False,
    )
    atf.run_command(
        "sacctmgr -i create wckey {} account={}".format(wckey2_name, account2_name),
        fatal=False,
    )

    qos = V0039QosList(
        [
            V0039Qos(
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
                                job=V0039TresList(
                                    [
                                        V0039Tres(
                                            type="cpu",
                                            count=100,
                                        ),
                                        V0039Tres(
                                            type="memory",
                                            count=100000,
                                        ),
                                    ]
                                ),
                            ),
                        ),
                    ),
                ),
                name=qos_name,
                preempt=dict(
                    exempt_time=V0039Uint32NoVal(set=True, number=199),
                ),
                priority=V0039Uint32NoVal(number=180, set=True),
                usage_factor=V0039Float64NoVal(
                    set=True,
                    number=82382.23823,
                ),
                usage_threshold=V0039Float64NoVal(
                    set=True,
                    number=929392.33,
                ),
            ),
            V0039Qos(
                description="test QOS 2",
                name=qos2_name,
            ),
        ]
    )

    resp = slurm.slurmdb_v0039_update_qos(body=Dbv0039UpdateQos(qos=qos))
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_qos(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["qos"]

    resp = slurm.slurmdb_v0039_get_single_qos(
        query_params={}, path_params={"qos_name": qos_name}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["qos"]
    for qos in resp.body["qos"]:
        assert qos["description"] == "test QOS"
        assert qos["flags"]
        for flag in qos["flags"]:
            assert flag in [
                "PARTITION_MAXIMUM_NODE",
                "PARTITION_TIME_LIMIT",
                "ENFORCE_USAGE_THRESHOLD",
                "NO_RESERVE",
                "DENY_LIMIT",
                "OVERRIDE_PARTITION_QOS",
                "NO_DECAY",
            ]
        assert qos["limits"]["min"]["tres"]["per"]["job"]
        for tres in qos["limits"]["min"]["tres"]["per"]["job"]:
            assert tres["type"] == "cpu" or tres["type"] == "memory"
            if tres["type"] == "cpu":
                assert tres["count"] == 100
            if tres["type"] == "memory":
                assert tres["count"] == 100000
        assert qos["name"] == qos_name
        assert qos["preempt"]["exempt_time"]["set"]
        assert qos["preempt"]["exempt_time"]["number"] == 199
        assert qos["priority"]["set"]
        assert qos["priority"]["number"] == 180
        assert qos["usage_factor"]["set"]
        assert qos["usage_factor"]["number"] == 82382.23823
        assert qos["usage_threshold"]["set"]
        assert qos["usage_threshold"]["number"] == 929392.33

    resp = slurm.slurmdb_v0039_get_single_qos(
        query_params={}, path_params={"qos_name": qos2_name}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["qos"]
    for qos in resp.body["qos"]:
        assert qos["description"] == "test QOS 2"
        assert not qos["flags"]
        assert not qos["limits"]["min"]["tres"]["per"]["job"]
        assert qos["name"] == qos2_name
        assert not qos["preempt"]["exempt_time"]["set"]
        assert qos["priority"]["set"]
        assert qos["priority"]["number"] == 0
        assert qos["usage_factor"]["set"]
        assert qos["usage_factor"]["number"] == 1
        assert not qos["usage_threshold"]["set"]

    resp = slurm.slurmdb_v0039_delete_qos(path_params={"qos_name": qos_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_single_qos(
        query_params={}, path_params={"qos_name": qos_name}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["qos"]

    resp = slurm.slurmdb_v0039_delete_qos(path_params={"qos_name": qos2_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurmdb_v0039_get_single_qos(
        query_params={}, path_params={"qos_name": qos2_name}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert not resp.body["qos"]


def test_db_tres():
    import openapi_client

    slurm = get_slurm()

    resp = slurm.slurmdb_v0039_get_tres()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]


def test_db_config():
    import openapi_client

    slurm = get_slurm()

    resp = slurm.slurmdb_v0039_get_config()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]


def test_jobs():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.v0039_job_submission import V0039JobSubmission
    from openapi_client.model.v0039_job_submission_response import (
        V0039JobSubmissionResponse,
    )
    from openapi_client.model.v0039_job_desc_msg import V0039JobDescMsg
    from openapi_client.model.v0039_job_desc_msg_list import V0039JobDescMsgList
    from openapi_client.model.v0039_string_array import V0039StringArray
    from openapi_client.model.v0039_job_info import V0039JobInfo
    from openapi_client.model.v0039_uint32_no_val import V0039Uint32NoVal
    from openapi_client.model.v0039_job_info_msg import V0039JobInfoMsg

    slurm = get_slurm()

    script = "#!/bin/bash\n/bin/true"
    env = V0039StringArray(["PATH=/bin/:/sbin/:/usr/bin/:/usr/sbin/"])

    job = V0039JobSubmission(
        script=script,
        job=V0039JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0039_submit_job(body=job)
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["job_id"]
    assert resp.body["step_id"]
    jobid = int(resp.body["job_id"])

    resp = slurm.slurm_v0039_get_jobs(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurm_v0039_get_job(path_params={"job_id": str(jobid)})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    for job in resp.body["jobs"]:
        assert job["job_id"] == jobid
        assert job["name"] == "test job"
        assert job["partition"] == partition_name

    # submit a HELD job to be able to update it
    job = V0039JobSubmission(
        script=script,
        job=V0039JobDescMsg(
            partition=partition_name,
            name="test job",
            environment=env,
            priority=V0039Uint32NoVal(number=0, set=True),
            current_working_directory="/tmp/",
        ),
    )

    resp = slurm.slurm_v0039_submit_job(body=job)
    assert resp.response.status == 200
    assert resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["job_id"]
    assert resp.body["step_id"]
    jobid = int(resp.body["job_id"])

    # Disabled until v0.0.40 due double $refs not being supported
    #    job = V0039JobSubmission(
    #            job=V0039JobDescMsg(
    #                environment=env,
    #                partition=partition_name,
    #                name="updated test job",
    #                priority=V0039Uint32NoVal(number=0, set=True),
    #            )
    #    )
    #
    #    resp = slurm.slurm_v0039_update_job(
    #            path_params={'job_id': str(jobid)}, body=job)
    #    assert resp.response.status == 200
    #    assert not resp.body['warnings']
    #    assert not resp.body['errors']

    resp = slurm.slurm_v0039_get_job(path_params={"job_id": str(jobid)})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    for job in resp.body["jobs"]:
        assert job["job_id"] == jobid
        assert job["name"] == "test job"
        assert job["partition"] == partition_name
        assert job["priority"]["set"]
        assert job["priority"]["number"] == 0
        assert job["user_name"] == local_user_name

    resp = slurm.slurm_v0039_cancel_job(
        path_params={"job_id": str(jobid)},
        query_params={},
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurm_v0039_get_job(path_params={"job_id": str(jobid)})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    for job in resp.body["jobs"]:
        assert job["job_id"] == jobid
        assert job["name"] == "test job"
        assert job["partition"] == partition_name
        assert job["user_name"] == local_user_name
        assert job["job_state"] == "CANCELLED"

    resp = slurm.slurmdb_v0039_get_jobs(query_params={"users": local_user_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["jobs"]
    for job in resp.body["jobs"]:
        assert job["user"] == local_user_name

    resp = slurm.slurmdb_v0039_get_jobs(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    requery = True
    while requery:
        resp = slurm.slurmdb_v0039_get_job(path_params={"job_id": str(jobid)})
        assert resp.response.status == 200
        assert not resp.body["warnings"]
        assert not resp.body["errors"]
        assert resp.body["jobs"]
        for job in resp.body["jobs"]:
            if job["name"] == "allocation":
                # job hasn't settled at slurmdbd yet
                requery = True
            else:
                requery = False
                assert job["job_id"] == jobid
                assert job["name"] == "test job"
                assert job["partition"] == partition_name


def test_resv():
    import openapi_client
    from openapi_client.model.status import Status

    slurm = get_slurm()

    atf.run_command(
        "scontrol create reservation starttime=now duration=120 user=root flags=maint,ignore_jobs nodes=ALL ReservationName={}".format(
            resv_name
        ),
        fatal=False,
    )

    resp = slurm.slurm_v0039_get_reservation(
        path_params={"reservation_name": resv_name}, query_params={}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["reservations"]
    for resv in resp.body["reservations"]:
        assert resv["name"] == resv_name

    resp = slurm.slurm_v0039_get_reservations(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["reservations"]


def test_partitions():
    import openapi_client
    from openapi_client.model.status import Status

    slurm = get_slurm()

    resp = slurm.slurm_v0039_get_partition(
        path_params={"partition_name": partition_name}, query_params={}
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["partitions"]
    for part in resp.body["partitions"]:
        assert part["name"] == partition_name

    resp = slurm.slurm_v0039_get_partitions(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["partitions"]


def test_nodes():
    import openapi_client
    from openapi_client.model.status import Status
    from openapi_client.model.v0039_update_node_msg import V0039UpdateNodeMsg
    from openapi_client.model.v0039_csv_list import V0039CsvList

    slurm = get_slurm()

    resp = slurm.slurm_v0039_get_nodes(query_params={})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["nodes"]
    for node in resp.body["nodes"]:
        if "IDLE" in node["state"]:
            node_name = node["name"]
            comment = node["comment"]
            extra = node["extra"]
            feat = node["features"]
            actfeat = node["active_features"]
            state = node["state"]
            reason = node["reason"]
            reasonuid = node["reason_set_by_user"]
            break
    assert node_name

    node = V0039UpdateNodeMsg(
        comment="test node comment",
        extra="test node extra",
        features=V0039CsvList(
            [
                "taco1",
                "taco2",
                "taco3",
            ]
        ),
        features_act=V0039CsvList(
            [
                "taco1",
                "taco3",
            ]
        ),
        state=["DRAIN"],
        reason="testing and tacos are the reason",
        reason_uid=local_user_name,
    )

    resp = slurm.slurm_v0039_update_node(
        path_params={"node_name": node_name}, body=node
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurm_v0039_get_node(path_params={"node_name": node_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["nodes"]
    for node in resp.body["nodes"]:
        assert node["name"] == node_name
        assert node["comment"] == "test node comment"
        assert node["extra"] == "test node extra"
        assert "DRAIN" in node["state"]
        assert node["reason"] == "testing and tacos are the reason"
        assert node["reason_set_by_user"] == local_user_name

    node = V0039UpdateNodeMsg(
        comment=comment,
        extra=extra,
        features=feat,
        features_act=actfeat,
        state=["RESUME"],
        reason=reason,
        reason_uid=reasonuid,
    )

    resp = slurm.slurm_v0039_update_node(
        path_params={"node_name": node_name}, body=node
    )
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]

    resp = slurm.slurm_v0039_get_node(path_params={"node_name": node_name})
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["nodes"]
    for node in resp.body["nodes"]:
        assert node["name"] == node_name
        assert node["comment"] == comment
        assert node["extra"] == extra


def test_ping():
    import openapi_client
    from openapi_client.model.status import Status

    slurm = get_slurm()

    resp = slurm.slurm_v0039_ping()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]


def test_diag():
    import openapi_client
    from openapi_client.model.status import Status

    slurm = get_slurm()

    resp = slurm.slurm_v0039_diag()
    assert resp.response.status == 200
    assert not resp.body["warnings"]
    assert not resp.body["errors"]
    assert resp.body["statistics"]


def test_licenses():
    import openapi_client
    from openapi_client.model.status import Status

    slurm = get_slurm()

    resp = slurm.slurm_v0039_slurmctld_get_licenses()
    assert resp.response.status == 200
    assert not resp.body["errors"]
