--[[
 Test lua script for test7.23
--]]

function slurm_job_submit(job_desc, part_list, submit_uid)
	if job_desc.min_mem_per_cpu == nil then
		slurm.log_user("min_mem_per_cpu is nil")
	elseif job_desc.min_mem_per_cpu == slurm.NO_VAL64 then
		slurm.log_user("min_mem_per_cpu is NO_VAL64")
	else
		slurm.log_user("min_mem_per_cpu is %u",
			       job_desc.min_mem_per_cpu)
	end

	if job_desc.min_mem_per_node == nil then
		slurm.log_user("min_mem_per_node is nil")
	else
		slurm.log_user("min_mem_per_node is %u",
			       job_desc.min_mem_per_node)
	end

	return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
	return slurm.SUCCESS
end

slurm.log_user("initialized")
return slurm.SUCCESS
