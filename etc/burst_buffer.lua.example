--[[
--Example burst_buffer.lua file for Slurm
--
--In order to use this file, it must be called "burst_buffer.lua" and must
--exist in the same directory as slurm.conf and burst_buffer.conf.
--BurstBufferType=burst_buffer/lua must also be specified in slurm.conf.
--
--This file implements each required burst buffer function, but does not
--do anything particularly useful.
--
--The required functions begin with "slurm_bb_". Other functions in this
--example file are provided merely as examples.
--
--Some functions in this file are expected to be very fast because they are
--called from slurmctld while it is has some mutexes locked. If these functions
--run for very long (even 1 second), it may severely impact slurmctld
--performance. These functions cannot be killed, so they will never time out.
--The remaining functions are called asynchronously and can potentially run for
--as long as needed without harming slurmctld performance. These functions are
--called from a separate process and will be killed if they exceed the time
--limit specified by burst_buffer.conf.
--A comment above each function will specify whether or not the function must
--return quickly.
--
--
--Function parameters:
--
--All parameters for "slurm_bb_" functions except for job_info are strings.
--job_info is a table of information about the job. The function print_job_info
--demonstrates how to read the data in this table. A complete list of fields is
--in the Slurm source code in the following location:
--
--src/plugins/burst_buffer/lua/burst_buffer_lua.c:_lua_job_info_field
--
--NOTE: job_info is read-only. It is a snapshot of job information
--just before this function was called. The actual job record can be changed
--while this script is running, making the job_info not in sync with the real
--job record.
--
--
--Return values:
--
--Each function may return 1 or 2 values. The first value must be the return
--code. The second value is optional. If given, the second return value is
--a string. It may be useful to pass a message back to the job, for example a
--reason why a particular burst buffer function failed, but is not required.
--If a "slurm_bb_" function returns an error and a string, the string may
--appear in the job's reason field.
--
--
--External "slurm" functions:
--
--You may log to the slurmctld log file with Slurm logging functions such as
--slurm.log_info(). Replace "info" with the desired debug level.
--
--A function has been provided to convert job_info to a string. It returns two
--values:
--  (1) return code: SLURM_SUCCESS on success, SLURM_ERROR on error
--  (2) string: the job_info string on success, an error message on error
--    rc, str = slurm.job_info_to_string(job_info)
--
--
--This file also provides an example of how to use a module in lua-posix.
--lua-posix provides posix bindings to lua, which can be very useful, but it is
--not required to run this file and may be removed.
--]]

lua_script_name="burst_buffer.lua"

--Print job_info to the log file
function print_job_info(job_info)
	account = job_info["account"]
	array_job_id = job_info["array_job_id"]
	array_task_id = job_info["array_task_id"]
	array_max_tasks = job_info["array_max_tasks"]
	array_task_str = job_info["array_task_str"]
	gres_detail_cnt = job_info["gres_detail_cnt"]
	if (gres_detail_cnt ~= 0) then
		--[[
		--This keys of this table are the index starting with 1 and
		--ending with gres_detail_cnt. The index is the offset of the
		--node in the job (index==1 is the first node in the job).
		--
		--The values of this table are strings representing the gres
		--currently allocated to the job on each node. The format
		--is a comma-separated list of:
		--
		--For gres with a file:
		--<gres_name>[:<gres_type>]:<count>(IDX:<gres_index>)
		--
		--For count-only gres:
		--<gres_name>[:<gres_type>](CNT:<count>)
		--
		--This field is only non-nil if the job is running and has
		--allocated gres; hence it only applies
		--to slurm_bb_pre_run since that is the only hook called with
		--a job in the running state.
		--]]
		gres_table = job_info["gres_detail_str"]
		sep = "\n\t\t"
		gres_detail_str = string.format("%s%s",
			sep, table.concat(gres_table, sep))
	else
		gres_detail_str = nil
	end
	gres_total = job_info["gres_total"]
	group_id = job_info["group_id"]
	het_job_id = job_info["het_job_id"]
	het_job_id_set = job_info["het_job_id_set"]
	het_job_offset = job_info["het_job_offset"]
	job_id = job_info["job_id"]
	job_state = job_info["job_state"]
	nodes = job_info["nodes"]
	partition = job_info["partition"]

	slurm.log_info("%s:\
JobId=%u\
	account=%s\
	array_job_id=%u\
	array_task_id=%u\
	array_max_tasks=%u\
	array_task_str=%s\
	gres_total=%s\
	group_id=%u\
	het_job_id=%u\
	het_job_offset=%u\
	job_state=%u\
	nodes=%s\
	partition=%s\
",
		lua_script_name, job_id, account, array_job_id, array_task_id,
		array_max_tasks, array_task_str, gres_total, group_id,
		het_job_id, het_job_offset, job_state, nodes, partition)

	if (gres_detail_cnt ~= 0) then
		slurm.log_info("complete gres_detail_str=\n%s",
			gres_detail_str)
		for i,v in ipairs(gres_table) do
			slurm.log_info("Node index = %u, gres_detail_str = %s",
				i, gres_table[i])
		end
	end
