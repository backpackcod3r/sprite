/*
 * Test driver for the pseudo-device implementation.
 *
 * The programs output goes to standard out, while the
 * statistics taken go to error output or another file.
 */

#include <sprite.h>
#include <status.h>
#include <stdio.h>
#include <fs.h>
#include <fsCmd.h>
#include <sysStats.h>
#include <rpc.h>
#include <proc.h>
#include <vm.h>
#include <kernel/sched.h>
#include <kernel/fsStat.h>
#include <kernel/vm.h>
#include <option.h>
#include "pdevInt.h"

Boolean clean = FALSE;
Boolean histogram = FALSE;
Boolean useSelect = FALSE;
Boolean copy = FALSE;
Boolean readP = FALSE;
Boolean writeP = FALSE;
Boolean ioctlP = FALSE;
Boolean selectP = FALSE;
Boolean switchBuf = FALSE;
Boolean writeBehind = FALSE;
int size = 128;
int reps = 100;
int numClients = -1;			/* For multi-program synchronization,
					 * this is the number of slaves with
					 * which to synchronize */
int delay = 0;				/* Delay loop for server to eat CPU */
Boolean slave = FALSE;
char outFileDefault[] = "pdevtest.out";
char *outFile = outFileDefault;

Option optionArray[] = {
	{OPT_INT, "M", (Address)&numClients,
		"Master for -M (int) clients"},
	{OPT_TRUE, "S", (Address)&slave,
		"Slave bench program\nTests:"},
	{OPT_STRING, "o", (Address)&outFile,
		"Output file name"},
	{OPT_STRING, "p", (Address)&pdev,
		"Name of the master pseudo device"},
	{OPT_TRUE, "i", (Address)&ioctlP,
		"IOControl test"},
	{OPT_TRUE, "r", (Address)&readP,
		"Read test"},
	{OPT_TRUE, "w", (Address)&writeP,
		"Write test"},
	{OPT_TRUE, "W", (Address)&writeBehind,
		"Enable write behind (with -M)"},
	{OPT_TRUE, "s", (Address)&selectP,
		"Select, makes master block client"},
	{OPT_TRUE, "c", (Address)&copy,
		"Copy stream test, the slave forks"},
	{OPT_INT, "n", (Address)&reps,
		"Number of repetitions"},
	{OPT_INT, "d", (Address)&size,
		"Amount of data to transfer"},
	{OPT_INT, "P", (Address)&delay,
		"Loop for -P <int> cylces before replying"},
	{OPT_TRUE, "z", (Address)&useSelect,
		"Force Master to use select with 1 client"},
	{OPT_TRUE, "b", (Address)&switchBuf,
		"Test switching request buffers (use with -i)\nTracing:"},
	{OPT_TRUE, "x", (Address)&clean,
		"Turn off all tracing"},
	{OPT_TRUE, "h", (Address)&histogram,
		"Leave histograms on (ok with -x)"},
};
int numOptions = sizeof(optionArray) / sizeof(Option);

Fs_Stats fsStartStats, fsEndStats;
Vm_Stat	vmStartStats, vmEndStats;
Time startTime, endTime;
Sched_Instrument startSchedStats, endSchedStats;

