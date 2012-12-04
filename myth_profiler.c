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
#include "myth_profiler_output.h"
#include "myth_init.h"

#define DIR_FOR_PROF_DATA "./tsprof"
#define FILE_FOR_TASK_TREE "./tsprof/task_tree.dot"
#define FILE_FOR_TIME_RECORDS "./tsprof/time_records.txt"
#define FILE_FOR_TASK_TREE_W_TIME_RECORDS "./tsprof/task_tree_w_time_records.dot"
#define FILE_FOR_OTHER_DATA "./tsprof/other_data.txt"
#define FILE_FOR_TASK_TREE_W_L1TCM "./tsprof/task_tree_w_l1tcm.dot"
#define FILE_FOR_TASK_TREE_W_L2TCM "./tsprof/task_tree_w_l2tcm.dot"
#define FILE_FOR_TASK_TREE_SUMMARY "./tsprof/task_tree_summary.dot"

#define GRAPH_TITLE_PRINT_PATTERN "label=\"%s\"\nlabelloc=top\nlabeljust=left\n"
#define GRAPH_TITLE_TASK_TREE "Task tree graph"
#define GRAPH_TITLE_TASK_TREE_TIME_RECORDS "Task tree graph with time records"
#define GRAPH_TITLE_TASK_TREE_L1TCM "Task tree graph with level 1 total cache misses"
#define GRAPH_TITLE_TASK_TREE_L2TCM "Task tree graph with level 2 total cache misses"

#define NUMBER_OF_PAPI_EVENTS 2

// For profiler
task_node_t root_node = NULL;
task_node_t sched_nodes = NULL;
int sched_num = 0;

// Environment variables
char task_depth_limit = CHAR_MAX;		// Profiling task depth limit, CHAR_MAX (127) means unlimited
char profiler_off = 0;				// To turn profiler off; on by default
char profiling_depth_limit = CHAR_MAX;	// Profiling depth limit, equal task_depth_limit by default

// For data output
double base = 0;  // base value for time

// For memory allocator
task_node_t node_mem;
time_record_t record_mem;
int n_nodes, n_records;
int N_nodes, N_records;
myth_internal_lock_t * node_mem_lock, * record_mem_lock;

// PAPI
int retval;
long long start_usec, end_usec;
int * EventSet;
long long ** values; // count PAPI_L1_TCM, PAPI_L2_TCM

double profiler_get_curtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1.0E+3 + tv.tv_usec * 1.0E-3;
}

void PAPI_fail(char *file, int line, char *call, int retval)
{
    fprintf(stderr, "%s\tFAILED\nLine # %d\n", file, line);
    if ( retval == PAPI_ESYS ) {
        char buf[128];
        memset( buf, '\0', sizeof(buf) );
        sprintf(buf, "System error in %s:", call);
        perror(buf);
    }
    else if ( retval > 0 ) {
        fprintf(stderr, "Error calculating: %s\n", call );
    }
    else {
    	// PAPI 4.4.0
        //char errstring[PAPI_MAX_STR_LEN];
        //PAPI_perror(retval, errstring, PAPI_MAX_STR_LEN );
        //printf("Error in %s: %s\n", call, errstring );
    	// PAPI 5.0.1
    	fprintf(stderr, "Error in %s\n", call);
    	PAPI_perror(PAPI_strerror(retval));
    }
    fprintf(stderr, "\n");
    exit(1);
}

