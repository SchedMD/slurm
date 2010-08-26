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
   local min_nodes = 10
   local account = "none"

   log_info ("slurm_job_submit: changing min_nodes from %d to %d, and setting account to %s",
   	     job_desc.min_nodes, min_nodes, account)
   job_desc.min_nodes = min_nodes
   job_desc.account = account

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
