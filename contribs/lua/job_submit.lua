--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.

--]]

require "posix"

--########################################################################--
--
--  SLURM job_submit/lua interface:
--
--########################################################################--


function slurm_job_submit ( )
	local account = "none"
	local comment = "Set by job_submit/lua"
	local job_desc = {}
	setmetatable (job_desc, job_req_meta)

	if job_desc.account == nil then
		log_info("slurm_job_submit: setting default account value to %s",
			 account)
		job_desc.account = account
	end

	if job_desc.comment == nil then
		log_info("slurm_job_submit: setting default comment value to %s",
			 comment)
		job_desc.comment = comment
	end

	return
end

function slurm_job_modify ( )
	local comment = "Set by job_submit/lua, modify"
	local job_desc = {}
	setmetatable (job_desc, job_req_meta)

	if job_desc.comment == nil then
		log_info("slurm_job_submit: setting default comment value to %s",
			 comment)
		job_desc.comment = comment
	end

	return
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

job_req_meta = {
	__index = function (table, key)
		return _get_job_req_field(key)
	end,
	__newindex = function (table, key, value)
		return _set_job_req_field(key, value)
	end
}

log_info("initialized")

return slurm.SUCCESS
