--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.
 This is only an example, not meant for use in its current form.
 Leave the function names, arguments, local varialbes and setmetatable
 set up logic in each function unchanged. Change only the logic after
 the line containing "*** YOUR LOGIC GOES BELOW ***".

--]]

function _build_part_table ( part_list )
	local part_rec = {}
	local i = 1
	while part_list[i] do
		part_rec[i] = { part_rec_ptr=part_list[i] }
		setmetatable (part_rec[i], part_rec_meta)
		i = i + 1
	end
	return part_rec
end

--########################################################################--
--
--  SLURM job_submit/lua interface:
--
--########################################################################--

function slurm_job_submit ( job_desc, part_list )
	setmetatable (job_desc, job_req_meta)
	local part_rec = _build_part_table (part_list)

--      *** YOUR LOGIC GOES BELOW ***
	if job_desc.account == nil then
		local account = "***TEST_ACCOUNT***"
		log_info("slurm_job_submit: job from uid %d, setting default account value: %s",
			 job_desc.user_id, account)
		job_desc.account = account
	end
--	If no default partition, set the partition to the highest
--	priority partition this user has access to
	if job_desc.partition == nil then
		local new_partition = nil
		local top_priority  = -1
		local last_priority = -1
		local i = 1
		while part_rec[i] do
--			log_info("part name[%d]:%s", i, part_rec[i].name)
			if part_rec[i].flag_default ~= 0 then
				top_priority = -1
				break
			end
			last_priority = part_rec[i].priority
			if last_priority > top_priority then
				top_priority = last_priority
				new_partition = part_rec[i].name
			end
			i = i + 1
		end
		if top_priority >= 0 then
			log_info("slurm_job_submit: job from uid %d, setting default partition value: %s",
				 job_desc.user_id, new_partition)
			job_desc.partition = new_partition
		end
	end

	return
end

function slurm_job_modify ( job_desc, job_rec, part_list )
	setmetatable (job_desc, job_req_meta)
	setmetatable (job_rec,  job_rec_meta)
	local part_rec = _build_part_table (part_list)

--      *** YOUR LOGIC GOES BELOW ***
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
--  Define functions for logging and accessing slurmctld structures
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
part_rec_meta = {
	__index = function (table, key)
		return _get_part_rec_field(table.part_rec_ptr, key)
	end
}

log_info("initialized")

return slurm.SUCCESS
