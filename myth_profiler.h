/*
 * myth_profiler.h
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#ifndef MYTH_PROFILER_H_
#define MYTH_PROFILER_H_

#include <stdio.h>
#include <sys/time.h>
//#include "myth_misc.h"
//#include "myth_worker.h"

typedef struct time_record {
	int type; // 0: start, 1: stop
	double val; // time value
	int worker;
	struct time_record * next; // pointer to next node
} time_record, * time_record_t;

// Ant: [struct task_node] structure to save tasks' infomation
typedef struct task_node {
	int level;
	int index;
	double running_time;
	time_record_t time_record;
	struct task_node * mate;
	struct task_node * child;
} task_node, * task_node_t;

double 		profiler_get_curtime();
void 		profiler_init(int worker_thread_num);
task_node_t profiler_create_new_node(task_node_t parent);
void 		profiler_output_data();
//void profiler_add_time_record(task_node_t node, char type, int worker);
void 		profiler_add_time_start(task_node_t node, int worker, int start_code);
void 		profiler_add_time_stop(task_node_t node, int worker, int stop_code);
//void profiler_add_time_record_wthread(task_node_t node, char type, void * thread);
task_node_t profiler_get_root_node();
task_node_t profiler_get_sched_node(int i);

#endif /* MYTH_PROFILER_H_ */