main(argc, argv)
    int argc;
    char *argv[];
{
    register ReturnStatus status = SUCCESS;
    Proc_PID child;
    Proc_ResUsage usage;
    FILE * outStream;
    register int i;
    ClientData serverData, clientData;

    argc = Opt_Parse(argc, argv, optionArray, numOptions, OPT_ALLOW_CLUSTERING);
    if (!slave && (numClients < 0)) {
	fprintf(stderr, "Master: %s [-xfpoh] -M numSlaves\n",
			    argv[0]);
	fprintf(stderr, "Slave: %s [-xfpoh] -S\n", argv[0]);
	Opt_PrintUsage(argv[0], optionArray, numOptions);
	exit(1);
    }
    if (size > MAX_SIZE) {
	size = MAX_SIZE;
    }
    if (clean) {
	int newValue;
	newValue = 0;
	Fs_Command(FS_SET_CACHE_DEBUG, sizeof(int), &newValue);
	newValue = 0;
	Fs_Command(FS_SET_TRACING, sizeof(int), &newValue);
	newValue = 0;
	Fs_Command(FS_SET_RPC_DEBUG, sizeof(int), &newValue);
	newValue = 0;
	Fs_Command(FS_SET_RPC_TRACING, sizeof(int), &newValue);
	if (!histogram) {
	    newValue = 0;
	    (void) Fs_Command(FS_SET_RPC_SERVER_HIST, sizeof(int), &newValue);
	    newValue = 0;
	    (void) Fs_Command(FS_SET_RPC_CLIENT_HIST, sizeof(int), &newValue);
	}
    }
    outStream = fopen(outFile, "w+");
    if (outStream == (FILE *)NULL) {
	perror(outFile);
	exit(status);
    }
    /*
     * Copy command line to output file.
     */
    fprintf(outStream, "%s ", argv[0]);
    if (clean) {
	fprintf(outStream, "-x ");
    }
    if (histogram) {
	fprintf(outStream, "-h ");
    }
    if (clean) {
	fprintf(outStream, "-x ");
    }
    for (i=1 ; i<argc ; i++) {
	fprintf(outStream, "%s ", argv[i]);
    }
    fprintf(outStream, "\n");
    /*
     * Check for multi-program master/slave setup.
     */
    if (numClients > 0) {
	ServerSetup(numClients, &serverData);
	slave = FALSE;
    }
    if (slave) {
	ClientSetup(&clientData);
    }
    /*
     * Get first sample of filesystem stats, vm stats, time, and idle ticks.
     */
    status = Fs_Command(FS_RETURN_STATS, sizeof(Fs_Stats), &fsStartStats);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Error getting FS stats");
	exit(status);
    }
    status = Vm_Cmd(VM_GET_STATS, &vmStartStats);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Error getting VM stats");
	exit(status);
    }
    status = Sys_Stats(SYS_SCHED_STATS, 0, &startSchedStats);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Error in Sys_Stats");
	exit(status);
    }

    status = Sys_GetTimeOfDay(&startTime, NULL, NULL);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Error in Sys_GetTimeOfDay");
	exit(status);
    }
    if (slave) {
	if (copy) {
	    child = fork();
	}
	if (readP) {
	    ClientRead(clientData, size, reps);
	}
	if (writeP) {
	    ClientWrite(clientData, size, reps);
	}
	if (ioctlP) {
	    ClientIOControl(clientData, size, reps);
	}
	if (copy) {
	    if (child != 0) {
		(void)wait(0);
	    } else {
		exit(0);
	    }
	}
	/*
	 * Take ending statistics and print user, system, and elapsed times.
	 */
	Sys_GetTimeOfDay(&endTime, NULL, NULL);
	Sys_Stats(SYS_SCHED_STATS, 0, &endSchedStats);
	Time_Subtract(endTime, startTime, &endTime);
	printf("Slave: ");
	PrintTimes(stdout, &usage, &endTime);

	Sys_Shutdown(0);	/* sync cache */
	ClientDone(clientData);

	Fs_Command(FS_RETURN_STATS, sizeof(Fs_Stats), &fsEndStats);
	Vm_Cmd(VM_GET_STATS, &vmEndStats);
	/*
	 * Print FS statistics.
	 */
	fprintf(outStream, "C: ");
	PrintIdleTime(outStream, &startSchedStats, &endSchedStats, &endTime);
	PrintFs_Stats(outStream, &fsStartStats, &fsEndStats);
	/*
	 * Print VM statistics.
	 */
	PrintVmStats(outStream, &vmStartStats, &vmEndStats);
    } else {
	/*
	 * Drop into the top level service loop.  ServeOne is a faster
	 * version that doesn't use select because there is only
	 * one client.
	 */
	if (numClients > 1 || useSelect) {
	    Serve(serverData);
	} else {
	    ServeOne(serverData);
	}
	/*
	 * Take ending statistics and print user, system, and elapsed times.
	 */
	Sys_GetTimeOfDay(&endTime, NULL, NULL);
	Sys_Stats(SYS_SCHED_STATS, 0, &endSchedStats);
	Time_Subtract(endTime, startTime, &endTime);
	printf("Master: ");
	PrintTimes(stdout, &usage, &endTime);

	Sys_Shutdown(0);	/* sync cache */
	Fs_Command(FS_RETURN_STATS, sizeof(Fs_Stats), &fsEndStats);
	Vm_Cmd(VM_GET_STATS, &vmEndStats);
	/*
	 * Print FS statistics.
	 */
	fprintf(outStream, "S: ");
	PrintIdleTime(outStream, &startSchedStats, &endSchedStats, &endTime);
	PrintFs_Stats(outStream, &fsStartStats, &fsEndStats);
	/*
	 * Print VM statitistics.
	 */
	PrintVmStats(outStream, &vmStartStats, &vmEndStats);

	if (delay > 0) {
	    int cnt;
	    Sys_GetTimeOfDay(&startTime, NULL, NULL);
	    for (cnt=0 ; cnt<50 ; cnt++) {
		for (i=delay<<1 ; i>0 ; i--) ;
	    }
	    Sys_GetTimeOfDay(&endTime, NULL, NULL);
	    Time_Subtract(endTime, startTime, &endTime);
	    Time_Divide(endTime, 50, &endTime);
	    printf("%d.%06d seconds service delay\n",
		endTime.seconds, endTime.microseconds);
	}
    }
    exit(status);
}
