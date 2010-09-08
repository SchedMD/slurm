--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.
 This is only an example, not meant for use in its current form.

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
	local user_id = 0
	local job_desc = {}
	setmetatable (job_desc, job_req_meta)

	user_id = job_desc.user_id
	if job_desc.account == nil then
		log_info("slurm_job_submit: job from uid %d, setting default account value: %s",
			 user_id, account)
		job_desc.account = account
	end

	if job_desc.comment == nil then
		log_info("slurm_job_submit: job from uid %d, setting default comment value: %s",
			 user_id, comment)
		job_desc.comment = comment
	end

	return
end

function slurm_job_modify ( )
	local comment = "Set by job_submit/lua, modify"
	local job_id = 0
	local job_desc = {}
	local job_rec = {}
	setmetatable (job_desc, job_req_meta)
	setmetatable (job_rec,  job_rec_meta)

	job_id = job_rec.job_id
	if job_desc.comment == nil then
		log_info("slurm_job_modify: for job %u, setting default comment value: %s",
			 job_id, comment)
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

job_rec_meta = {
	__index = function (table, key)
		return _get_job_rec_field(key)
	end
}
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
