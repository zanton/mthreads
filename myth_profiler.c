/*
 * myth_profiler.c
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#include <sys/stat.h>
#include "myth_profiler.h"
#include "myth_worker.h"
#include "myth_desc.h"
#include "myth_internal_lock.h"
#include "myth_init.h"
#include "myth_worker_func.h"

#define NUMBER_OF_PAPI_EVENTS 2
#define MEMORY_SIZE_LIMIT 524288000


// For profiler
int num_workers = 0;	// number of workers
task_node_t root_node = NULL;	//TODO: linked list of all task nodes?

// Environment variables
//char task_depth_limit = CHAR_MAX;		// Profiling task depth limit,CHAR_MAX (127) means unlimited
char profiler_off = 0;				// To turn profiler off; on by default
char profiling_depth_limit = CHAR_MAX;	// Profiling depth limit, CHAR_MAX (127) means unlimited

// For memory allocators
/*task_node_t * node_mem;
int *n_nodes, *N_nodes;
time_record_t * record_mem;
int *n_records, *N_records;

// For memory freelists
myth_freelist_t * freelist_node;
myth_freelist_t * freelist_record;*/

// PAPI
int retval;

// In-level indexer
int indexer[CHAR_MAX];	// level-based
myth_internal_lock_t * indexer_lock[CHAR_MAX]; 	// level-based

// Profiler memory observation
int total_mem_size = 0;

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

void init_memory_allocator() {
	int i;
	myth_running_env_t env;
	for (i=0; i<num_workers; i++){
		env = &g_envs[i];

		// Task node memory TODO: observe total mem size?
		env->node_mem.N = 1;
		env->node_mem.mem = (task_node_t) myth_malloc(sizeof(task_node));
		env->node_mem.n = 1;
		myth_freelist_init(env->node_mem.freelist);

		// Time record memory TODO: observe total mem size?
		env->record_mem.N = 1;
		env->record_mem.mem = (time_record_t) myth_malloc(sizeof(time_record));
		env->record_mem.n = 1;
		myth_freelist_init(env->record_mem.freelist);

	}
}

task_node_t profiler_malloc_task_node(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	//myth_running_env_t env = myth_get_current_env();
	node_allocator * alloc = &g_envs[worker].node_mem;

	task_node_t ret;
	/*myth_freelist_pop(alloc->freelist, ret);
	if (ret)
		return ret;*/

	if (alloc->n == 0) {
		alloc->N *= 2;
		alloc->mem = (task_node_t) myth_malloc(alloc->N * sizeof(task_node));
		myth_assert(alloc->mem != NULL);
		alloc->n = alloc->N;
	}
	ret = alloc->mem;
	alloc->mem++;
	alloc->n--;
	//printf("task myth_malloc: %p\n", ret);
	return ret;
}

time_record_t profiler_malloc_time_record(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	//myth_running_env_t env = myth_get_current_env();
	record_allocator * alloc = &g_envs[worker].record_mem;

	time_record_t ret;
	/*myth_freelist_pop(alloc->freelist, ret);
	if (ret) {
		printf("pop from freelist: %p\n", &ret);
		return ret;
	}*/

	if (alloc->n == 0) {
		alloc->N *= 2;
		alloc->mem = (time_record_t) myth_malloc(alloc->N * sizeof(time_record));
		myth_assert(alloc->mem != NULL);
		alloc->n = alloc->N;
	}
	ret = alloc->mem;
	alloc->mem++;
	alloc->n--;
	//printf("myth_malloc: %p\n", ret);
	return ret;
}

void create_root_node() {
	if  (root_node == NULL) {
		// Allocate memory
		root_node = profiler_malloc_task_node(0);
		// Set up fields
		root_node->level = 0;
		root_node->index = 0;
		root_node->parent_index = -1;
		root_node->time_record = NULL;
	}
}

void profiler_init(int worker_thread_num) {
	// Get enviroment variable
	char * env_var;

	// PROFILER_OFF environment variable
	env_var = getenv(ENV_PROFILER_OFF);
	if (env_var)
		profiler_off = atoi(env_var);
	if (profiler_off) return;

	// PROFILING_DEPTH_LIMIT environment variable
	env_var = getenv(ENV_PROFILING_DEPTH_LIMIT);
	if (env_var) {
		profiling_depth_limit = atoi(env_var);
	}

	// General initialization
	num_workers = worker_thread_num;
	init_memory_allocator();
	create_root_node();

	// Initialize indexer
	int j;
	for (j=0; j<CHAR_MAX; j++) {
		indexer[j] = 0;
		indexer_lock[j] = (myth_internal_lock_t *) myth_malloc(sizeof(myth_internal_lock_t));
		myth_internal_lock_init(indexer_lock[j]);
	}

	// Make prof folder
	mkdir(DIR_FOR_PROF_DATA, S_IRWXU | S_IRWXG | S_IROTH);
	FILE * fp;
	fp = fopen(FILE_FOR_TASK_DATA, "w");
	fclose(fp);

	// PAPI
	// Initialize PAPI library
	retval = PAPI_library_init(PAPI_VER_CURRENT);
	if (retval != PAPI_VER_CURRENT)
		PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", retval);
	// Initialize PAPI's thread function
	retval = PAPI_thread_init( (unsigned long (*) (void)) real_pthread_self);
	if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_thread_init", retval);
}

