#!/bin/env python3
import time
import os
import openapi_client
from openapi_client.rest import ApiException
from openapi_client.models.v0037_job_submission import V0037JobSubmission as jobSubmission
from openapi_client.models import V0037JobProperties as jobProperties
from openapi_client.api import SlurmApi as slurm
from pprint import pprint

s = slurm(openapi_client.ApiClient(openapi_client.Configuration()))
env = {
	"PATH": os.getenv("PATH", "/usr/local/bin:/bin:/usr/bin/:/usr/local/bin/"),
	"LD_LIBRARY_PATH": os.getenv("LD_LIBRARY_PATH", "/usr/local/lib64:/usr/local/lib/:/lib/:/lib64/:/usr/local/lib"),
	"SHELL": os.getenv("SHELL", "/bin/sh")
}
script = "#!/bin/env sh\nsrun uptime"
job = jobSubmission(script=script)
job.jobs = [
jobProperties(
	environment=env,
	current_working_directory=os.getcwd(),
	nodes=[2,3],
),
jobProperties(
	environment=env,
	current_working_directory=os.getcwd(),
	nodes=[2,4],
),
jobProperties(
	environment=env,
	current_working_directory=os.getcwd(),
	nodes=[2,5],
),
]
try:
	njob = s.slurmctld_submit_job(job)
	pprint(njob)
except ApiException as e:
	print("Exception when calling: %s\n" % e)
	os._exit(1)