end


--This requires lua-posix to be installed
function posix_sleep(n)
	local Munistd = require("posix.unistd")
	local rc
	slurm.log_info("sleep for %u seconds", n)
	rc = Munistd.sleep(n)
	--rc will be 0 if successful or non-zero for amount of time left
	--to sleep
	return rc
end

--This commented out function is a wrapper for the posix "sleep"
--function in the lua-posix posix.unistd module.
function sleep_wrapper(n)
	return slurm.SUCCESS, ""
	--local rc, ret_str
	--rc = posix_sleep(n)
	--if (rc ~= 0) then
	--	ret_str = "Sleep interrupted, " .. tostring(rc) .. " seconds left"
	--	rc = slurm.ERROR
	--else
	--	ret_str = "Success"
	--	rc = slurm.SUCCESS
	--end
	--return rc, ret_str
end

--[[
--slurm_bb_job_process
--
--WARNING: This function is called synchronously from slurmctld and must
--return quickly.
--
--This function is called on job submission.
--This example reads, logs, and returns the job script.
--If this function returns an error, the job is rejected and the second return
--value (if given) is printed where salloc, sbatch, or srun was called.
--]]
function slurm_bb_job_process(job_script, uid, gid, job_info)
	local contents
	slurm.log_info("%s: slurm_bb_job_process(). job_script=%s, uid=%s, gid=%s",
		lua_script_name, job_script, uid, gid)
	io.input(job_script)
	contents = io.read("*all")

	local rc, str = slurm.job_info_to_string(job_info)
	slurm.log_info("slurm.job_info_to_string returned:\nrc=%d, str=\n%s",
		rc, str)

	return slurm.SUCCESS, contents
end

--[[
--slurm_bb_pools
--
--WARNING: This function is called from slurmctld and must return quickly.
--
--This function is called on slurmctld startup, and then periodically while
--slurmctld is running.
--
--You may specify "pools" of resources here. If you specify pools, a job may
--request a specific pool and the amount it wants from the pool. Slurm will
--subtract the job's usage from the pool at slurm_bb_data_in and Slurm will
--add the job's usage of those resources back to the pool after
--slurm_bb_teardown.
--A job may choose not to specify a pool even you pools are provided.
--If pools are not returned here, Slurm does not track burst buffer resources
--used by jobs.
--
--If pools are desired, they must be returned as the second return value
--of this function. It must be a single JSON string representing the pools.
--]]
function slurm_bb_pools()

	slurm.log_info("%s: slurm_bb_pools().", lua_script_name)

	--This commented out code specifies pools in a file:
	--local pools_file, pools
	--pools_file = "/path/to/file"

	--io.input(pools_file)
	--pools = io.read("*all")
	--slurm.log_info("Pools file:\n%s", pools)

	--This specifies pools inline:
	local pools
	pools ="\
{\
\"pools\":\
  [\
    { \"id\":\"pool1\", \"quantity\":1000, \"granularity\":1024 },\
    { \"id\":\"pool2\", \"quantity\":5, \"granularity\":2 },\
    { \"id\":\"pool3\", \"quantity\":4, \"granularity\":1 },\
    { \"id\":\"pool4\", \"quantity\":25000, \"granularity\":1 }\
  ]\
}"

	return slurm.SUCCESS, pools
end

