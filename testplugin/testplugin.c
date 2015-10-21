
/*
 *   To compile:
 *    gcc -shared -o renice.so renice.c
 *
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the
 * Slurm plugin loader.
 */
SPANK_PLUGIN(spanktest, 1);

static int spank_flag = 1;
/*
 * Minimum allowable value for priority. May be
 * set globally via plugin option min_prio=<prio>
 */


static int _enable_spanktest (int val,
                                const char *optarg,
                                int remote);

/*
 *  Provide a --renice=[prio] option to srun:
 */
struct spank_option spank_options[] =
{
    { "spanktest", NULL,
      "Test the new spank capabilities", 2, 0,
      (spank_opt_cb_f) _enable_spanktest
    },
    SPANK_OPTIONS_TABLE_END
};

/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    int i;

    spank_option_register(sp, spank_options);
    slurm_info("this is executed on the local node node before the job");

    return (0);
}


int slurm_spank_task_init (spank_t sp, int ac, char **av)
{

  slurm_error("this is executed on the remote node before the job");

  if (spank_flag != 0){
    slurm_error ("spanktest plugin not activated, exiting");
    return 0;
  }
    slurm_error ("spanktest plugin activated");

    slurm_error("TEST 1: new parameter supported on spank_get_item, S_CHECKPOINT_DIR");
    char *ckpt_dir;
    spank_get_item (sp, S_CHECKPOINT_DIR, &ckpt_dir);
    slurm_error("checkpoint dir is %s", ckpt_dir);

    slurm_error("TEST 2: new function, spank_set_item");
    char **argv;
    int argc;
    int cont;

    slurm_error("showing current argv.");
    spank_get_item (sp, S_JOB_ARGV, &argc,&argv);
    for (cont = 0; cont < argc; cont++)
      slurm_error(" %d: %s", cont, argv[cont]);

    slurm_error("Modifying argv to /bin/env");

    argc = 1;
    argv = malloc (sizeof(char*) * (argc + 1));
    argv[0] = malloc(sizeof(char) * 100);
    strcpy(argv[0], "/bin/env");
    argv[1] = NULL;

    if (spank_set_item(sp, S_JOB_ARGV, &argc,&argv) != ESPANK_SUCCESS)
      slurm_error("modification did not succeeded");
    argv[0] = " ";

    slurm_error("showing new argv.");
    spank_get_item (sp, S_JOB_ARGV, &argc,&argv);
    for (cont = 0; cont < argc; cont++)
      slurm_error(" %d: %s", cont, argv[cont]);

    slurm_error("end of test");


    return (0);
}


static int _enable_spanktest (int val,
                                const char *optarg,
                                int remote)
{

  slurm_error("this is process called when the user enters --spanktest");
  spank_flag = 0;

    return (0);
}
