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
#include "assert.h"

// For profiler
int num_workers = 0;	// number of workers
task_node_t root_node = NULL;	//TODO: linked list of all task nodes?

// Environment variables
char profiler_off = 0;				// To turn profiler off; on by default
char profiling_depth_limit = CHAR_MAX;	// Profiling depth limit, CHAR_MAX (127) means unlimited
char num_papi_events = 2;
int papi_event_codes[MAX_NUM_PAPI_EVENTS];
int default_papi_event_codes[MAX_NUM_PAPI_EVENTS] = {PAPI_L1_TCM, PAPI_L2_TCM, PAPI_NULL, PAPI_NULL};
char * papi_event_names[MAX_NUM_PAPI_EVENTS];
char * default_papi_event_names[MAX_NUM_PAPI_EVENTS] = {"PAPI_L1_TCM", "PAPI_L2_TCM", "PAPI_NULL", "PAPI_NULL"};

// PAPI
__thread int retval;

// In-level indexer
int indexer[CHAR_MAX];	// level-based
myth_internal_lock_t * indexer_lock[CHAR_MAX]; 	// level-based

// Profiler memory observation
int num_time_records_threshold;

// Data filenames
__thread char * filename;

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
	/*int i;
	myth_running_env_t env;
	for (i=0; i<num_workers; i++){
		env = &g_envs[i];

		// Task node memory TODO: observe total mem size?
		//env->node_mem.N = 1;
		//env->node_mem.mem = (task_node_t) myth_malloc(sizeof(task_node));
		//assert(env->node_mem.mem);
		//env->node_mem.n = 1;
		//myth_freelist_init(env->node_mem.freelist);

		// Time record memory TODO: observe total mem size?
		//env->record_mem.N = 1;
		//env->record_mem.mem = (time_record_t) myth_malloc(sizeof(time_record));
		//assert(env->record_mem.mem);
		//env->record_mem.n = 1;
		//myth_freelist_init(env->record_mem.freelist);

	}*/
}

task_node_t profiler_malloc_task_node(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	task_node_t ret;

	/*node_allocator * alloc = &g_envs[worker].node_mem;*/

	/*myth_freelist_pop(alloc->freelist, ret);
	if (ret)
		return ret;*/

	/*if (alloc->n == 0) {
		alloc->N *= 2;
		alloc->mem = (task_node_t) myth_malloc(alloc->N * sizeof(task_node));
		assert(alloc->mem != NULL);
		alloc->n = alloc->N;
	}
	ret = alloc->mem;
	alloc->mem++;
	alloc->n--;*/

	// Use myth_flmalloc()
	ret = myth_flmalloc(worker, sizeof(task_node));

	//printf("task myth_malloc: %p\n", ret);
	return ret;
}

time_record_t profiler_malloc_time_record(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	time_record_t ret;

	/*record_allocator * alloc = &g_envs[worker].record_mem;*/

	/*myth_freelist_pop(alloc->freelist, ret);
	if (ret) {
		printf("pop from freelist: %p\n", &ret);
		return ret;
	}*/

	/*if (alloc->n == 0) {
		alloc->N *= 2;
		alloc->mem = (time_record_t) myth_malloc(alloc->N * sizeof(time_record));
		assert(alloc->mem != NULL);
		alloc->n = alloc->N;
	}
	ret = alloc->mem;
	alloc->mem++;
	alloc->n--;*/

	// Use myth_flmalloc()
	ret = myth_flmalloc(worker, sizeof(time_record));

	//printf("myth_malloc: %p\n", ret);
	return ret;
}

long long * profiler_malloc_long_long_array(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	long long * ret;

	// Use myth_flmalloc()
	ret = myth_flmalloc(worker, num_papi_events * sizeof(long long));

	return ret;
}

void profiler_free_long_long_array(int worker, long long * values) {
#ifdef PROFILER_ON
	myth_flfree(worker, num_papi_events * sizeof(long long), values);
#endif /*PROFILER_ON*/
}

void profiler_free_time_record(int worker, time_record_t record) {
#ifdef PROFILER_ON
	// Use myth_flfree()
	profiler_free_long_long_array(worker, record->counters.values);
	myth_flfree(worker, sizeof(time_record), record);
#endif /*PROFILER_ON*/
}

void profiler_free_task_node(int worker, task_node_t node) {
#ifdef PROFILER_ON
	// Use myth_flfree()
	myth_flfree(worker, sizeof(task_node), node);
#endif /*PROFILER_ON*/
}

