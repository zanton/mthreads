/*
 * myth_profiler_output.h
 *
 *  Created on: 2012/11/20
 *      Author: denjo
 */

#ifndef MYTH_PROFILER_OUTPUT_H_
#define MYTH_PROFILER_OUTPUT_H_

#include <stdio.h>
#include "myth_profiler.h"

void output_task_tree(FILE * fp, task_node_t node);
void output_running_time(FILE * fp, task_node_t node);
void output_total_counters(FILE * fp, task_node_t node);
void output_time_records_ex(FILE * fp, task_node_t node);
void output_time_records_1(FILE * fp, task_node_t node);
void output_time_records(FILE * fp);
void output_task_tree_wtime_ex(FILE * fp, task_node_t node);
void output_task_tree_wtime_1(FILE * fp, task_node_t node);
void output_task_tree_wtime_arcs(FILE * fp, task_node_t node);
void output_task_tree_wtime(FILE * fp);
void output_task_tree_wtcm_ex(FILE * fp, task_node_t node, int output_code);
void output_task_tree_wtcm_1(FILE * fp, task_node_t node, int output_code);
void output_task_tree_wtcm_arcs(FILE * fp, task_node_t node);
void output_task_tree_wtcm(FILE * fp, int output_code);


#endif /* MYTH_PROFILER_OUTPUT_H_ */
