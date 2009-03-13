# Set database information
$db_name      = "slurm_acct_db";
$db_job_table = "job_table";

# These are the options you should change for your own site.
#$db_host      = "fargo";
#$db_port      = "3306";
#$db_user      = "some_user";
#$db_passwd    = "some_password";

# Database connection line for the DBI
$db_conn_line = "DBI:mysql:database=${db_name};host=${db_host}";

# "1;" Required for file inclusion
1;
