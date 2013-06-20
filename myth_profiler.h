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
#define PROFILER_LIB_INSTRUMENT_ON
#define PROFILER_APP_INSTRUMENT_ON

// Ant: enviroment variables for profiler
#define ENV_PROFILER_OFF "PROFILER_OFF"
#define ENV_PROFILER_DEPTH_LIMIT "PROFILER_DEPTH_LIMIT"
#define ENV_PROFILER_NUM_PAPI_EVENTS "PROFILER_NUM_PAPI_EVENTS"
#define ENV_PROFILER_PAPI_EVENT_NAME "PROFILER_PAPI_EVENT_"
#define ENV_PROFILER_MEM_SIZE_LIMIT "PROFILER_MEM_SIZE_LIMIT"
#define ENV_PROFILER_WATCH_FROM "PROFILER_WATCH_FROM"
#define ENV_PROFILER_WATCH_MODE "PROFILER_WATCH_MODE"
#define ENV_PROFILER_WATCH_TO "PROFILER_WATCH_TO"
#define ENV_PROFILER_TRACE_NAME "PROFILER_TRACE_NAME"
#define ENV_PROFILER_LIB_INSTRUMENT_OFF "PROFILER_LIB_INS_OFF"
#define ENV_PROFILER_APP_INSTRUMENT_OFF "PROFILER_APP_INS_OFF"


#define MAX_NUM_PAPI_EVENTS 4
#define MAX_PAPI_EVENT_NAME_LENGTH 22

#define DIR_FOR_PROF_DATA "./tsprof"
#define FILE_FOR_GENERAL_INFO "./tsprof/overview_info.txt"

#define EACH_CORE_MEMORY_SIZE_LIMIT 300 // Megabytes

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <papi.h>
#include <limits.h>
#include "myth_misc.h"

#define profiler_time_t unsigned long long //uint64_t

typedef struct profiler_task_node {
	unsigned int 	index; 				// Hash code
	char * 			tree_path; 			// Generation tree path
	int 			level; 				// Task's depth
	char * 			function_name; 		// Function's name
	int 			head_scl;			// head source code location
	int				tail_scl; 			// tail source code location
	int 			num_child_tasks;	// number of born child tasks
	struct profiler_task_node * next;	// pointer to next task_node
	//int				worker;				// worker where its memory is allocated
} profiler_task_node, *profiler_task_node_t;

typedef struct profiler_time_record {
	int 				type; 							// start or stop, what kind of start or stop
	unsigned int 		task_index; 					// task's identifier
	profiler_time_t		time; 							// at what time it happened
	int 				scl; 							// source code location
	long long 	 		values[MAX_NUM_PAPI_EVENTS];	// hardware counter values
	struct profiler_time_record * next;	// pointer to next time_record
	//int				worker;				// worker where its memory is allocated
} profiler_time_record, *profiler_time_record_t;

typedef struct profiler_function_record {
	int 				type;
	char * 				file;
	int 				line;
	int 				level;
	char * 				tree_path;
	profiler_time_t		time;
	long long			values[MAX_NUM_PAPI_EVENTS];
	struct profiler_function_record * next;
} profiler_function_record, *profiler_function_record_t;

void 					profiler_init(int worker_thread_num);
void					profiler_init_worker(int rank);
void					profiler_fini_worker(int rank);
void 					profiler_fini();
profiler_task_node_t 	profiler_create_root_node();
profiler_task_node_t 	profiler_create_new_node(profiler_task_node_t parent, int worker, int level);
void 					profiler_add_time_start(void * thread, int worker, int start_code);
void 					profiler_add_time_stop(void * thread, int worker, int stop_code);

#endif /* MYTH_PROFILER_H_ */