void profiler_init_worker(int worker) {
	// PROFILER OFF
	if (profiler_off) return;

	// Ant: [PAPI] register this worker thread
	if (worker != 0) {
		retval = PAPI_register_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_register_thread", retval);
	}

	// Initialize values variable
	g_envs[worker].EventSet = PAPI_NULL;
	g_envs[worker].values = (long long *) myth_malloc(NUMBER_OF_PAPI_EVENTS * sizeof(long long));

	// Initialize EventSet
	if ((retval = PAPI_create_eventset(&g_envs[worker].EventSet)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_create_eventset", retval);
	if ((retval = PAPI_add_event(g_envs[worker].EventSet, PAPI_L1_TCM)) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", retval);
	if ((retval = PAPI_add_event(g_envs[worker].EventSet, PAPI_FP_OPS)) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", retval);
	if ((retval = PAPI_start(g_envs[worker].EventSet)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_start", retval);

}

void profiler_fini_worker(int worker) {
	// PROFILER OFF
	if (profiler_off) return;

	if ((retval = PAPI_stop(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_stop", retval);
	if (worker != 0) {
		retval = PAPI_unregister_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_unregister_thread", retval);
	}
}

void profiler_fini() {
	int i;
	for (i=0; i<CHAR_MAX; i++) {
		myth_internal_lock_destroy(indexer_lock[i]);
	}
}

task_node_t profiler_create_new_node(task_node_t parent, int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	// Out of profiling limit
	if (parent == NULL || parent->level == profiling_depth_limit)
		return NULL;

	task_node_t new_node;

	// Allocate memory
	new_node = profiler_malloc_task_node(worker);

	// Set up fields
	new_node->level = parent->level + 1;
	new_node->parent_index = parent->index;
	new_node->time_record = NULL;
	// Indexing
	myth_internal_lock_lock(indexer_lock[(int) new_node->level]);
	new_node->index = indexer[(int) new_node->level]++;
	myth_internal_lock_unlock(indexer_lock[(int) new_node->level]);

	return new_node;
}

time_record_t create_time_record(int type, int worker, double val) {
	time_record_t record;
	record = profiler_malloc_time_record(worker);
	record->type = type;
	record->worker = worker;
	record->counters.time = val;
	record->counters.counter1 = 0;
	record->counters.counter2 = 0;
	record->next = NULL;
	return record;
}

void profiler_add_time_start(task_node_t node, int worker, int start_code) {
	// PROFILER OFF
	if (profiler_off) return;

	// Out of profiling limit
	if (node == NULL) return;

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
	if ((retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_read", retval);
	record->counters.counter1 = g_envs[worker].values[0];
	record->counters.counter2 = g_envs[worker].values[1];
	//TODO: Must be at the end
	record->counters.time = profiler_get_curtime();

}

void profiler_add_time_stop(task_node_t node, int worker, int stop_code) {
	// PROFILER OFF
	if (profiler_off) return;

	// Out of profiling limit
	if (node == NULL) return;

	//TODO: Must be at the begining
	double time = profiler_get_curtime();

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
	if ((retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_read", retval);
	record->counters.counter1 = g_envs[worker].values[0];
	record->counters.counter2 = g_envs[worker].values[1];

}

task_node_t profiler_get_root_node() {
	return root_node;
}

void profiler_free_time_record(time_record_t record) {
	/*time_record_t t;
	//myth_running_env_t env = myth_get_current_env();
	while (record != NULL) {
		printf("truoc khi lay next: %p \n", record);
		t = record->next;
		//printf("push to freelist: %p\n", record);
		//myth_freelist_push(env->record_mem.freelist, record);
		printf("myth_free: %p, type %d, worker %d, next %p\n", record, record->type, record->worker, t);
		myth_free(record, sizeof(time_record));
		printf("tien toi\n");
		record = t;
	}
	printf("thoat ra khoi free_time_record\n");*/
}

void profiler_free_task_node(task_node_t node) {
	/*printf("node level %d index %d, %p\n", node->level, node->index, node->time_record);
	profiler_free_time_record(node->time_record);
	//myth_running_env_t env = myth_get_current_env();
	//myth_freelist_push(env->record_mem.freelist, node);
	printf("task myth_free: %p\n", node);
	myth_free(node, sizeof(task_node));*/
}

void profiler_output_task_data(task_node_t node) {
	// PROFILER OFF
	if (profiler_off) return;

	// Open file
	FILE *fp;
	fp = fopen(FILE_FOR_TASK_DATA, "a");

	// Write to file
	time_record_t t = node->time_record;
	while (t != NULL) {
		fprintf(fp, "%d, %d, %d, ", node->level, node->index, node->parent_index);
		fprintf(fp, "%d, %d, ", t->type, t->worker);
		fprintf(fp, "%lf, %lld, %lld\n", t->counters.time, t->counters.counter1, t->counters.counter2);
		t = t->next;
	}

	node->time_record = NULL;

	fclose(fp);
}

