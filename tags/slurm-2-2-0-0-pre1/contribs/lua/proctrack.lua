--[[

 Example lua script demonstrating the SLURM proctrack/lua interface.

 This script implements a very simple job step container using CPUSETs.

--]]

require "posix"

--########################################################################--
--
--  SLURM proctrack/lua interface:
--
--########################################################################--

function slurm_container_create (job)
    local id = cpuset_id_create (job)
    local cpu_list = cpumap:convert_ids (job.CPUs)

    log_verbose ("slurm_container_create: job=%u.%u CPUs=%s (%s) cpuset=%d",
            job.jobid, job.stepid, job.CPUs, cpu_list, id)
    if not cpuset_create (id, cpu_list) then return nil end
    return id
end

function slurm_container_add (job, id, pid)
   log_verbose ("slurm_container_add(%d, %d)\n", id, pid)
   return cpuset_add_pid (id, pid)
end

function slurm_container_signal (id, signo)
    log_verbose ("slurm_container_signal(%d, %d)\n", id, signo)
    cpuset_kill (id, signo)
    return 0
end

function slurm_container_destroy (id)
   log_verbose ("slurm_container_destroy (id=%d)\n", id)
   return (cpuset_destroy (id)) and 0 or -1
end

function slurm_container_find (pid)
   log_verbose ("slurm_container_find (pid=%d)\n", pid)
   for i, id in ipairs (posix.dir (cpuset_dir)) do
      path = string.format ("%s/%s", cpuset_dir, id)
      st = posix.stat (path)
      if st.type == "directory" and cpuset_has_pid (id) then
         return id
      end
   end
   return -1
end

function slurm_container_has_pid (id, pid)
   log_verbose ("slurm_container_has_pid (id=%d, pid=%d)\n", id, pid)
   return cpuset_has_pid (id, pid)
end

function slurm_container_wait (id)
    local s = 1

    if not cpuset_exists (id) then return 0 end

    log_verbose ("slurm_container_wait (id=%d)\n", id)
    while not cpuset_destroy (id) do
        cpuset_kill (id, 9)
        log_debug ("Waiting %ds for cpuset id=%d\n", s, id)
        posix.sleep (s)
        s = (2*s <= 30) and 2*s or 30 -- Wait a max of 30s
    end
    return 0
end

function slurm_container_get_pids (id)
    log_debug ("slurm_container_get_pids (id=%d)\n", id)
    return cpuset_pids (id)
end


--########################################################################--
--
--  Internal lua functions:
--
--########################################################################--

root_cpuset = {}

function split (line)
    local t = {}
    for word in line:gmatch ('%S+') do table.insert(t, word) end
    return t
end

function get_cpuset_dir ()
   for line in io.lines ("/proc/mounts") do
      local t = split (line)
      if t[3] == "cpuset" then return t[2] end
   end
   return nil
end

function cpuset_exists (id)
    local path = cpuset_dir .. "/" .. id
    local s = posix.stat (path)
    return (s ~= nil and s.type == "directory")
end

-- Set cpus
function cpuset_set_f (path, name, val)
    local f, msg = io.open (path .."/".. name, "w")
    if f == nil then
        log_err ("open (%s/%s): %s\n", path, name, msg)
        return nil
    end

    --
    -- Write value to cpuset if [val] was passed to this function,
    --  if not use the value from the root cpuset:
    --
    f:write (val or root_cpuset[name])
    f:close ()
end

function cpuset_create (name, cpus)
    local mask = posix.umask()
    local path = cpuset_dir .. "/" .. name

    posix.umask ("077")
    local d, s = posix.mkdir (path)
    if (d == nil and s ~= "File exists") then
        log_err ("cpuset_create: %s: %s\n", path, s or "msg")
        return false
    end
    posix.umask (mask)
    cpuset_set_f (path, "cpus", cpus)
    cpuset_set_f (path, "mems")
    return true
end

function cpuset_destroy (name)
    if (not cpuset_exists (name)) then return true end

    local path = cpuset_dir .. "/" .. name
    return (posix.rmdir (path) ~= 0)  or false;
end

function cpuset_add_pid (name, pid)
    if (not cpuset_exists (name)) then return -1 end

    local path = cpuset_dir .. "/" .. name
    local f = io.open (path.."/tasks", "w")
    f:write (pid)
    f:close()
    log_debug ("Added pid=%d to cpuset %s", pid, name)
end

function cpuset_kill (name, signo)
    if (not cpuset_exists (name)) then return end

    local path = string.format ("%s/%s/tasks", cpuset_dir, name)
    for pid in io.lines (cpuset_dir .. "/" .. name .. "/tasks") do
       log_debug ("Sending signal %d to pid %d", signo, pid)
       posix.kill (pid, signo)
    end
end

function cpuset_read (path, name)
    local f = assert (io.open (path .. "/" .. name))
    val = f:read("*all")
    f:close()
    return val
