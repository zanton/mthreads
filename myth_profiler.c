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
#include "myth_internal_lock.h"

task_node_t root_node = NULL;
task_node_t sched_nodes = NULL;
int sched_num = 0;

double base = 0;  // base value for time

// For memory allocator
task_node_t node_mem;
time_record_t record_mem;
int n_nodes, n_records;
int N_nodes, N_records;
myth_internal_lock_t * node_mem_lock, * record_mem_lock;

// Temp
double tempdata[2];
int n_tempdata;

double profiler_get_curtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1.0E+3 + tv.tv_usec * 1.0E-3;
}

void create_root_node() {
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

void create_sched_nodes(int num) {
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

void init_memory_allocator() {
	// Task node memory
	N_nodes = 1;
	node_mem = (task_node_t) myth_malloc(N_nodes * sizeof(task_node));
	myth_assert(new_node != NULL);
	n_nodes = N_nodes;
	node_mem_lock = (myth_internal_lock_t *) myth_malloc(sizeof(myth_internal_lock_t));
	myth_internal_lock_init(node_mem_lock);
	// Time record memory
	N_records = 1;
	record_mem = (time_record_t) myth_malloc(N_records * sizeof(time_record));
	myth_assert(new_node != NULL);
	n_records = N_records;
	record_mem_lock = (myth_internal_lock_t *) myth_malloc(sizeof(myth_internal_lock_t));
	myth_internal_lock_init(record_mem_lock);
}

void profiler_init(int worker_thread_num) {
	create_root_node();
	create_sched_nodes(worker_thread_num);
	init_memory_allocator();

	// Temp
	n_tempdata = 0;
}

void profiler_fini() {
	myth_internal_lock_destroy(node_mem_lock);
	myth_internal_lock_destroy(record_mem_lock);
}
task_node_t profiler_malloc_task_node() {
	myth_internal_lock_lock(node_mem_lock);
	if (n_nodes == 0) {
		N_nodes *= 2;
		node_mem = (task_node_t) myth_malloc(N_nodes * sizeof(task_node));
		myth_assert(new_node != NULL);
		n_nodes = N_nodes;
	}
	task_node_t ret = node_mem;
	node_mem++;
	n_nodes--;
	myth_internal_lock_unlock(node_mem_lock);
	return ret;
}

time_record_t profiler_malloc_time_record() {
	myth_internal_lock_lock(record_mem_lock);
	if (n_records == 0) {
		N_records *= 2;
		record_mem = (time_record_t) myth_malloc(N_records * sizeof(time_record));
		myth_assert(new_node != NULL);
		n_records = N_records;
	}
	time_record_t ret = record_mem;
	record_mem++;
	n_records--;
	myth_internal_lock_unlock(record_mem_lock);
	return ret;
}

task_node_t profiler_create_new_node(task_node_t parent) {
	//if (parent->level == 1)
			//tempdata[n_tempdata++] = profiler_get_curtime();

	task_node_t new_node;
	// Allocate memory
	//new_node = (task_node_t) myth_flmalloc(env->rank, sizeof(task_node));
	//new_node = (task_node_t) myth_malloc(sizeof(task_node));
	new_node = profiler_malloc_task_node();
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

int indexing_tasks(task_node_t node, int index) {
	node->index = index;
	int i = index + 1;
	if (node->child != NULL)
		i = indexing_tasks(node->child, i);
	if (node->mate != NULL)
		i = indexing_tasks(node->mate, i);
	return i;
}

void output_task_tree(FILE * fp, task_node_t node) {
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
		output_task_tree(fp, child);
		child = child->mate;
	}
}

void output_running_time(FILE * fp, task_node_t node) {
	fprintf(fp, "%d [label=\"%d\\n%0.3lf\"]\n", node->index, node->index, node->running_time);
	if (node->child != NULL)
		output_running_time(fp, node->child);
	if (node->mate != NULL)
		output_running_time(fp, node->mate);
}

void calculate_running_time_ex(task_node_t node) {
	time_record_t t = node->time_record;
	if (t == NULL) {
		node->running_time = 0;
		return;
	}

	double val = 0;
	double last = 0;
	while (t != NULL) {
		while (t != NULL && t->type%2 != 0)
			t = t->next;
		if (t != NULL)
			last = t->val;
		while (t != NULL && t->type%2 == 0)
			t = t->next;
		while (t != NULL && t->next != NULL && t->next->type%2 == 1)
					t = t->next;
		if (t != NULL)
			val += t->val - last;
	}
	node->running_time = val;
}

void calculate_running_time_1(task_node_t node) {
	calculate_running_time_ex(node);
	if (node->child != NULL)
		calculate_running_time_1(node->child);
	if (node->mate != NULL)
		calculate_running_time_1(node->mate);
}

void output_time_records_ex(FILE * fp, task_node_t node) {
	time_record_t t = node->time_record;
	while (t != NULL) {
		fprintf(fp, "[%d:%d]:%d:%0.3lf", t->type % 2, t->type >> 1, t->worker, t->val - base);
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
			temp = sched_nodes[i].time_record->val;
			if (temp < base) base = temp;
		}
	if (root_node->time_record != NULL) {
		temp = root_node->time_record->val;
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
		fprintf(fp, "{<b%d> |<s%d>[%c%d][%d]:%0.3lf} | ", count, count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->val - base);
		count++;
		t = t->next;
	}
	while (t != NULL && t->next != NULL) {
		fprintf(fp, "{<b%d>[%c%d][%d]:%0.3lf|<s%d>[%c%d][%d]:%0.3lf}", count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->val - base,
				count, (t->next->type % 2 == 0)?'s':'o', t->next->type >> 1, t->next->worker, t->next->val - base);
		count++;
		t = t->next->next;
		if (t != NULL)
			fprintf(fp, " | ");
	}
	if (t != NULL) { //node->index == 0) {
		fprintf(fp, "{<b%d>[%c%d][%d]:%0.3lf|<s%d> }", count, (t->type % 2 == 0)?'s':'o', t->type >> 1, t->worker, t->val - base, count);
	}*/

	int count = 0;
	while (t != NULL) {
		if (t->type%2 == 1)
			fprintf(fp, "{<b%d> |", count);
		else {
			fprintf(fp, "{<b%d>", count);
			while (t != NULL && t->type%2 == 0) {
				fprintf(fp, "(%c%d)(%d):%0.3lf", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, t->val - base);
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
				fprintf(fp, "(%c%d)(%d):%0.3lf", (t->type%2 == 0)?'s':'o', t->type >> 1, t->worker, t->val - base);
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
	output_task_tree_wtime_ex(fp, node);
	if (node->child != NULL)
		output_task_tree_wtime_1(fp, node->child);
	if (node->mate != NULL)
		output_task_tree_wtime_1(fp, node->mate);
}

void output_task_tree_wtime_arcs(FILE * fp, task_node_t node) {
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
	output_task_tree_wtime_1(fp, root_node);
	//printf("finished outputing task tree wtime 1\n");
	output_task_tree_wtime_arcs(fp, root_node);
}

void profiler_output_data() {
	printf("Profiler's output begins...\n");

	// Make prof folder
	mkdir("./prof", S_IRWXU | S_IRWXG | S_IROTH);

	// Indexing tasks
	root_node->index = 0;
	indexing_tasks(root_node->child, 1);
	//printf("finished indexing tasks\n");

	// Calculate running time
	int i;
	for (i=0; i<sched_num; i++)
		calculate_running_time_ex(&sched_nodes[i]);
	//printf("finished calculating scheds' running times\n");
	calculate_running_time_1(root_node);
	//printf("finished calculating running times\n");

	// Output data
	FILE *fp;

	// Task tree
	fp = fopen("./prof/task_tree.dot", "w");
	fprintf(fp, "digraph g{\n");
	output_task_tree(fp, root_node);
	output_running_time(fp, root_node);
	fprintf(fp, "\n}");
	fclose(fp);
	//printf("finished writing task_tree.dot\n");

	// Time records
	fp = fopen("./prof/time_records.txt", "w");
	output_time_records(fp);
	fclose(fp);
	//printf("finished writing time_records.txt\n");

	// Task tree with time records
	fp = fopen("./prof/task_tree_w_time_records.dot", "w");
	fprintf(fp, "// task tree with time records\n");
	fprintf(fp, "digraph g{\nnode [shape=\"record\"]\n");
	output_task_tree_wtime(fp);

	// Print temp data
	for (i=0; i<n_tempdata; i++)
		fprintf(fp, "%0.3lf\n", tempdata[i] - base);

	fprintf(fp, "\n}");
	fclose(fp);
	//printf("finished writing task_tree_w_time_records.dot\n");
	printf("Profiler's output ended.\n");
	profiler_fini();
}

time_record_t create_time_record(int type, int worker, double val) {
	time_record_t record;
	//record = (time_record_t) myth_malloc(sizeof(time_record));
	record = profiler_malloc_time_record();
	record->type = type;
	record->worker = worker;
	record->val = val;
	record->next = NULL;
	return record;
}

void profiler_add_time_start(task_node_t node, int worker, int start_code) {
	time_record_t record = create_time_record(start_code << 1, worker, 0);
	if (node->time_record == NULL)
		node->time_record = record;
	else {
		time_record_t temp = node->time_record;
		while (temp->next != NULL)
			temp = temp->next;
		temp->next = record;
	}
	record->val = profiler_get_curtime();
}

void profiler_add_time_stop(task_node_t node, int worker, int stop_code) {
	double time = profiler_get_curtime();
	time_record_t record = create_time_record((stop_code << 1) + 1, worker, time);
	if (node->time_record == NULL)
		node->time_record = record;
	else {
		time_record_t temp = node->time_record;
		while (temp->next != NULL)
			temp = temp->next;
		temp->next = record;
	}
}

/*
void profiler_add_time_record(task_node_t node, char type, int worker) {
	double time = profiler_get_curtime();
	time_record_t record = create_time_record(type, worker, time);
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
*/

task_node_t profiler_get_root_node() {
	return root_node;
}

task_node_t profiler_get_sched_node(int i) {
	return &sched_nodes[i];
}