char * get_data_file_name_for_worker() {
	return filename;
}

char * get_data_file_name_for_general() {
	return FILE_FOR_GENERAL_INFO;
}

void profiler_init(int worker_thread_num) {
#ifdef PROFILER_ON
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

	// NUM_PAPI_EVENTS environment variable
	env_var = getenv(ENV_NUM_PAPI_EVENTS);
	if (env_var) {
		num_papi_events = atoi(env_var);
	}

	// PAPI_EVENT_x environment variables
	int i, retvall;
	char name[MAX_PAPI_EVENT_NAME_LENGTH];
	for (i=0; i<num_papi_events; i++) {
		sprintf(name, "%s%d", ENV_PAPI_EVENT_NAME, i+1);
		papi_event_names[i] = getenv(name);
		if (papi_event_names[i] == NULL) {
			papi_event_names[i] = default_papi_event_names[i];
			papi_event_codes[i] = default_papi_event_codes[i];
		} else {
			if ((retvall = PAPI_event_name_to_code(papi_event_names[i], &papi_event_codes[i])) != PAPI_OK)
				PAPI_fail(__FILE__, __LINE__, "PAPI_event_name_to_code", retvall);
		}
	}

	// General initialization
	num_workers = worker_thread_num;
	init_memory_allocator();

	// Memory size limit
	num_time_records_threshold = EACH_CORE_MEMORY_SIZE_LIMIT * 1024 * 1024 / sizeof(time_record);

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

	// General data file
	fp = fopen(get_data_file_name_for_general(), "w");
	fprintf(fp, "Overview profile data\n");
	fprintf(fp, "num_papi_events = %d\n", num_papi_events);
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
#endif /*PROFILER_ON*/
}

