--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.

--]]

require "posix"

--########################################################################--
--
--  SLURM job_submit/lua interface:
--
--########################################################################--


function slurm_job_submit (job_desc)
	local account = "none"
	local partition = "tbd"
	setmetatable (job_desc, job_mt)

	if job_desc.account == nil then
		log_info("slurm_job_submit: setting default account value of %s",
			 account)
		job_desc.account = account
	end

	if job_desc.partition == nil then
		log_info("slurm_job_submit: setting default partition value of %s",
			 partition)
		job_desc.partition = partition
	end

	return job_desc
end

function slurm_job_modify (job_desc)
	return job_desc
end

--########################################################################--
--
--  Initialization code:
--
--########################################################################--


log_info = slurm.log_info
log_verbose = slurm.log_verbose
log_debug = slurm.log_debug
log_err = slurm.error

job_mt = {
	__index = function (table, key)
		return _get_job_field(key)
	end,
	__newindex = function (table, key, value)
		return _set_job_field(key, value)
	end
}

log_info("initialized")

return slurm.SUCCESS
