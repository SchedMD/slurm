#define EC_PACKAGE_ENERGY	1
#define EC_DRAM_ENERGY 		2
#define EC_TOTAL_ENERGY 	3
#define EC_PACKAGE_POWER 	4
#define EC_DRAM_POWER		5
#define EC_TOTAL_POWER		6
#define EC_ENERGY_UNITS		7

#define EC_ALL_PACKAGES	        -2
#define EC_CURRENT_CPU	        -1

#define MAX_PKGS        256

#define MSR_RAPL_POWER_UNIT             0x606

/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT        0x610
#define MSR_PKG_ENERGY_STATUS           0x611
#define MSR_PKG_PERF_STATUS             0x613
#define MSR_PKG_POWER_INFO              0x614

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT            0x618
#define MSR_DRAM_ENERGY_STATUS          0x619
#define MSR_DRAM_PERF_STATUS            0x61B
#define MSR_DRAM_POWER_INFO             0x61C


extern int energy_accounting_p_updatenodeenergy( void );
extern void energy_accounting_p_getjoules_task(struct jobacctinfo *jobacct);
extern int energy_accounting_p_getjoules_scaled(uint32_t stp_smpled_time, 
						ListIterator itr);
extern int energy_accounting_p_setbasewatts( void );
extern int energy_accounting_p_readbasewatts( void );
extern uint32_t energy_accounting_p_getcurrentwatts(void );
extern uint32_t energy_accounting_p_getbasewatts( void );
extern uint32_t energy_accounting_p_getnodeenergy(uint32_t up_time);
extern int init ( void );