void profiler_init_worker(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	// Ant: [PAPI] register this worker thread
	if (worker != 0) {
		retval = PAPI_register_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_register_thread", retval);
	}

	// Initialize worker thread-local variables
	g_envs[worker].EventSet = PAPI_NULL;
	g_envs[worker].values = (long long *) myth_malloc(num_papi_events * sizeof(long long));
	g_envs[worker].head = NULL;
	g_envs[worker].tail = NULL;
	g_envs[worker].num_time_records = 0;

	// Worker data file
	filename = myth_malloc(30 * sizeof(char));
	sprintf(filename, "%s%d%s", FILE_FOR_EACH_WORKER_THREAD, worker, ".txt");
	FILE * fp = fopen(filename, "w");
	fprintf(fp, "# level, in-level index, in-level parent_index, time type, worker, time");
	int i;
	//char event_name[20];
	for (i=0; i<num_papi_events; i++)
		fprintf(fp, ", %s", papi_event_names[i]);
	fprintf(fp, "\n");
	fclose(fp);

	// Initialize EventSet
	if ((retval = PAPI_create_eventset(&g_envs[worker].EventSet)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_create_eventset", retval);
	for (i=0; i<num_papi_events; i++) {
		if ((retval = PAPI_add_event(g_envs[worker].EventSet, papi_event_codes[i])) != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", retval);
	}
	if ((retval = PAPI_start(g_envs[worker].EventSet)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_start", retval);
#endif /*PROFILER_ON*/
}

void profiler_fini() {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return ;

	int i;
	for (i=0; i<CHAR_MAX; i++) {
		myth_internal_lock_destroy(indexer_lock[i]);
	}
#endif /*PROFILER_ON*/
}

void profiler_fini_worker(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	// Write data to file
	profiler_write_to_file(worker);

	// Worker thread-local variables
	if (filename != NULL)
		myth_free(filename, 30 * sizeof(char));

	// Stop PAPI
	if ((retval = PAPI_stop(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_stop", retval);
	if (worker != 0) {
		retval = PAPI_unregister_thread();
		if (retval != PAPI_OK)
			PAPI_fail(__FILE__, __LINE__, "PAPI_unregister_thread", retval);
	}
#endif /*PROFILER_ON*/
}

task_node_t profiler_create_root_node(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return NULL;

	task_node_t node;

	// Allocate memory
	node = profiler_malloc_task_node(worker);

	// Set up fields
	node->level = 0;
	node->parent_index = 0;
	node->counter = 0;
	node->worker = worker;
	// Indexing
	myth_internal_lock_lock(indexer_lock[(int) node->level]);
	node->index = indexer[(int) node->level]++;
	myth_internal_lock_unlock(indexer_lock[(int) node->level]);

	return node;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

task_node_t profiler_create_new_node(task_node_t parent, int worker) {
#ifdef PROFILER_ON
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
	new_node->counter = 0;
	new_node->worker = worker;
	// Indexing
	myth_internal_lock_lock(indexer_lock[(int) new_node->level]);
	new_node->index = indexer[(int) new_node->level]++;
	myth_internal_lock_unlock(indexer_lock[(int) new_node->level]);

	return new_node;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

void profiler_delete_task_node(task_node_t node) {
#ifdef PROFILER_ON
	if (node != NULL)
		profiler_free_task_node(node->worker, node);
#endif /*PROFILER_ON*/
}

void profiler_mark_delete_task_node(task_node_t node) {
#ifdef PROFILER_ON
	if (node != NULL)
		node->counter++; // means that its execution has ended, it can be deleted if there's no time record pointing to it
#endif /*PROFILER_ON*/
}

void profiler_write_to_file(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	//if (profiler_off) return;

	// Open file
	FILE *fp;
	fp = fopen(get_data_file_name_for_worker(), "a");
	assert(fp != NULL);

	// Write to file
	myth_running_env_t env = &g_envs[worker];
	time_record_t t = env->head;
	time_record_t tt;
	while (t != NULL) {
		tt = t->next;
		fprintf(fp, "%d, %d, %d, ", t->node->level, t->node->index, t->node->parent_index);
		fprintf(fp, "%d, %d, %lf, ", t->type, t->worker, t->counters.time);
		int i;
		for (i=0; i<num_papi_events; i++) {
			fprintf(fp, "%lld", t->counters.values[i]);
			if (i == num_papi_events-1)
				fprintf(fp, "\n");
			else
				fprintf(fp, ", ");
		}
		t->node->counter -= 2;
		if (t->node->counter == 1)
			profiler_delete_task_node(t->node);
		profiler_free_time_record(worker, t);
		t = tt;
	}
	env->num_time_records = 0;
	env->head = env->tail = NULL;

	// Close file
	fclose(fp);
#endif /*PROFILER_ON*/
}

time_record_t create_time_record(int type, int worker, double time) {
#ifdef PROFILER_ON
	time_record_t record;
	record = profiler_malloc_time_record(worker);
	record->type = type;
	record->worker = worker;
	record->counters.time = time;
	record->counters.values = profiler_malloc_long_long_array(worker);
	int i;
	for (i=0; i<num_papi_events; i++) {
		record->counters.values[i] = 0;
	}
	record->next = NULL;

	return record;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

void profiler_add_time_start(task_node_t node, int worker, int start_code) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	// Out of profiling limit
	if (node == NULL) return;

	// Create time record
	time_record_t record = create_time_record(start_code << 1, worker, 0);

	// Attach to env's time record list
	myth_running_env_t env;
	env = myth_get_current_env();//g_envs[worker];
	if (env->num_time_records == 0) {
		env->head = record;
		env->tail = record;
	} else {
		assert(env->tail != NULL);
		env->tail->next = record;
		env->tail = record;
	}
	// Pointer to node
	record->node = node;
	env->num_time_records++;

	// Increment node's counter
	node->counter += 2;

	// PAPI counts
	if ((retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_read", retval);
	int i;
	for (i=0; i<num_papi_events; i++)
		record->counters.values[i] = g_envs[worker].values[i];
	//TODO: Must be at the end
	record->counters.time = profiler_get_curtime();

	// Measurement data limitation (must be after record data assignment)
	if (env->num_time_records > num_time_records_threshold)
		profiler_write_to_file(worker);
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

	// Attach to env's time record list
	myth_running_env_t env;
	env = myth_get_current_env();//g_envs[worker];
	if (env->num_time_records == 0) {
		env->head = record;
		env->tail = record;
	} else {
		assert(env->tail != NULL);
		env->tail->next = record;
		env->tail = record;
	}
	// Pointer to node
	record->node = node;
	env->num_time_records++;

	// Increment node's counter
	node->counter += 2;

	// PAPI counts
	if ((retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		PAPI_fail(__FILE__, __LINE__, "PAPI_read", retval);
	int i;
	for (i=0; i<num_papi_events; i++)
		record->counters.values[i] = g_envs[worker].values[i];

	// Measurement data limitation (must be after record data assignment)
	if (env->num_time_records > num_time_records_threshold)
		profiler_write_to_file(worker);
#endif /*PROFILER_ON*/
}