--[[
--slurm_bb_job_teardown
--
--This function is called asynchronously and is not required to return quickly.
--This function is normally called after the job completes (or is cancelled).
--]]
function slurm_bb_job_teardown(job_id, job_script, hurry, uid, gid)
	slurm.log_info("%s: slurm_bb_job_teardown(). job id:%s, job script:%s, hurry:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, hurry, uid, gid)
	local rc, ret_str = sleep_wrapper(1)
	return rc, ret_str
end

--[[
--slurm_bb_setup
--
--This function is called asynchronously and is not required to return quickly.
--This function is called while the job is pending.
--]]
function slurm_bb_setup(job_id, uid, gid, pool, bb_size, job_script, job_info)
	slurm.log_info("%s: slurm_bb_setup(). job id:%s, uid: %s, gid:%s, pool:%s, size:%s, job script:%s",
		lua_script_name, job_id, uid, gid, pool, bb_size, job_script)

	return slurm.SUCCESS
end

--[[
--slurm_bb_data_in
--
--This function is called asynchronously and is not required to return quickly.
--This function is called immediately after slurm_bb_setup while the job is
--pending.
--]]
function slurm_bb_data_in(job_id, job_script, uid, gid, job_info)
	slurm.log_info("%s: slurm_bb_data_in(). job id:%s, job script:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, uid, gid)
	local rc, ret_str = sleep_wrapper(1)
	return rc, ret_str
end

--[[
--slurm_bb_real_size
--
--This function is called asynchronously and is not required to return quickly.
--This function is called immediately after slurm_bb_data_in while the job is
--pending.
--
--This function is only called if pools are specified and the job requested a
--pool. This function may return a number (surrounded by quotes to make it a
--string) as the second return value. If it does, the job's usage of the pool
--will be changed to this number. A commented out example is given.
--]]
function slurm_bb_real_size(job_id, uid, gid, job_info)
	slurm.log_info("%s: slurm_bb_real_size(). job id:%s, uid:%s, gid:%s",
		lua_script_name, job_id, uid, gid)
	--return slurm.SUCCESS, "10000"
	return slurm.SUCCESS
end

--[[
--slurm_bb_paths
--
--WARNING: This function is called synchronously from slurmctld and must
--return quickly.
--This function is called after the job is scheduled but before the
--job starts running when the job is in a "running + configuring" state.
--
--The file specfied by path_file is an empty file. If environment variables are
--written to path_file, these environment variables are added to the job's
--environment. A commented out example is given.
--]]
function slurm_bb_paths(job_id, job_script, path_file, uid, gid, job_info)
	slurm.log_info("%s: slurm_bb_paths(). job id:%s, job script:%s, path file:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, path_file, uid, gid)
	--io.output(path_file)
	--io.write("FOO=BAR")
	return slurm.SUCCESS
end

--[[
--slurm_bb_pre_run
--
--This function is called asynchronously and is not required to return quickly.
--This function is called after the job is scheduled but before the
--job starts running when the job is in a "running + configuring" state.
--]]
function slurm_bb_pre_run(job_id, job_script, uid, gid, job_info)
	slurm.log_info("%s: slurm_bb_pre_run(). job id:%s, job script:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, uid, gid)
	local rc, ret_str
	rc, ret_str = sleep_wrapper(1)

	print_job_info(job_info)

	-- Generate a list of nodes allocated to the job.
	-- A hostlist expression of the nodes allocated to the job is in
	-- job_info["nodes"].
	-- scontrol show hostnames expands a hostlist expression to one node
	-- per line. It does not send an RPC to slurmctld.
	--[[
	local slurm_install_path = "/opt/slurm/install"
	local scontrol = string.format("%s/bin/scontrol show hostnames %s",
		slurm_install_path, job_info["nodes"])
	slurm.log_info("Running %s", scontrol)
	local fd = io.popen(scontrol)
	local nodelist = {}

	for node in fd:lines() do
		nodelist[#nodelist + 1] = node
	end
	fd:close()

	for i,v in ipairs(nodelist) do
		slurm.log_info("slurm_bb_pre_run: node(%u)=%s", i, v)
	end
	--]]

	return rc, ret_str
end

--[[
--slurm_bb_post_run
--
--This function is called asynchronously and is not required to return quickly.
--This function is called after the job finishes. The job is in a "stage out"
--state.
--]]
function slurm_bb_post_run(job_id, job_script, uid, gid, job_info)
	slurm.log_info("%s: slurm_post_run(). job id:%s, job script:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, uid, gid)
	local rc, ret_str = sleep_wrapper(1)
	return rc, ret_str
end

--[[
--slurm_bb_data_out
--
--This function is called asynchronously and is not required to return quickly.
--This function is called after the job finishes immediately after
--slurm_bb_post_run. The job is in a "stage out" state.
--]]
function slurm_bb_data_out(job_id, job_script, uid, gid, job_info)
	slurm.log_info("%s: slurm_bb_data_out(). job id:%s, job script:%s, uid:%s, gid:%s",
		lua_script_name, job_id, job_script, uid, gid)
	local rc, ret_str = sleep_wrapper(1)
	return rc, ret_str
end

--[[
--slurm_bb_get_status
--
--This function is called asynchronously and is not required to return quickly.
--
--This function is called when "scontrol show bbstat" is run. It receives the
--authenticated user id and group id of the caller, as well as a variable
--number of arguments - whatever arguments are after "bbstat".
--For example:
--
--  scontrol show bbstat foo bar
--
--This command will pass 2 arguments after uid and gid to this function:
--  "foo" and "bar".
--
--If this function returns slurm.SUCCESS, then this function's second return
--value will be printed where the scontrol command was run. If this function
--returns slurm.ERROR, then this function's second return value is ignored and
--an error message will be printed instead.
--
--The example in this function simply prints the arguments that were given.
--]]
function slurm_bb_get_status(uid, gid, ...)

	local i, v, args
	slurm.log_info("%s: slurm_bb_get_status(), uid: %s, gid:%s",
		lua_script_name, uid, gid)

	-- Create a table from variable arg list
	args = {...}
	args.n = select("#", ...)

	for i,v in ipairs(args) do
		slurm.log_info("arg %u: \"%s\"", i, tostring(v))
	end

	return slurm.SUCCESS, "Status return message\n"
end
