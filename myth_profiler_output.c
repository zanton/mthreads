/*
 * myth_profiler_output.c
 *
 *  Created on: 2012/11/20
 *      Author: denjo
 */

#include <sys/stat.h>
#include "myth_profiler_output.h"

extern task_node_t root_node;
extern task_node_t sched_nodes;
extern int sched_num;

extern char task_depth_limit;
extern double base;  // base value for time

void output_task_tree(FILE * fp, task_node_t node) {
	if (node->child == NULL)
		return;
	// Output all level-1 children
	task_node_t child = node->child;
	if (child->mate == NULL) {
		fprintf(fp, "%d -> %d ", node->index, child->index);
	} else {
		fprintf(fp, "%d -> {%d ", node->index, child->index);
		child = child->mate;
		while (child->mate != NULL) {
			fprintf(fp, "%d ", child->index);
			child = child->mate;
		}
		fprintf(fp, "%d} ", child->index);
	}
	if (task_depth_limit >= 0 && node->level == task_depth_limit)
		fprintf(fp, "[style=dotted]");
	fprintf(fp, "\n");
	// Output all subtrees
	child = node->child;
	while (child != NULL) {
		output_task_tree(fp, child);
		child = child->mate;
	}
}

void output_running_time(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d\\n%0.3lf\"] ", node->index, node->index, node->counters.time);
	if (task_depth_limit >= 0 && node->level > task_depth_limit)
		fprintf(fp, "[style=dotted]");
	fprintf(fp, "\n");
	if (node->child != NULL)
		output_running_time(fp, node->child);
	if (node->mate != NULL)
		output_running_time(fp, node->mate);
}

void output_total_counters(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d | %0.3lf | %lld | %lld\"] ", node->index, node->index, node->counters.time, node->counters.l1_tcm, node->counters.l2_tcm);
	if (task_depth_limit >= 0 && node->level > task_depth_limit)
		fprintf(fp, "[style=dotted]");
	fprintf(fp, "\n");
	if (node->child != NULL)
		output_total_counters(fp, node->child);
	if (node->mate != NULL)
		output_total_counters(fp, node->mate);
}

void output_time_records_ex(FILE * fp, task_node_t node) {
	time_record_t t = node->time_record;
	while (t != NULL) {
		fprintf(fp, "[%d:%d]:%d:%0.3lf", t->type % 2, t->type >> 1, t->worker, t->counters.time - base);
		t = t->next;
		if (t != NULL)
			fprintf(fp, ", ");
		else
			fprintf(fp, "\n");
	}
}

void output_time_records_1(FILE * fp, task_node_t node) {
	fprintf(fp, "Task %d: ", node->index);
	output_time_records_ex(fp, node);
	if (node->child != NULL)
		output_time_records_1(fp, node->child);
	if (node->mate != NULL)
		output_time_records_1(fp, node->mate);
}

void output_time_records(FILE * fp) {
	fprintf(fp, "\n\ntime records\n");
	int i;
	// Get time base
	double temp;
	base = profiler_get_curtime();
	for (i=0; i<sched_num; i++)
		if (sched_nodes[i].time_record != NULL) {
			temp = sched_nodes[i].time_record->counters.time;
			if (temp < base) base = temp;
		}
	if (root_node->time_record != NULL) {
		temp = root_node->time_record->counters.time;
		if (temp < base) base = temp;
	}
	fprintf(fp, "time base = %0.3lf\n", base);

	// Schedulers' time records
	for (i=0; i<sched_num; i++) {
		fprintf(fp, "Scheduler %d: \n", i);
		output_time_records_ex(fp, &sched_nodes[i]);
	}
	// Tasks' time records
	output_time_records_1(fp, root_node);
}

void output_task_tree_wtime_ex(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d | ", node->index, node->index);
	time_record_t t = node->time_record;

	if (t == NULL) {
		fprintf(fp, "null\"]\n");
		return;
	}
	/*
	int count = 0;
	if (node->index == 0) {
		fprintf(fp, "{<b%d> |<s%d>[%c%d][%d]:%0.3lf} | ", count, count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->counters.time - base);
		count++;
		t = t->next;
	}
	while (t != NULL && t->next != NULL) {
		fprintf(fp, "{<b%d>[%c%d][%d]:%0.3lf|<s%d>[%c%d][%d]:%0.3lf}", count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->counters.time - base,
				count, (t->next->type % 2 == 0)?'s':'o', t->next->type >> 1, t->next->worker, t->next->counters.time - base);
		count++;
		t = t->next->next;
		if (t != NULL)
			fprintf(fp, " | ");
	}
	if (t != NULL) { //node->index == 0) {
		fprintf(fp, "{<b%d>[%c%d][%d]:%0.3lf|<s%d> }", count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->counters.time - base, count);
	}*/

	int count = 0;
	while (t != NULL) {
		if (t->type%2 == 1)
			fprintf(fp, "{<b%d> |", count);
		else {
			fprintf(fp, "{<b%d>", count);
			while (t != NULL && t->type%2 == 0) {
				fprintf(fp, "(%c%d)(%d):%0.3lf", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, t->counters.time - base);
				t = t->next;
				fprintf(fp, "|");
			}
		}
		if (t == NULL)
			fprintf(fp, "<s%d> }", count);
		else {
			while (t != NULL && t->type%2 == 1) {
				if (t->next == NULL || t->next->type%2 == 0)
					fprintf(fp, "<s%d>", count);
				fprintf(fp, "(%c%d)(%d):%0.3lf", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, t->counters.time - base);
				t = t->next;
				if (t != NULL && t->type%2 == 1)
					fprintf(fp, "|");
				else
					fprintf(fp, "}");
			}
			if (t != NULL)
				fprintf(fp, " | ");
		}
		count++;
	}

	fprintf(fp, "\"]\n");
}

