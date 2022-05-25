#!/bin/env python3
import time
import os
import openapi_client
from openapi_client.rest import ApiException
from openapi_client.api import SlurmApi as slurm
from pprint import pprint

s = slurm(openapi_client.ApiClient(openapi_client.Configuration()))
try:
	pprint(s.slurmdb_v0039_diag())
except ApiException as e:
	print("Exception when calling: %s\n" % e)
	os._exit(1)
