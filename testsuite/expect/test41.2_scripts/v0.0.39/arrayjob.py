#!/bin/env python3
import time
import os
import openapi_client
from openapi_client.rest import ApiException
from openapi_client.models.v0039_job_submission import V0039JobSubmission as jobSubmission
from openapi_client.models import V0039JobProperties as jobProperties
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
job.job = jobProperties(
	environment=env,
	current_working_directory=os.getcwd(),
	nodes=[2,9999],
	array="1-100%2",
)

try:
	njob = s.slurm_v0039_submit_job(job)
	pprint(njob)

	pprint(s.slurm_v0039_get_job(njob.job_id))
	pprint(s.slurm_v0039_cancel_job(njob.job_id, signal="KILL"))
	pprint(s.slurm_v0039_get_job(njob.job_id))

except ApiException as e:
	print("Exception when calling: %s\n" % e)
	os._exit(1)

os._exit(0)
