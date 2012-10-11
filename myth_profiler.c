/*
 * myth_profiler.c
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#include <sys/stat.h>
#include "myth_profiler.h"
#include "myth_misc.h"
#include "myth_worker.h"
#include "myth_desc.h"

task_node_t root_node = NULL;
task_node_t sched_nodes = NULL;
int sched_num = 0;

double base = 0;  // base time

// Test: Evaluate contex switch time, and malloc time
#define NMAX 100
double * malloctn;
double * malloctr;
int n_malloctn, n_malloctr;

double profiler_get_curtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1.0E+3 + tv.tv_usec * 1.0E-3;
}


// Test
void profiler_add_malloctn_item(double item) {
	if (n_malloctn < NMAX) {
		malloctn[n_malloctn++] = item;
	}
}

// Test
void profiler_add_malloctr_item(double item) {
	if (n_malloctr < NMAX) {
		malloctr[n_malloctr++] = item;
	}
}


void profiler_create_root_node() {
	if  (root_node == NULL) {
		// Allocate memory
		root_node = (task_node_t) myth_malloc(sizeof(task_node));
		// Set up fields
		root_node->level = 0;
		root_node->index = 0;
		root_node->mate = NULL;
		root_node->child = NULL;
		root_node->running_time = 0.0;
		root_node->time_record = NULL;
	}
}

void profiler_create_sched_nodes(int num) {
	if (sched_nodes == NULL) {
		sched_num = num;
		// Allocate memory
		sched_nodes = (task_node_t) myth_malloc(sched_num * sizeof(task_node));
		// Set up fields
		int i;
		for (i=0; i<sched_num; i++) {
			sched_nodes[i].level = 0; // unused
			sched_nodes[i].index = i;
			sched_nodes[i].mate = NULL; // unused
			sched_nodes[i].child = NULL; // unused
			sched_nodes[i].running_time = 0.0;
			sched_nodes[i].time_record = NULL;
		}
	}
}

void profiler_init(int worker_thread_num) {
	profiler_create_root_node();
	profiler_create_sched_nodes(worker_thread_num);

	// Test
	malloctn = (double *) myth_malloc(sizeof(double) * NMAX);
	malloctr = (double *) myth_malloc(sizeof(double) * NMAX);
	int i;
	for (i=0; i<NMAX; i++) {
		malloctn[i] = 0;
		malloctr[i] = 0;
	}
	n_malloctn = n_malloctr = 0;
}

task_node_t profiler_create_new_node(task_node_t parent) {
	task_node_t new_node;
	// Allocate memory
	//new_node = (task_node_t) myth_flmalloc(env->rank, sizeof(task_node));

	// Test
	double time1 = profiler_get_curtime();
	new_node = (task_node_t) myth_malloc(sizeof(task_node));
	double time2 = profiler_get_curtime();
	profiler_add_malloctn_item(time2 - time1);
	double time3 = profiler_get_curtime();
	profiler_add_malloctr_item(time3 - time2);

	// Set up fields
	new_node->level = parent->level + 1;
	new_node->index = 0;  // need edited later
	new_node->mate = NULL;
	new_node->child = NULL;
	new_node->running_time = 0.0;  // need edited later
	new_node->time_record = NULL;

	// Bind new_node to task tree
	if (parent->child == NULL) {
		parent->child = new_node;
	} else {
		task_node_t temp = parent->child;
		while (temp->mate != NULL)
			temp = temp->mate;
		temp->mate = new_node;
	}

	return new_node;
}

int profiler_indexing_tasks(task_node_t node, int index) {
	node->index = index;
	int i = index + 1;
	if (node->child != NULL)
		i = profiler_indexing_tasks(node->child, i);
	if (node->mate != NULL)
		i = profiler_indexing_tasks(node->mate, i);
	return i;
}

void profiler_output_task_tree(FILE * fp, task_node_t node) {
	if (node->child == NULL)
		return;
	// Output all level-1 children
	task_node_t child = node->child;
	if (child->mate == NULL) {
		fprintf(fp, "%d -> %d\n", node->index, child->index);
	} else {
		fprintf(fp, "%d -> {%d ", node->index, child->index);
		child = child->mate;
		while (child->mate != NULL) {
			fprintf(fp, "%d ", child->index);
			child = child->mate;
		}
		fprintf(fp, "%d}\n", child->index);
	}
	// Output all subtrees
	child = node->child;
	while (child != NULL) {
		profiler_output_task_tree(fp, child);
		child = child->mate;
	}
}

void profiler_output_running_time(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d\\n%0.3lf\"]\n", node->index, node->index, node->running_time);
	if (node->child != NULL)
		profiler_output_running_time(fp, node->child);
	if (node->mate != NULL)
		profiler_output_running_time(fp, node->mate);
}

void profiler_calculate_running_time_ex(task_node_t node) {
	time_record_t t = node->time_record;

	// Adhoc for task 0, and scheduler 0
	if (node->index == 0)
		t = t->next;

	double val = 0;
	double last = 0;
	if (t != NULL) {
		while (t != NULL && t->next != NULL) {
			last = t->val;
			t = t->next;
			val += t->val - last;
			t = t->next;
		}
	}
	node->running_time = val;
}

void profiler_calculate_running_time_1(task_node_t node) {
	profiler_calculate_running_time_ex(node);
	if (node->child != NULL)
		profiler_calculate_running_time_1(node->child);
	if (node->mate != NULL)
		profiler_calculate_running_time_1(node->mate);
}

void profiler_output_time_records_ex(FILE * fp, task_node_t node) {
	time_record_t t = node->time_record;
	while (t != NULL) {
		fprintf(fp, "%d:%d:%0.3lf", t->type, t->worker, t->val - base);
		t = t->next;
		if (t != NULL)
			fprintf(fp, ", ");
		else
			fprintf(fp, "\n");
	}
}

void profiler_output_time_records_1(FILE * fp, task_node_t node) {
	fprintf(fp, "Task %d: ", node->index);
	profiler_output_time_records_ex(fp, node);
	if (node->child != NULL)
		profiler_output_time_records_1(fp, node->child);
	if (node->mate != NULL)
		profiler_output_time_records_1(fp, node->mate);
}

void profiler_output_time_records(FILE * fp) {
	fprintf(fp, "\n\ntime records\n");
	int i;
	// Get time base
	base = sched_nodes[0].time_record->val;
	for (i=1; i<sched_num; i++) {
		double temp = sched_nodes[i].time_record->val;
		if (temp < base) base = temp;
	}
	// Schedulers' time records
	for (i=0; i<sched_num; i++) {
		fprintf(fp, "Scheduler %d: ", i);
		profiler_output_time_records_ex(fp, &sched_nodes[i]);
	}
	// Tasks' time records
	profiler_output_time_records_1(fp, root_node);
}

void profiler_output_task_tree_wtime_ex(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d | ", node->index, node->index);
	time_record_t t = node->time_record;
	int count = 0;
	if (node->index == 0) {
		fprintf(fp, "{<b%d> |<s%d>%d:%0.3lf} | ", count, count, t->worker, t->val - base);
		count++;
		t = t->next;
	}
	while (t != NULL && t->next != NULL) {
		fprintf(fp, "{<b%d>%d:%0.3lf|<s%d>%d:%0.3lf}", count, t->worker, t->val - base, count, t->next->worker, t->next->val - base);
		count++;
		t = t->next->next;
		if (t != NULL)
			fprintf(fp, " | ");
	}
	if (node->index == 0) {
		fprintf(fp, "{<b%d>%d:%0.3lf|<s%d> }", count, t->worker, t->val - base, count);
	}
	fprintf(fp, "\"]\n");
}

void profiler_output_task_tree_wtime_1(FILE * fp, task_node_t node) {
	profiler_output_task_tree_wtime_ex(fp, node);
	if (node->child != NULL)
		profiler_output_task_tree_wtime_1(fp, node->child);
	if (node->mate != NULL)
		profiler_output_task_tree_wtime_1(fp, node->mate);
}

void profiler_output_task_tree_wtime_arcs(FILE * fp, task_node_t node) {
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
		profiler_output_task_tree_wtime_arcs(fp, child);
		child = child->mate;
	}
}

void profiler_output_task_tree_wtime(FILE * fp) {
	profiler_output_task_tree_wtime_1(fp, root_node);
	profiler_output_task_tree_wtime_arcs(fp, root_node);
}

void profiler_output_data() {
	printf("Profiler's output begins...");

	// Make prof folder
	mkdir("./prof", S_IRWXU | S_IRWXG | S_IROTH);

	// Indexing tasks
	root_node->index = 0;
	profiler_indexing_tasks(root_node->child, 1);

	// Calculate running time
	int i;
	for (i=0; i<sched_num; i++)
		profiler_calculate_running_time_ex(&sched_nodes[i]);
	profiler_calculate_running_time_1(root_node);

	// Output data
	FILE *fp;

	// Task tree
	fp = fopen("./prof/task_tree.dot", "w");
	fprintf(fp, "digraph g{\n");
	profiler_output_task_tree(fp, root_node);
	profiler_output_running_time(fp, root_node);
	fprintf(fp, "\n}");
	fclose(fp);

	// Time records
	fp = fopen("./prof/time_records.txt", "w");
	profiler_output_time_records(fp);
	fclose(fp);

	// Task tree with time records
	fp = fopen("./prof/task_tree_w_time_records.dot", "w");
	fprintf(fp, "// task tree with time records\n");
	fprintf(fp, "digraph g{\nnode [shape=\"record\"]\n");
	profiler_output_task_tree_wtime(fp);
	fprintf(fp, "\n}");
	fclose(fp);

	// Test: Output
	fp = fopen("./prof/test_data.txt", "w");
	double sum = 0;
	fprintf(fp, "myth_malloc: %d items\n", n_malloctn);
	for (i=0; i<n_malloctn; i++) {
		if (i % 10 == 0 && i != 0)
			fprintf(fp, "\n");
		fprintf(fp, "%0.3lf ", malloctn[i]);
		sum += malloctn[i];
	}
	fprintf(fp, "\nAverage = %0.3lf\n\nprofiler_add_malloctn_item() + get_curtime: %d items\n", sum/n_malloctn, n_malloctr);
	sum = 0;
	for (i=0; i<n_malloctr; i++) {
		if (i % 10 == 0 && i != 0)
			fprintf(fp, "\n");
		fprintf(fp, "%0.3lf ", malloctr[i]);
		sum += malloctr[i];
	}
	fprintf(fp, "\nAverage = %0.3lf\n", sum/n_malloctr);
	fclose(fp);

	printf("\nProfiler's output ended.\n");
}

time_record_t profiler_create_time_record(char type, int worker, double val) {
	time_record_t record;
	record = (time_record_t) myth_malloc(sizeof(time_record));
	record->type = type;
	record->worker = worker;
	record->val = val;
	record->next = NULL;
	return record;
}

void profiler_add_time_record(task_node_t node, char type, int worker) {
	double time = profiler_get_curtime();
	time_record_t record = profiler_create_time_record(type, worker, time);
	if (node->time_record == NULL)
		node->time_record = record;
	else {
		time_record_t temp = node->time_record;
		while (temp->next != NULL)
			temp = temp->next;
		temp->next = record;
	}
}

void profiler_add_time_record_wthread(task_node_t node, char type, void * thread_p) {
	myth_thread_t thread = (myth_thread_t) thread_p;
	int worker = (thread)?thread->env->rank:-1;
	profiler_add_time_record(node, type, worker);
}

task_node_t profiler_get_root_node() {
	return root_node;
}

task_node_t profiler_get_sched_node(int i) {
	return &sched_nodes[i];
}
