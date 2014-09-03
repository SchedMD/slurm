--[[

 Example lua script demonstrating the SLURM job_submit/lua interface.
 This is only an example, not meant for use in its current form.

 For use, this script should be copied into a file name "job_submit.lua"
 in the same directory as the SLURM configuration file, slurm.conf.

--]]

function _limit_license_cnt(orig_string, license_name, max_count)
	local i = 0
	local j = 0
	local val = 0 

	if orig_string == nil then
		return 0
	end

	i, j, val = string.find(orig_string, license_name .. "%:(%d)")
	if val ~= nil then
		slurm.log_info("name:%s count:%s", license_name, val)
	end
	if val ~= nil and val + 0 > max_count then
		return 1
	end
	return 0
end

--########################################################################--
--
--  SLURM job_submit/lua interface:
--
--########################################################################--

function slurm_job_submit ( job_desc, part_list, submit_uid )
	local bad_license_count = 0

	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratcha", 1)
	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratchb", 1) + bad_license_count
	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratchc", 1) + bad_license_count
	if bad_license_count > 0 then
		slurm.log_info("slurm_job_submit: for user %u, invalid licenses value: %s",
				job_desc.user_id, job_desc.licenses)
		return slurm.ESLURM_INVALID_LICENSES
	end

	return 0
end

function slurm_job_modify ( job_desc, job_rec, part_list, modify_uid )
	local bad_license_count = 0

	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratcha", 1)
	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratchb", 1) + bad_license_count
	bad_license_count = _limit_license_cnt(job_desc.licenses, "lscratchc", 1) + bad_license_count
	if bad_license_count > 0 then
		slurm.log_info("slurm_job_modify: for job %u, invalid licenses value: %s",
				job_rec.job_id, job_desc.licenses)
		return slurm.ESLURM_INVALID_LICENSES
	end

	return 0
end

slurm.log_info("initialized")
return slurm.SUCCESS
