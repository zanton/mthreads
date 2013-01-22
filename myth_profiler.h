/*
 * myth_profiler.h
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#ifndef MYTH_PROFILER_H_
#define MYTH_PROFILER_H_

// Preprocessor directives
#define PROFILER_ON
#define PROFILER_WATCH_LIMIT

// Ant: enviroment variables for profiler
#define ENV_PROFILER_OFF "PROFILER_OFF"
#define ENV_PROFILER_DEPTH_LIMIT "PROFILER_DEPTH_LIMIT"
#define ENV_PROFILER_NUM_PAPI_EVENTS "PROFILER_NUM_PAPI_EVENTS"
#define ENV_PROFILER_PAPI_EVENT_NAME "PROFILER_PAPI_EVENT_"
#define ENV_PROFILER_MEM_SIZE_LIMIT "PROFILER_MEM_SIZE_LIMIT"

#define MAX_NUM_PAPI_EVENTS 4
#define MAX_PAPI_EVENT_NAME_LENGTH 22

#define DIR_FOR_PROF_DATA "./tsprof"
//#define FILE_FOR_TASK_DATA "./tsprof/task_data.txt"
#define FILE_FOR_EACH_WORKER_THREAD "./tsprof/worker_thread_"
#define FILE_FOR_GENERAL_INFO "./tsprof/overview_info.txt"

#define EACH_CORE_MEMORY_SIZE_LIMIT 100 // Megabytes

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <papi.h>
#include <limits.h>
#include "myth_misc.h"


typedef struct counter_record {
	double time;	// Time value
	long long * values; // PAPI counter values
} counter_record, * counter_record_t;

typedef struct time_record {
	int type; 		// briefly, 0: start, 1: stop
	int worker;		// worker thread's rank
	counter_record counters; // counter data
	struct time_record * next; // pointer to next time_record
	struct task_node * node; // the task that this record belongs to
} time_record, * time_record_t;

// Ant: [struct task_node] structure to save tasks' infomation
typedef struct task_node {
	char level; 	// task's depth level
	int index;		// in-level index
	int parent_index;
	int counter; 	// number of time records pointing to it, LSB=0 -> task's running, 1 -> task ended
	int worker;		// worker on which it's allocated
} task_node, * task_node_t;


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