void output_task_tree_wtime_1(FILE * fp, task_node_t node) {
	if (task_depth_limit >=0 && node->level > task_depth_limit)
		return;
	output_task_tree_wtime_ex(fp, node);
	if (node->child != NULL)
		output_task_tree_wtime_1(fp, node->child);
	if (node->mate != NULL)
		output_task_tree_wtime_1(fp, node->mate);
}

void output_task_tree_wtime_arcs(FILE * fp, task_node_t node) {
	if (task_depth_limit >=0 && node->level >= task_depth_limit)
		return;
	if (node->child == NULL)
		return;
	// Output all level-1 children
	task_node_t child = node->child;
	int count = 0;
	if (node->index == 0)
		count = 1;
	while (child != NULL) {
		fprintf(fp, "%d:s%d -> %d:b0\n", node->index, count, child->index);
		count++;
		child = child->mate;
	}
	// Output all subtrees
	child = node->child;
	while (child != NULL) {
		output_task_tree_wtime_arcs(fp, child);
		child = child->mate;
	}
}

void output_task_tree_wtime(FILE * fp) {
	output_task_tree_wtime_arcs(fp, root_node);
	output_task_tree_wtime_1(fp, root_node);
}

void output_task_tree_wtcm_ex(FILE * fp, task_node_t node, int output_code) {
	fprintf(fp, "%d [label=\"%d | ", node->index, node->index);
	time_record_t t = node->time_record;

	if (t == NULL) {
		fprintf(fp, "null\"]\n");
		return;
	}

	int count = 0;
	while (t != NULL) {
		if (t->type%2 == 1)
			fprintf(fp, "{<b%d> |", count);
		else {
			fprintf(fp, "{<b%d>", count);
			while (t != NULL && t->type%2 == 0) {
				fprintf(fp, "(%c%d)(%d):%lld", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, (output_code == 0)?t->counters.l1_tcm:t->counters.l2_tcm);
				t = t->next;
				fprintf(fp, "|");
			}
		}
		if (t == NULL)
			fprintf(fp, "<s%d> }", count);
		else {
			while (t != NULL && t->type%2 == 1) {
				if (t->next == NULL || t->next->type%2 == 0)
					fprintf(fp, "<s%d>", count);
				fprintf(fp, "(%c%d)(%d):%lld", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, (output_code == 0)?t->counters.l1_tcm:t->counters.l2_tcm);
				t = t->next;
				if (t != NULL && t->type%2 == 1)
					fprintf(fp, "|");
				else
					fprintf(fp, "}");
			}
			if (t != NULL)
				fprintf(fp, " | ");
		}
		count++;
	}

	fprintf(fp, "\"]\n");
}

void output_task_tree_wtcm_1(FILE * fp, task_node_t node, int output_code) {
	if (task_depth_limit >= 0 && node->level > task_depth_limit)
		return;
	output_task_tree_wtcm_ex(fp, node, output_code);
	if (node->child != NULL)
		output_task_tree_wtcm_1(fp, node->child, output_code);
	if (node->mate != NULL)
		output_task_tree_wtcm_1(fp, node->mate, output_code);
}

void output_task_tree_wtcm_arcs(FILE * fp, task_node_t node) {
	if (task_depth_limit >= 0 && node->level >= task_depth_limit)
		return;
	if (node->child == NULL)
		return;
	// Output all level-1 children
	task_node_t child = node->child;
	int count = 0;
	if (node->index == 0)
		count = 1;
	while (child != NULL) {
		fprintf(fp, "%d:s%d -> %d:b0\n", node->index, count, child->index);
		count++;
		child = child->mate;
	}
	// Output all subtrees
	child = node->child;
	while (child != NULL) {
		output_task_tree_wtime_arcs(fp, child);
		child = child->mate;
	}
}

void output_task_tree_wtcm(FILE * fp, int output_code) {
	output_task_tree_wtcm_1(fp, root_node, output_code);
	//printf("finished outputing task tree wtime 1\n");
	output_task_tree_wtcm_arcs(fp, root_node);
}



