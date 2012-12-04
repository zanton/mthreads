/*
 * myth_profiler.h
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#ifndef MYTH_PROFILER_H_
#define MYTH_PROFILER_H_

// Ant: enviroment variables for profiler
#define ENV_TASK_DEPTH_LIMIT "TASK_DEPTH_LIMIT"
#define ENV_PROFILER_OFF "PROFILER_OFF"
#define ENV_PROFILING_DEPTH_LIMIT "PROFILING_DEPTH_LIMIT"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <papi.h>
#include <limits.h>


typedef struct counter_record {
	double time; 				// Time value
	long long l1_tcm; 	// PAPI_L1_TCM
	long long l2_tcm; 	// PAPI_L2_TCM
} counter_record, * counter_record_t;

typedef struct time_record {
	int type; 		// briefly, 0: start, 1: stop
	int worker;		// worker thread's rank
	counter_record counters; // counter data
	struct time_record * next; // pointer to next time_record
} time_record, * time_record_t;

// Ant: [struct task_node] structure to save tasks' infomation
typedef struct task_node {
	char level; 	// task depth, -1 means out of profiling limit, need particular manipulation
	int index;
	counter_record counters; // sum of it at each execution
	time_record_t time_record; // linked list of time_record
	struct task_node * mate;
	struct task_node * child;
} task_node, * task_node_t;

double 		profiler_get_curtime();
void 		profiler_init(int worker_thread_num);
void		profiler_init_thread(int rank);
void		profiler_fini_thread(int rank);
task_node_t profiler_create_new_node(task_node_t parent);
void 		profiler_output_data();
//void profiler_add_time_record(task_node_t node, char type, int worker);
void 		profiler_add_time_start(task_node_t node, int worker, int start_code);
void 		profiler_add_time_stop(task_node_t node, int worker, int stop_code);
//void profiler_add_time_record_wthread(task_node_t node, char type, void * thread);
task_node_t profiler_get_root_node();
task_node_t profiler_get_sched_node(int i);


#endif /* MYTH_PROFILER_H_ */
