--[[
 Test reject all jobs lua script for test7.20
--]]

function slurm_job_submit(job_desc, part_list, submit_uid)
	slurm.log_user("submit1")
	slurm.log_user("submit2")
	slurm.log_user("submit3")
	return slurm.ERROR
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
	slurm.log_user("modify1")
	slurm.log_user("modify2")
	slurm.log_user("modify3")
	return slurm.ERROR
end

slurm.log_user("initialized")
return slurm.SUCCESS