end

--
-- lua doesn't have bitwise operators, fun.
--
function truncate_to_n_bits (n, bits)
    local result = 0
    for i = 1, bits do
        local l = math.mod (n, 2)
        if (l == 1) then
            result = result + (2 ^ (i-1))
        end
        n = (n - l) / 2
    end
    return result
end

--
-- Create a unique identifier from the job step in [job]
--  to be used as the name of the resulting cpuset
--
function cpuset_id_create (job)
    local id = job.jobid

    -- Simulate a left shift by 16 (I think):
    for i = 0, 16 do
        id = id*2
    end

    -- Add the lower 16 bits of the stepid:
    id = id truncate_to_n_bits (job.stepid, 16)

    -- Must truncate result to 32bits until SLURM's job container
    --  id is no longer represented by uint32_t :
    return truncate_to_n_bits (id, 32)
end

function cpuset_has_pid (id, process_id)
    if (not cpuset_exists (id)) then return false end

    local path = string.format ("%s/%s/tasks", cpuset_dir, id)

    -- Force pid to be a number
    local pid = tonumber (process_id)

    for task in io.lines (path) do
       -- again, ensure task is represented as a lua number for comparison:
       if tonumber(task) == pid then return true end
    end
    return false
end

function cpuset_pids (id)
    local pids = {}
    if (cpuset_exists (id)) then
        local path = string.format ("%s/%s/tasks", cpuset_dir, id)
        for task in io.lines (path) do
            table.insert (pids, task)
        end
    end
    return pids
end

--
--  cpumap_create() creates an object whose sole purpose is to
--  convert a list of physical CPU ids, which are given relative
--  to physical location, back to the logical cpu d map of the
--  current host.
--
function cpumap_create ()

    function cpuset_list_create (s)
        local cpus = {}
        for c in s:gmatch ('[^,]+') do
            local s, e = c:match ('([%d]+)-?([%d]*)')
            if e == "" then e = s end
            for cpu = s, e do
                table.insert (cpus, cpu)
            end
        end
        return cpus
    end

    function read_cpu_topology_member (id, name)
        local val
        local cpudir = "/sys/devices/system/cpu"
        local path = string.format ("%s/cpu%d/topology/%s", cpudir, id, name)
        local f, err = io.open (path, "r")
        if f == nil then
            print (err)
            return f, err
        end
        val = f:read ("*all")
        f:close()
        return val
    end

    function cpu_info_create (id)
        local cpuinfo = {}
        local cpudir = "/sys/devices/system/cpu"
        cpuinfo.id = id
        cpuinfo.pkgid  = read_cpu_topology_member (id, "physical_package_id")
        cpuinfo.coreid = read_cpu_topology_member (id, "core_id")
        return cpuinfo
    end

    function list_id (self, i)
        return self.cpu_list[i+1].id
    end

    local function cmp_cpu_info (a,b)
        if a.pkgid == b.pkgid then
            return a.coreid < b.coreid
        else
            return a.pkgid < b.pkgid
        end
    end

    local function convert_cpu_ids (self, s)
        local l = {}
        for i, id in ipairs (cpuset_list_create (s)) do
            table.insert (l, list_id (self, id))
        end
        return table.concat (l, ",")
    end

    local cpu_map = {
        cpu_list =    {},
        ncpus =       0,
        get_id =      list_id,
        convert_ids = convert_cpu_ids
    }

    for i, dir in ipairs (posix.dir ("/sys/devices/system/cpu")) do
        local id = string.match (dir, 'cpu([%d]+)')
        if id then
            table.insert (cpu_map.cpu_list, cpu_info_create (id))
        end
    end

    cpu_map.ncpus = #cpu_map.cpu_list

    table.sort (cpu_map.cpu_list, cmp_cpu_info)

    return cpu_map
end

function v_log_msg (l, fmt, ...)
    slurm.log (l, string.format (fmt, ...))
end

function log_msg (fmt, ...)
    v_log_msg (0, fmt, ...)
end

function log_verbose (fmt, ...)
    v_log_msg (1, fmt, ...)
end

function log_debug (fmt, ...)
    v_log_msg (2, fmt, ...)
end

function log_err (fmt, ...)
    slurm.error (string.format (fmt, ...))
end


--########################################################################--
--
--  Initialization code:
--
--########################################################################--

cpuset_dir = get_cpuset_dir ()
if cpuset_dir == nil then
   print "cpuset must be mounted"
   return 0
end

root_cpuset.cpus = cpuset_read (cpuset_dir, "cpus")
root_cpuset.mems = cpuset_read (cpuset_dir, "mems")

cpumap = cpumap_create ()

slurm.log (string.format ("initialized: root cpuset = %s\n", cpuset_dir))

return 0

-- vi: filetype=lua ts=4 sw=4 expandtab
