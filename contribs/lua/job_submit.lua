--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.
 This is only an example, not meant for use in its current form.
 Leave the function names, arguments, local varialbes and setmetatable
 logic in each function unchanged.

--]]

require "posix"

--########################################################################--
--
--  SLURM job_submit/lua interface:
--
--########################################################################--


function slurm_job_submit ( job_desc_addr )
	local job_desc = { job_desc_ptr=job_desc_addr }
	setmetatable (job_desc, job_req_meta)

	if job_desc.account == nil then
		local account = "***TEST_ACCOUNT***"
		log_info("slurm_job_submit: job from uid %d, setting default account value: %s",
			 job_desc.user_id, account)
		job_desc.account = account
	end

	return
end

function slurm_job_modify ( job_desc_addr, job_rec_addr )
	local job_desc = { job_desc_ptr=job_desc_addr }
	local job_rec  = { job_rec_ptr=job_rec_addr }
	setmetatable (job_desc, job_req_meta)
	setmetatable (job_rec,  job_rec_meta)

	if job_desc.comment == nil then
		local comment = "***TEST_COMMENT***"
		log_info("slurm_job_modify: for job %u, setting default comment value: %s",
			 job_rec.job_id, comment)
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
		return _get_job_rec_field(table.job_rec_ptr, key)
	end
}
job_req_meta = {
	__index = function (table, key)
		return _get_job_req_field(table.job_desc_ptr, key)
	end,
	__newindex = function (table, key, value)
		return _set_job_req_field(table.job_desc_ptr, key, value)
	end
}

log_info("initialized")

return slurm.SUCCESS
