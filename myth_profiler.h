/*
 * myth_profiler.h
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#ifndef MYTH_PROFILER_H_
#define MYTH_PROFILER_H_

// Ant: enviroment variables for profiler
#define ENV_PROFILER_OFF "PROFILER_OFF"
#define ENV_PROFILING_DEPTH_LIMIT "PROFILING_DEPTH_LIMIT"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <papi.h>
#include <limits.h>
#include "myth_misc.h"
#include "myth_profiler_output.h"

//typedef long long counter_value_t;

typedef struct counter_record {
	double time;	// Time value
	long long counter1;
	long long counter2;
} counter_record, * counter_record_t;

typedef struct time_record {
	int type; 		// briefly, 0: start, 1: stop
	int worker;		// worker thread's rank
	counter_record counters; // counter data
	struct time_record * next; // pointer to next time_record
	struct task_node * node;
} time_record, * time_record_t;

// Ant: [struct task_node] structure to save tasks' infomation
typedef struct task_node {
	char level; 	// task depth
	int index;		// in-level index
	int parent_index;
	int counter; 	// number of time records pointing to it, LSB=0 -> task's running, 1 -> task ended
	int worker;		// worker on which it's allocated
} task_node, * task_node_t;


/*typedef struct node_allocator {
	task_node_t mem;
	int n, N;
	myth_freelist_t freelist;
} node_allocator, *node_allocator_t;

typedef struct record_allocator {
	time_record_t mem;
	int n, N;
	myth_freelist_t freelist;
} record_allocator, *record_allocator_t;
*/

double 		profiler_get_curtime();
void 		profiler_init(int worker_thread_num);
void		profiler_init_worker(int rank);
void		profiler_fini_worker(int rank);
void 		profiler_fini();
task_node_t profiler_create_new_node(task_node_t parent, int worker);
void 		profiler_add_time_start(task_node_t node, int worker, int start_code);
void 		profiler_add_time_stop(task_node_t node, int worker, int stop_code);
task_node_t profiler_create_root_node();
void		profiler_mark_delete_task_node(task_node_t node);
void		profiler_write_to_file(int worker);

#endif /* MYTH_PROFILER_H_ */
