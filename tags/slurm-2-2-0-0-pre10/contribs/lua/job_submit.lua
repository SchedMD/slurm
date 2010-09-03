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
   local rc = slurm.SUCCESS
   local account = "none"
   local partition = "tbd"

   if job_desc.account == nil then
      log_info("slurm_job_submit: account is not set, setting default value of %s",
               account)
      job_desc.account = account
   end

   if job_desc.partition == nil then
      log_info("slurm_job_submit: partition is not set, setting default value of %s",
               partition)
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

log_info("initialized")

return slurm.SUCCESS