void create_root_node() {
	if  (root_node == NULL) {
		// Allocate memory
		root_node = (task_node_t) myth_malloc(sizeof(task_node));
		// Set up fields
		root_node->level = 0;
		root_node->index = 0;
		root_node->counters.time = 0.0;
		root_node->counters.l1_tcm = 0;
		root_node->counters.l2_tcm = 0;
		root_node->time_record = NULL;
		root_node->mate = NULL;
		root_node->child = NULL;
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
			sched_nodes[i].level = 0; 		// unused
			sched_nodes[i].index = i;
			sched_nodes[i].counters.time = 0.0;
			sched_nodes[i].counters.l1_tcm = 0;
			sched_nodes[i].counters.l2_tcm = 0;
			sched_nodes[i].time_record = NULL;
			sched_nodes[i].mate = NULL;		// unused
			sched_nodes[i].child = NULL;	// unused
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
	// Get enviroment variable
	char * env_var;

	// Profiler off
	env_var = getenv(ENV_PROFILER_OFF);
	if (env_var)
		profiler_off = atoi(env_var);
	// PROFILER OFF
	if (profiler_off) return;

	// Task depth limit
	env_var = getenv(ENV_TASK_DEPTH_LIMIT);
	if (env_var) {
		task_depth_limit = atoi(env_var);
		profiling_depth_limit = task_depth_limit;
	}

	// Profiling depth limit
	env_var = getenv(ENV_PROFILING_DEPTH_LIMIT);
	if (env_var) {
		profiling_depth_limit = atoi(env_var);
		if (profiling_depth_limit < task_depth_limit)
			profiling_depth_limit = task_depth_limit;
	}

	// Profiler's init
	create_root_node();
	create_sched_nodes(worker_thread_num);
	init_memory_allocator();

	// PAPI
	// Initialize PAPI library
	retval = PAPI_library_init(PAPI_VER_CURRENT);
	if (retval != PAPI_VER_CURRENT)
		PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
	// Initialize PAPI's thread function
	retval = PAPI_thread_init( (unsigned long (*) (void)) real_pthread_self);
	if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_thread_init", retval);
	// Initialize variables related to PAPI
	EventSet = (int *) myth_malloc(worker_thread_num * sizeof(int));
	int i;
	for (i=0; i<worker_thread_num; i++)
		EventSet[i] = PAPI_NULL;
	values = (long long **) myth_malloc(worker_thread_num * sizeof(long long *));

	start_usec = PAPI_get_real_usec();
}

void profiler_init_thread(int rank) {
	// PROFILER OFF
	if (profiler_off) return;

	// Ant: [prof] register this worker thread
	if (rank != 0) {
		retval = PAPI_register_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_register_thread", retval);
	}
	// Initialize EventSet
	if ((retval = PAPI_create_eventset(EventSet + rank)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_create_eventset", retval);
	if ((retval = PAPI_add_event(EventSet[rank], PAPI_L1_TCM)) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", retval);
	if ((retval = PAPI_add_event(EventSet[rank], PAPI_L2_TCM)) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", retval);
	if ((retval = PAPI_start(EventSet[rank])) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_start", retval);
	// Initialize values variable
	values[rank] = (long long *) myth_malloc(NUMBER_OF_PAPI_EVENTS * sizeof(long long));
}

void profiler_fini_thread(int rank) {
	// PROFILER OFF
	if (profiler_off) return;

	if ((retval = PAPI_stop(EventSet[rank], values[rank])) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_stop", retval);
	if (rank != 0) {
		retval = PAPI_unregister_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_unregister_thread", retval);
	}
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
	// PROFILER OFF
	if (profiler_off) return NULL;

	// Out of profiling limit
	if (parent == NULL || parent->level >= profiling_depth_limit)
		return NULL;

	task_node_t new_node;
	// Allocate memory
	//new_node = (task_node_t) myth_flmalloc(env->rank, sizeof(task_node));
	//new_node = (task_node_t) myth_malloc(sizeof(task_node));
	new_node = profiler_malloc_task_node();

	// Check if the parent task's level is in or out of limit
	if (parent->level < task_depth_limit) {
		// Set up fields
		new_node->level = parent->level + 1;
		new_node->index = 0;  // need edited later
		new_node->counters.time = 0.0;  		// need edited later
		new_node->counters.l1_tcm = 0;  	// need edited later
		new_node->counters.l2_tcm = 0;  	// need edited later
		new_node->time_record = NULL;
		new_node->mate = NULL;
		new_node->child = NULL;

		// Bind new_node to task tree
		if (parent->child == NULL) {
			parent->child = new_node;
		} else {
			task_node_t temp = parent->child;
			while (temp->mate != NULL)
				temp = temp->mate;
			temp->mate = new_node;
		}
	} else if (parent->level == task_depth_limit) {

		// Create a child node to accumulate child results
		if (parent->child == NULL) {
			parent->child = profiler_malloc_task_node();
			parent->child->level = parent->level + 1;
			parent->child->index = 0;  // unused
			parent->child->counters.time = 0.0;		// accumulate
			parent->child->counters.l1_tcm = 0;  	// accumulate
			parent->child->counters.l2_tcm = 0;  	// accumulate
			parent->child->time_record = NULL;	// unused
			parent->child->mate = NULL;				// unused
			parent->child->child = NULL; 			// unused
		}
		// Set up fields
		new_node->level = parent->level + 1;
		new_node->index = -1;  // used to indicate time record type, 0:start, 1:stop, -1:nothing
		new_node->counters.time = 0.0;		// need edited later
		new_node->counters.l1_tcm = 0;  	// need edited later
		new_node->counters.l2_tcm = 0;  	// need edited later
		new_node->time_record = NULL;	// unused
		new_node->mate = NULL;				// unused
		new_node->child = parent; // pointer to its parent node for accumulating child results

	} else {

		// Set up fields
		new_node->level = parent->level + 1;
		new_node->index = -1;  // used to indicate time record type, 0:start, 1:stop, -1:nothing
		new_node->counters.time = 0.0;		// need edited later
		new_node->counters.l1_tcm = 0;  	// need edited later
		new_node->counters.l2_tcm = 0;  	// need edited later
		new_node->time_record = NULL;	// unused
		new_node->mate = NULL;				// unused
		new_node->child = parent->child; // pointer to its nearest in-limit parent node for accumulating child results

	}

	return new_node;
}

int indexing_tasks(task_node_t node, int index) {
	if (task_depth_limit >=0 && node->level > task_depth_limit) {
		node->index = -(index-1);
		return index;
	}
	node->index = index;
	int i = index + 1;
	if (node->child != NULL)
		i = indexing_tasks(node->child, i);
	if (node->mate != NULL)
		i = indexing_tasks(node->mate, i);
	return i;
}

void calculate_sum_counters_ex(task_node_t node) {
	time_record_t t = node->time_record;
	if (t == NULL) {
		node->counters.time = 0;
		node->counters.l1_tcm = 0;
		node->counters.l2_tcm = 0;
		return;
	}

	double val1, val2, val3, last1, last2, last3;
	val1 = val2 = val3 = 0;
	last1 = last2 = last3 = 0;
	while (t != NULL) {
		while (t != NULL && t->type%2 != 0)
			t = t->next;
		if (t != NULL) {
			last1 = t->counters.time;
			last2 = t->counters.l1_tcm;
			last3 = t->counters.l2_tcm;
		}
		while (t != NULL && t->type%2 == 0)
			t = t->next;
		while (t != NULL && t->next != NULL && t->next->type%2 == 1)
			t = t->next;
		if (t != NULL) {
			val1 += t->counters.time - last1;
			val2 += t->counters.l1_tcm - last2;
			val3 += t->counters.l2_tcm - last3;
		}
	}
	node->counters.time = val1;
	node->counters.l1_tcm = val2;
	node->counters.l2_tcm = val3;
}

void calculate_sum_counters(task_node_t node) {
	if (task_depth_limit >= 0 && node->level > task_depth_limit)
		return;

	calculate_sum_counters_ex(node);
	if (node->child != NULL)
		calculate_sum_counters(node->child);
	if (node->mate != NULL)
		calculate_sum_counters(node->mate);
}

void profiler_output_data() {
	// PROFILER OFF
	if (profiler_off) return;

	printf("Profiler's output begins...\n");

	// Make prof folder
	mkdir(DIR_FOR_PROF_DATA, S_IRWXU | S_IRWXG | S_IROTH);

	// Indexing tasks
	root_node->index = 0;
	if (root_node->child != NULL)
		indexing_tasks(root_node->child, 1);

	// Calculate running time
	calculate_sum_counters(root_node);

	// Output data
	FILE *fp;

	// Task tree
	fp = fopen(FILE_FOR_TASK_TREE, "w");
	fprintf(fp, "digraph g{\n");
	fprintf(fp, GRAPH_TITLE_PRINT_PATTERN, GRAPH_TITLE_TASK_TREE);
	output_task_tree(fp, root_node);
	output_running_time(fp, root_node);
	fprintf(fp, "\n}");
	fclose(fp);

	// Task tree
	fp = fopen(FILE_FOR_TASK_TREE_SUMMARY, "w");
	//fprintf(fp, "digraph g{\n");
	fprintf(fp, "digraph g{\nnode [shape=\"record\"]\n");
	fprintf(fp, GRAPH_TITLE_PRINT_PATTERN, GRAPH_TITLE_TASK_TREE);
	output_task_tree(fp, root_node);
	output_total_counters(fp, root_node);
	fprintf(fp, "\n}");
	fclose(fp);

	// Time records
	fp = fopen(FILE_FOR_TIME_RECORDS, "w");
	output_time_records(fp);
	fclose(fp);
	//printf("finished writing time_records.txt\n");

	// Task tree with time records
	fp = fopen(FILE_FOR_TASK_TREE_W_TIME_RECORDS, "w");
	fprintf(fp, "// task tree with time records\n");
	fprintf(fp, "digraph g{\nnode [shape=\"record\"]\n");
	fprintf(fp, GRAPH_TITLE_PRINT_PATTERN, GRAPH_TITLE_TASK_TREE_TIME_RECORDS);
	output_task_tree_wtime(fp);
	fprintf(fp, "\n}");
	fclose(fp);

	// Print temp data for testing
	fp = fopen(FILE_FOR_OTHER_DATA, "w");
	end_usec = PAPI_get_real_usec();
	fprintf(fp, "Time from profiler_init() to end of output_data():\n%lld\n", end_usec - start_usec);
	fclose(fp);

	// Task tree with PAPI_L1_TCM
	fp = fopen(FILE_FOR_TASK_TREE_W_L1TCM, "w");
	fprintf(fp, "// task tree with data from PAPI_L1_TCM\n");
	fprintf(fp, "digraph g{\nnode[shape=\"record\"]\n");
	fprintf(fp, GRAPH_TITLE_PRINT_PATTERN, GRAPH_TITLE_TASK_TREE_L1TCM);
	output_task_tree_wtcm(fp, 0);
	fprintf(fp, "\n}");
	fclose(fp);

	// Task tree with PAPI_L2_TCM
	fp = fopen(FILE_FOR_TASK_TREE_W_L2TCM, "w");
	fprintf(fp, "// task tree with data from PAPI_L2_TCM\n");
	fprintf(fp, "digraph g{\nnode[shape=\"record\"]\n");
	fprintf(fp, GRAPH_TITLE_PRINT_PATTERN, GRAPH_TITLE_TASK_TREE_L2TCM);
	output_task_tree_wtcm(fp, 1);
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
	record->counters.time = val;
	record->counters.l1_tcm = 0;
	record->counters.l2_tcm = 0;
	record->next = NULL;
	return record;
}

void profiler_add_time_start(task_node_t node, int worker, int start_code) {
	// PROFILER OFF
	if (profiler_off) return;

	// Out of profiling limit
	if (node == NULL)
		return;

	if (node->level <= task_depth_limit) {

		// Create time record
		time_record_t record = create_time_record(start_code << 1, worker, 0);
		if (node->time_record == NULL)
			node->time_record = record;
		else {
			time_record_t temp = node->time_record;
			while (temp->next != NULL)
				temp = temp->next;
			temp->next = record;
		}
		// PAPI counts
		if ((retval = PAPI_read(EventSet[worker], values[worker])) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
		record->counters.l1_tcm = values[worker][0];
		record->counters.l2_tcm = values[worker][1];
		// Must be at the end
		record->counters.time = profiler_get_curtime();

	} else if (node->index != 0) { // If current record is not a start record

		node->index = start_code << 1;
		// PAPI counts
		if ((retval = PAPI_read(EventSet[worker], values[worker])) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
		node->counters.l1_tcm = values[worker][0];
		node->counters.l2_tcm = values[worker][1];
		// Must be at the end
		node->counters.time = profiler_get_curtime();

	}
}

void profiler_add_time_stop(task_node_t node, int worker, int stop_code) {
	// PROFILER OFF
	if (profiler_off) return;

	// Out of profiling limit
	if (node == NULL)
		return;

	// Must be at the begining
	double time = profiler_get_curtime();

	if (node->level <= task_depth_limit) {

		// Create time record
		time_record_t record = create_time_record((stop_code << 1) + 1, worker, time);
		if (node->time_record == NULL)
			node->time_record = record;
		else {
			time_record_t temp = node->time_record;
			while (temp->next != NULL)
				temp = temp->next;
			temp->next = record;
		}
		// PAPI counts
		if ((retval = PAPI_read(EventSet[worker], values[worker])) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
		record->counters.l1_tcm = values[worker][0];
		record->counters.l2_tcm = values[worker][1];

	} else {

		// PAPI counts
		if ((retval = PAPI_read(EventSet[worker], values[worker])) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
		// Accumulate
		if (node->index != -1) {
			node->child->child->counters.time += time - node->counters.time;
			node->child->child->counters.l1_tcm += values[worker][0] - node->counters.l1_tcm;
			node->child->child->counters.l2_tcm += values[worker][1] - node->counters.l2_tcm;
		}
		// Insert new stop time
		node->index = 1;
		node->counters.time = time;
		node->counters.l1_tcm = values[worker][0];
		node->counters.l2_tcm = values[worker][1];

	}
}

task_node_t profiler_get_root_node() {
	return root_node;
}

task_node_t profiler_get_sched_node(int i) {
	if (sched_nodes == NULL)
		return NULL;
	else
		return &sched_nodes[i];
}

