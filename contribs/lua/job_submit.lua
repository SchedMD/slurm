--[[

 Example lua script demonstrating the Slurm job_submit/lua interface.
 This is only an example, not meant for use in its current form.

 For use, this script should be copied into a file name "job_submit.lua"
 in the same directory as the Slurm configuration file, slurm.conf.

--]]

function slurm_job_submit(job_desc, part_list, submit_uid)
	if job_desc.account == nil then
		local account = "***TEST_ACCOUNT***"
		slurm.log_info("slurm_job_submit: job from uid %u, setting default account value: %s",
				submit_uid, account)
		job_desc.account = account
	end
--	If no default partition, set the partition to the highest
--	priority partition this user has access to
	if job_desc.partition == nil then
		local new_partition = nil
		local top_priority  = -1
		local last_priority = -1
		local inx = 0
		for name, part in pairs(part_list) do
			slurm.log_info("part name[%d]:%s", inx, part.name)
			inx = inx + 1
			if part.flag_default ~= 0 then
				top_priority = -1
				break
			end
			last_priority = part.priority
			if last_priority > top_priority then
				top_priority = last_priority
				new_partition = part.name
			end
		end
		if top_priority >= 0 then
			slurm.log_info("slurm_job_submit: job from uid %u, setting default partition value: %s",
					job_desc.user_id, new_partition)
			job_desc.partition = new_partition
		end
	end

	return slurm.SUCCESS
end

function slurm_job_modify(job_desc, job_rec, part_list, modify_uid)
	if job_desc.comment == nil then
		local comment = "***TEST_COMMENT***"
		slurm.log_info("slurm_job_modify: for job %u from uid %u, setting default comment value: %s",
				job_rec.job_id, modify_uid, comment)
		job_desc.comment = comment
	end

	return slurm.SUCCESS
end

slurm.log_info("initialized")
return slurm.SUCCESS
