/*
 * myth_profiler.c
 *
 *  Created on: 2012/10/02
 *      Author: denjo
 */

#include <sys/stat.h>
#include <stdarg.h>
#include "myth_profiler.h"
#include "myth_worker.h"
#include "myth_desc.h"
#include "myth_internal_lock.h"
#include "myth_init.h"
#include "myth_worker_func.h"
#include "assert.h"
#include "string.h"
#include "otf.h"

#define KEY_PROFILER_OFF 1
#define KEY_PROFILER_NUM_WORKERS 2
#define KEY_PROFILER_MEM_SIZE_LIMIT 3
#define KEY_PROFILER_DEPTH_LIMIT 4
#define KEY_PROFILER_NUM_PAPI_EVENTS 5
#define KEY_PROFILER_WATCH_FROM 6
#define KEY_PROFILER_WATCH_TO 7
#define KEY_PROFILER_WATCH_MODE 8
#define KEY_PROFILER_PAPI_EVENT_1 9
#define KEY_PROFILER_PAPI_EVENT_2 10
#define KEY_PROFILER_PAPI_EVENT_3 11
#define KEY_PROFILER_PAPI_EVENT_4 12
#define KEY_PROFILER_TASK_LEVEL 13
#define KEY_PROFILER_TASK_TREE_PATH 14
#define KEY_PROFILER_TIME_RECORD_TYPE 15

#define KEY_PROFILER_START_FUNCTION 16
#define KEY_PROFILER_START_SOURCE 17
#define KEY_PROFILER_START_TIME_RECORD_TYPE 18
#define KEY_PROFILER_STOP_FUNCTION 19
#define KEY_PROFILER_STOP_SOURCE 20
#define KEY_PROFILER_STOP_TIME_RECORD_TYPE 21

#define KEY_PROFILER_OTHER_TIME_RECORD_TYPES 22
#define KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES 23
#define KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES 24

#define KEY_PROFILER_FILE_NAME 25
#define KEY_PROFILER_LINE_NUMBER 26
#define KEY_PROFILER_FUNCTION 27
#define KEY_PROFILER_NUM_SUBTASKS 28
#define KEY_PROFILER_SPAWN_INDEX 29
#define KEY_PROFILER_SYNC_TASK_1 30
#define KEY_PROFILER_SYNC_TASK_2 31
#define KEY_PROFILER_SYNC_TASK_3 32
#define KEY_PROFILER_SYNC_TASK_4 33
#define KEY_PROFILER_SYNC_TASK_5 34


// For profiler
int profiler_num_workers = 0;	// number of workers

// Environment variables
char profiler_off = 0;				// To turn profiler off; on by default
char profiler_depth_limit = CHAR_MAX;	// Profiling depth limit, CHAR_MAX (127) means unlimited
char profiler_num_papi_events = 0;
int profiler_mem_size_limit;
char profiler_watch_from;
char profiler_watch_mode = 0;
char profiler_watch_to;
char * profiler_trace_name;
char profiler_lib_ins_off = 0;
char profiler_app_ins_off = 0;

// PAPI Events
int papi_event_codes[MAX_NUM_PAPI_EVENTS];
int default_papi_event_codes[MAX_NUM_PAPI_EVENTS] = {PAPI_L3_TCM, PAPI_L2_TCM, PAPI_L1_TCM, PAPI_L1_DCM};
char * papi_event_names[MAX_NUM_PAPI_EVENTS];
char * default_papi_event_names[MAX_NUM_PAPI_EVENTS] = {"PAPI_L3_TCM", "PAPI_L2_TCM", "PAPI_L1_TCM", "PAPI_L1_DCM"};

// PAPI
__thread int profiler_retval;

// Data filenames
__thread char * profiler_filename;

// OTF
OTF_FileManager * myManager;
OTF_Writer * myWriter;
__thread int profiler_otf_errno;
OTF_FileManager * profiler_otf_manager; // For application-level code instrumentation
OTF_Writer * profiler_otf_writer; // For application-level code instrumentation

// File for information
FILE * profiler_fp_overview;
myth_internal_lock_t * profiler_lock_fp_overview;


// Headers
void profiler_otf_init(OTF_FileManager **, OTF_Writer **, char * );
void profiler_otf_fini(OTF_FileManager **, OTF_Writer ** );
void profiler_libins_init();
void profiler_libins_fini();
void profiler_appins_init();
void profiler_appins_fini();
void profiler_write_to_file(int );
void profiler_libins_write_to_file(int );
void profiler_appins_write_to_file(int );
char profiler_appins_instrument(void * node, int line, int inscode, ...);

profiler_time_t profiler_get_curtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1.0E9 + tv.tv_usec * 1.0E3;
}

void profiler_PAPI_fail(char *file, int line, char *call, int retval)
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
    	fprintf(stderr, "Error in %s\nretval=%d\n", call, retval);
    	PAPI_perror(PAPI_strerror(retval));
    }
    fprintf(stderr, "\n");
    exit(1);
}

unsigned int profiler_RSHash(char* str, unsigned int len)
{
   unsigned int b    = 378551;
   unsigned int a    = 63689;
   unsigned int hash = 0;
   unsigned int i    = 0;

   for(i = 0; i < len; str++, i++)
   {
      hash = hash * a + (*str);
      a    = a * b;
   }

   return hash;
}

void profiler_otf_check(char * file, int line, int errno, int expected) {
	if (errno != expected) {
		printf("OTF Error at %s:%d:\n%s\n", file, line, otf_strerr);
		assert(profiler_otf_errno);
	}
}

int profiler_check_memory_usage() {
#ifdef PROFILER_ON
	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	unsigned int usage = env->num_libins_nodes * sizeof(profiler_libins_task_node) +
	  env->num_libins_records * sizeof(profiler_libins_time_record) +
	  env->num_appins_nodes * sizeof(profiler_appins_task_node) +
	  env->num_appins_records * sizeof(profiler_appins_time_record);
	if (usage < profiler_mem_size_limit << 20)
		return 1;
	else
		return 0;
#else
	return 1;
#endif /*PROFILER_ON*/
}
profiler_libins_task_node_t profiler_malloc_libins_task_node(int worker) {
	/* PROFILER_OFF */
	if (profiler_off) return NULL;

	profiler_libins_task_node_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_libins_task_node));
	//assert(ret != NULL);

	return ret;
}

profiler_libins_time_record_t profiler_malloc_libins_time_record(int worker) {
	/* PROFILER_OFF */
	if (profiler_off) return NULL;

	profiler_libins_time_record_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_libins_time_record));
	//assert(ret != NULL);

	return ret;
}

profiler_appins_task_node_t profiler_malloc_appins_task_node(int worker) {
	/* PROFILER_OFF */
	if (profiler_off) return NULL;

	profiler_appins_task_node_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_appins_task_node));
	//assert(ret != NULL);

	return ret;
}

profiler_appins_time_record_t profiler_malloc_appins_time_record(int worker) {
	/* PROFILER_OFF */
	if (profiler_off) return NULL;

	profiler_appins_time_record_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_appins_time_record));
	//assert(ret != NULL);

	return ret;
}

void profiler_free_libins_task_node(int worker, profiler_libins_task_node_t node) {
#ifdef PROFILER_ON
	assert(node);
	if (node->level != 0)
		myth_flfree(worker, (strlen(node->tree_path)+1) * sizeof(char), node->tree_path);
	myth_flfree(worker, sizeof(profiler_libins_task_node), node);
#endif /*PROFILER_ON*/
}

void profiler_free_libins_time_record(int worker, profiler_libins_time_record_t record) {
#ifdef PROFILER_ON
	assert(record);
	myth_flfree(worker, sizeof(profiler_appins_time_record), record);
#endif /*PROFILER_ON*/
}

void profiler_free_appins_task_node(int worker, profiler_appins_task_node_t node) {
#ifdef PROFILER_ON
	assert(node);
	if (node->tree_path)
	  myth_flfree(worker, (strlen(node->tree_path)+1) * sizeof(char), node->tree_path);
	myth_flfree(worker, sizeof(profiler_appins_task_node), node);
#endif /*PROFILER_ON*/
}

void profiler_free_appins_time_record(int worker, profiler_appins_time_record_t record) {
#ifdef PROFILER_ON
	assert(record);
	myth_flfree(worker, sizeof(profiler_appins_time_record), record);
#endif /*PROFILER_ON*/
}

profiler_libins_time_record_t profiler_create_time_record(int type, int worker, profiler_time_t time) {
#ifdef PROFILER_ON
	/* PROFILER_OFF */
	if (profiler_off) return NULL;

	// Allocate memory
	profiler_libins_time_record_t record;
	record = profiler_malloc_libins_time_record(worker);

	// Set up fields
	record->type = type;
	record->task_index = 0;
	record->time = time;
	record->scl = 0;
	int i;
	for (i=0; i<profiler_num_papi_events; i++) {
		record->values[i] = 0;
	}
	record->node = NULL;
	record->next = NULL;

	// Return
	return record;
#else
	return NULL;
#endif /*PROFILER_ON*/
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

	// PROFILER_DEPTH_LIMIT environment variable
	env_var = getenv(ENV_PROFILER_DEPTH_LIMIT);
	if (env_var) {
		profiler_depth_limit = atoi(env_var);
	}

	// PROFILER_NUM_PAPI_EVENTS environment variable
	env_var = getenv(ENV_PROFILER_NUM_PAPI_EVENTS);
	if (env_var) {
		profiler_num_papi_events = atoi(env_var);
		if (profiler_num_papi_events > MAX_NUM_PAPI_EVENTS)
			profiler_num_papi_events = MAX_NUM_PAPI_EVENTS;
	}

	// PROFILER_MEM_SIZE_LIMIT environment variable
	env_var = getenv(ENV_PROFILER_MEM_SIZE_LIMIT);
	if (env_var) {
		profiler_mem_size_limit = atoi(env_var);
	} else {
		profiler_mem_size_limit = EACH_CORE_MEMORY_SIZE_LIMIT;
	}

	// PROFILER_WATCH_FROM environment variable
	env_var = getenv(ENV_PROFILER_WATCH_FROM);
	if (env_var) {
		profiler_watch_from = atoi(env_var);
	} else {
		profiler_watch_from = profiler_depth_limit + 1;
		if (profiler_watch_from < 0)
		  profiler_watch_from = 127;
	}

	// PROFILER_WATCH_MODE environment variable
	env_var = getenv(ENV_PROFILER_WATCH_MODE);
	if (env_var) {
		profiler_watch_mode = atoi(env_var);
	}

	// PROFILER_WATCH_TO environment variable
	env_var = getenv(ENV_PROFILER_WATCH_TO);
	if (env_var) {
		profiler_watch_to = atoi(env_var);
	} else {
		profiler_watch_to = 127;
	}

	// PROFILER_TRACE_NAME environment variable
	env_var = getenv(ENV_PROFILER_TRACE_NAME);
	if (env_var) {
		profiler_trace_name = env_var;
	} else {
		profiler_trace_name = "anyname";
	}

	// PROFILER_LIB_INS_OFF environment variable
	env_var = getenv(ENV_PROFILER_LIB_INSTRUMENT_OFF);
	if (env_var)
		profiler_lib_ins_off = atoi(env_var);

	// PROFILER_OFF environment variable
	env_var = getenv(ENV_PROFILER_APP_INSTRUMENT_OFF);
	if (env_var)
		profiler_app_ins_off = atoi(env_var);


	// General initialization
	profiler_num_workers = worker_thread_num;

	int i;

	// Make prof folder
	mkdir(DIR_FOR_PROF_DATA, S_IRWXU | S_IRWXG | S_IROTH);
	// Open overview data file
	char * trace_file_name = malloc( ( strlen(profiler_trace_name) + 20 ) * sizeof(char) );
	sprintf(trace_file_name, "tsprof/%s_summary.txt", profiler_trace_name);
	profiler_fp_overview = fopen(trace_file_name, "w");
	profiler_lock_fp_overview = (myth_internal_lock_t *) malloc(sizeof(myth_internal_lock_t));
	myth_internal_lock_init(profiler_lock_fp_overview);

	// PAPI
	// Initialize PAPI library
	if (profiler_num_papi_events > 0) {
		profiler_retval = PAPI_library_init(PAPI_VER_CURRENT);
		if (profiler_retval != PAPI_VER_CURRENT)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_library_init", profiler_retval);
		// Initialize PAPI's thread function
		profiler_retval = PAPI_thread_init( (unsigned long (*) (void)) real_pthread_self);
		if (profiler_retval != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_thread_init", profiler_retval);
	}

	// PROFILER_PAPI_EVENT_x environment variables
	char name[MAX_PAPI_EVENT_NAME_LENGTH];
	for (i=0; i<profiler_num_papi_events; i++) {
		sprintf(name, "%s%d", ENV_PROFILER_PAPI_EVENT_NAME, i+1);
		papi_event_names[i] = getenv(name);
		if (papi_event_names[i] == NULL) {
			papi_event_names[i] = default_papi_event_names[i];
			papi_event_codes[i] = default_papi_event_codes[i];
		} else {
			if ((profiler_retval = PAPI_event_name_to_code(papi_event_names[i], &papi_event_codes[i])) != PAPI_OK)
				profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_event_name_to_code", profiler_retval);
		}
	}

	// OTF: initialize OTF traces
	profiler_libins_init();
	profiler_appins_init();

#endif /*PROFILER_ON*/
}

void profiler_init_worker(int worker) {
#ifdef PROFILER_ON
	/* PROFILER_OFF */
	if (profiler_off) return;

	// OTF: write event BeginProcess
	profiler_time_t time = profiler_get_curtime();
	if (myWriter) {
		profiler_otf_errno = OTF_Writer_writeBeginProcess(myWriter, time, worker + 1);
		assert(profiler_otf_errno);
	}
	if (profiler_otf_writer) {
		profiler_otf_errno = OTF_Writer_writeBeginProcess(profiler_otf_writer, time, worker + 1);
		assert(profiler_otf_errno);
	}

	// Ant: [PAPI] register this worker thread
	if (worker != 0 && profiler_num_papi_events > 0) {
		profiler_retval = PAPI_register_thread();
		if (profiler_retval != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_register_thread", profiler_retval);
	}

	// Initialize worker thread-local variables
	g_envs[worker].EventSet = PAPI_NULL;
	if (profiler_num_papi_events > 0)
		g_envs[worker].values = (long long *) malloc(profiler_num_papi_events * sizeof(long long));
	g_envs[worker].num_libins_nodes = 0;
	g_envs[worker].num_libins_records = 0;
	g_envs[worker].head_lr = NULL;
	g_envs[worker].tail_lr = NULL;
	g_envs[worker].num_appins_nodes = 0;
	g_envs[worker].num_appins_records = 0;
	g_envs[worker].head_ar = NULL;
	g_envs[worker].tail_ar = NULL;

	int i;

	// Initialize EventSet
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_create_eventset(&g_envs[worker].EventSet)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_create_eventset", profiler_retval);
		for (i=0; i<profiler_num_papi_events; i++) {
			if ((profiler_retval = PAPI_add_event(g_envs[worker].EventSet, papi_event_codes[i])) != PAPI_OK)
				profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_add_event", profiler_retval);
		}
		if ((profiler_retval = PAPI_start(g_envs[worker].EventSet)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_start", profiler_retval);
	}
#endif /*PROFILER_ON*/
}

void profiler_fini_worker(int worker) {
#ifdef PROFILER_ON
	/* PROFILER_OFF */
	if (profiler_off) return;

	// Write data to file
	profiler_write_to_file(worker);

	// Worker thread-local variables
	if (profiler_filename != NULL)
		free(profiler_filename);

	// Stop PAPI
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_stop(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_stop", profiler_retval);
		if (worker != 0) {
			profiler_retval = PAPI_unregister_thread();
			if (profiler_retval != PAPI_OK)
				profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_unregister_thread", profiler_retval);
		}
	}

	// OTF: write event EndProcess
	if (myWriter) {
		profiler_otf_errno = OTF_Writer_writeEndProcess(myWriter, profiler_get_curtime(), worker + 1);
		assert(profiler_otf_errno);
	}
	if (profiler_otf_writer) {
		profiler_otf_errno = OTF_Writer_writeEndProcess(profiler_otf_writer, profiler_get_curtime(), worker + 1);
		assert(profiler_otf_errno);
	}
#endif /*PROFILER_ON*/
}

void profiler_fini() {
#ifdef PROFILER_ON
	/* PROFILER_OFF */
	if (profiler_off) return;

	// Destroy overview file lock
	myth_internal_lock_destroy(profiler_lock_fp_overview);
	//free(profiler_lock_fp_overview);

	// Close overview file
	fclose(profiler_fp_overview);

	// OTF: terminate OTF traces
	profiler_libins_fini();
	profiler_appins_fini();

#endif /*PROFILER_ON*/
}

void profiler_write_to_file(int worker) {
#ifdef PROFILER_ON
	/* PROFILER_OFF */
	if (profiler_off) return;

	profiler_libins_write_to_file(worker);
	profiler_appins_write_to_file(worker);

#endif /*PROFILER_ON*/
}


profiler_libins_task_node_t profiler_create_root_node() {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER OFF */
	if (profiler_off) return NULL;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return NULL;

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Get current worker thread */
	int worker = env->rank;

	/* Allocate memory */
	profiler_libins_task_node_t new_node;
	new_node = profiler_malloc_libins_task_node(worker);

	/* Increment env's libins node counter */
	env->num_libins_nodes++;

	/* Set fields */
	new_node->tree_path = "0";
	new_node->index = profiler_RSHash(new_node->tree_path, strlen(new_node->tree_path));
	new_node->level = 0;
	new_node->function_name = "function_name"; // not implemented yet
	new_node->head_scl = 0; // not implemented yet
	new_node->tail_scl = 0; // not implemented yet
	new_node->subtask_count = 0;
	new_node->record_count = 0;
	new_node->ended = 0;

	/* Return */
	return new_node;
#else
	return NULL;
#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#else
	return NULL;
#endif /*PROFILER_ON*/
}

profiler_libins_task_node_t profiler_create_new_node(profiler_libins_task_node_t parent) {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return NULL;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return NULL;

	/* Check profiling limit */
	if (parent == NULL || parent->level == profiler_depth_limit)
		return NULL;

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Get current worker thread */
	int worker = env->rank;

	/* Allocate memory */
	profiler_libins_task_node_t new_node;
	new_node = profiler_malloc_libins_task_node(worker);

	/* Increment env's libins node counter */
	env->num_libins_nodes++;

	/* Set fields */
	int length = strlen(parent->tree_path) + 5;
	new_node->tree_path = (char *) myth_flmalloc(worker, length * sizeof(char));
	sprintf(new_node->tree_path, "%s_%d", parent->tree_path, parent->subtask_count);
	new_node->index = profiler_RSHash(new_node->tree_path, strlen(new_node->tree_path));
	new_node->level = parent->level + 1;
	new_node->function_name = "function_name"; // not implemented yet
	new_node->head_scl = 0; // not implemented yet
	new_node->tail_scl = 0; // not implemented yet
	new_node->subtask_count = 0;
	new_node->record_count = 0;
	new_node->ended = 0;
	parent->subtask_count++;

	/* Return */
	return new_node;
#else
	return NULL;
#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#else
	return NULL;
#endif /*PROFILER_ON*/
}

void profiler_add_time_start(void * thread_t, int worker, int start_code) {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return;

	/* Get task node */
	assert(thread_t);
	myth_thread_t thread = (myth_thread_t) thread_t;
	profiler_libins_task_node_t node = thread->node;

	/* Check profiling limit */
	if (node == NULL) return;

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Allocate memory */
	profiler_libins_time_record_t record = NULL;
	record = profiler_create_time_record(start_code << 1, worker, 0);

	/* Attach to env's time record list */
	if (env->num_libins_records == 0) {
		env->head_lr = record;
		env->tail_lr = record;
	} else {
		//assert(env->tail != NULL);
		env->tail_lr->next = record;
		env->tail_lr = record;
	}
	env->num_libins_records++;

	/* Set remained fields */
	record->task_index = node->index;
	record->node = node;

	/* Increment node's counter */
	node->record_count++;

	/* Get time, counter values */
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
	}
	profiler_time_t time = profiler_get_curtime();

	/* Assign time, counter values */
	record->time = time;
	int i;
	for (i=0; i<profiler_num_papi_events; i++)
		record->values[i] = g_envs[worker].values[i];

	/* Measurement data limitation (must be after record data assignment) */
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);
#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

void profiler_add_time_stop(void * thread_t, int worker, int stop_code) {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return;

	/* Get task node */
	assert(thread_t);
	myth_thread_t thread = (myth_thread_t) thread_t;
	profiler_libins_task_node_t node = thread->node;

	/* Check profiling limit */
	if (node == NULL) return;

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Get time, counter values */
	profiler_time_t time = profiler_get_curtime();
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
	}

	/* Allocate memory */
	profiler_libins_time_record_t record = NULL;
	record = profiler_create_time_record((stop_code << 1) + 1, worker, time);

	/* Attach to env's time record list */
	if (env->num_libins_records == 0) {
		env->head_lr = record;
		env->tail_lr = record;
	} else {
		//assert(env->tail != NULL);
		env->tail_lr->next = record;
		env->tail_lr = record;
	}
	env->num_libins_records++;

	/* Set remained fields */
	record->task_index = node->index;
	record->node = node;

	/* Increment node's counter */
	node->record_count++;

	/* Task's end*/
	if (stop_code == 13)
		node->ended = 1;

	/* Assign counter values */
	int i;
	for (i=0; i<profiler_num_papi_events; i++)
		record->values[i] = g_envs[worker].values[i];

	/* Measurement data limitation (must be after record data assignment) */
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);
#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}


void profiler_libins_init() {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return;

	char * trace_file_name = malloc( ( strlen(profiler_trace_name) + 15 ) * sizeof(char) );
	sprintf(trace_file_name, "tsprof/%s_lib", profiler_trace_name);
	profiler_otf_init(&myManager, &myWriter, trace_file_name);

#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

void profiler_libins_fini() {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return;

	profiler_otf_fini(&myManager, &myWriter);

#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

void profiler_libins_write_to_file(int worker) {
#ifdef PROFILER_ON
#ifdef PROFILER_LIB_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_LIB_INS_OFF */
	if (profiler_lib_ins_off) return;

	/* Get start time */
	profiler_time_t start_time = profiler_get_curtime();

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Write libins_records */
	profiler_libins_time_record_t record = env->head_lr;
	profiler_libins_time_record_t record_t = NULL;
	int num_records_written = 0;
	int num_records = env->num_libins_records;
	int num_nodes_deleted = 0;
	int num_nodes = env->num_libins_nodes;
	while (record != NULL) {

		// Key-value list
		OTF_KeyValueList * kvlist;
		kvlist = OTF_KeyValueList_new();
		assert(kvlist);

		// Get numbers of starts and stops
		int i;
		int num_starts = 0;
		int num_stops = 0;
		profiler_libins_time_record_t start_record = NULL;
		profiler_libins_time_record_t stop_record = NULL;
		record_t = record;
		while (record_t != NULL && record_t->type % 2 == 0) {
			num_starts++;
			start_record = record_t;
			record_t = record_t->next;
		}
		stop_record = record_t;
		while (record_t != NULL && record_t->type % 2 == 1) {
			num_stops++;
			record_t = record_t->next;
		}

		if (num_starts == 0) {

			fprintf(stderr, "Error: num_starts == 0, num_stops == %d (type=%d)\n", num_stops, record->type);
			// Add a stop time record without any associated start time record
			profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_FUNCTION, record->task_index);
			assert(profiler_otf_errno == 0);
			profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_SOURCE, record->scl);
			assert(profiler_otf_errno == 0);
			profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_TIME_RECORD_TYPE, record->type);
			assert(profiler_otf_errno == 0);
			for (i=0; i<profiler_num_papi_events; i++) {
				profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1 + i, record->values[i]);
				assert(profiler_otf_errno == 0);
			}
			profiler_otf_errno = OTF_Writer_writeEventCommentKV(myWriter, record->time, worker+1, "stop time record without any associated start time record", kvlist);
			assert(profiler_otf_errno);
			num_records_written++;

		} else {

			if (num_stops == 0) {

				fprintf(stderr, "Error: num_stops == 0, num_starts == %d\n", num_starts);
				// Add a start time record without any associated stop time record
				profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_FUNCTION, start_record->task_index);
				assert(profiler_otf_errno == 0);
				profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_SOURCE, start_record->scl);
				assert(profiler_otf_errno == 0);
				profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_TIME_RECORD_TYPE, start_record->type);
				assert(profiler_otf_errno == 0);
				for (i=0; i<profiler_num_papi_events; i++) {
					profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1 + i, start_record->values[i]);
					assert(profiler_otf_errno == 0);
				}
				profiler_otf_errno = OTF_Writer_writeEventCommentKV(myWriter, start_record->time, worker+1, "start time record without any associated stop time record", kvlist);
				assert(profiler_otf_errno);
				num_records_written++;

			} else {

				unsigned char * start_types_str = NULL;
				unsigned char * stop_types_str = NULL;
				if (num_starts > 1) {
					start_types_str = malloc(num_starts * sizeof(char));
					int i;
					profiler_libins_time_record_t record_temp = record;
					for (i=0; i<num_starts; i++, record_temp=record_temp->next)
						start_types_str[i] = record_temp->type;
				}
				if (num_stops > 1) {
					stop_types_str = malloc(num_stops * sizeof(char));
					int i;
					profiler_libins_time_record_t record_temp = record;
					for (i=0; i<num_stops; i++, record_temp=record_temp->next)
						stop_types_str[i] = record_temp->type;
				}

				if (start_record->time == stop_record->time)
					fprintf(stderr, "Error: start_record->time == stop_record->time, num_starts=%d, num_stops=%d\n", num_starts, num_stops);
/*				if (start_record->time == stop_record->time) {
					fprintf(stderr, "Error: start_record->time == stop_record->time, num_starts=%d, num_stops=%d\n", num_starts, num_stops);
					// Aggregate start and stop records
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_FUNCTION, start_record->task_index);
					assert(profiler_otf_errno == 0);
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_SOURCE, start_record->scl);
					assert(profiler_otf_errno == 0);
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_START_TIME_RECORD_TYPE, start_record->type);
					assert(profiler_otf_errno == 0);
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_FUNCTION, stop_record->task_index);
					assert(profiler_otf_errno == 0);
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_SOURCE, stop_record->scl);
					assert(profiler_otf_errno == 0);
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_STOP_TIME_RECORD_TYPE, stop_record->type);
					assert(profiler_otf_errno == 0);
					if (start_types_str) {
						profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES, (unsigned char *) start_types_str, num_starts);
						assert(profiler_otf_errno == 0);
					}
					if (stop_types_str) {
						profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES, (unsigned char *) stop_types_str, num_stops);
						assert(profiler_otf_errno == 0);
					}
					profiler_otf_errno = OTF_Writer_writeEventCommentKV(myWriter, start_record->time, worker+1, "aggregate start and stop time record", kvlist);
					assert(profiler_otf_errno);
					num_records_written++;
				} else {*/
					// Write each start and stop
					OTF_KeyValueList * kvlist2;
					kvlist2 = OTF_KeyValueList_new();
					assert(kvlist2);

					// kvlist for start record
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_TIME_RECORD_TYPE, start_record->type);
					assert(profiler_otf_errno == 0);
					if (start_types_str) {
						profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES, (unsigned char *) start_types_str, num_starts);
						assert(profiler_otf_errno == 0);
					}
					for (i=0; i<profiler_num_papi_events; i++) {
						profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1 + i, start_record->values[i]);
						assert(profiler_otf_errno == 0);
					}
					// kvlist2 for stop record
					profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist2, KEY_PROFILER_TIME_RECORD_TYPE, stop_record->type);
					assert(profiler_otf_errno == 0);
					if (stop_types_str) {
						profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES, (unsigned char *) stop_types_str, num_stops);
						assert(profiler_otf_errno == 0);
					}
					for (i=0; i<profiler_num_papi_events; i++) {
						profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist2, KEY_PROFILER_PAPI_EVENT_1 + i, stop_record->values[i]);
						assert(profiler_otf_errno == 0);
					}

					// Write Enter and Leave records
					profiler_otf_errno = OTF_Writer_writeEnterKV(myWriter, start_record->time, start_record->task_index, worker+1, start_record->scl, kvlist);
					if (profiler_otf_errno == 0) {
						printf("%d: Errorrrrrrrrrrrrrrrrrrrr\n%s\n%llu\n", worker, otf_strerr, start_record->time);
						assert(profiler_otf_errno);
					}
					profiler_otf_errno = OTF_Writer_writeLeaveKV(myWriter, stop_record->time, stop_record->task_index, worker+1, stop_record->scl, kvlist2);
					if (profiler_otf_errno == 0) {
						printf("%d: Errorrrrr\n%s\n%llu\n", worker, otf_strerr, stop_record->time);
						assert(profiler_otf_errno);
					}
					num_records_written += 2;

				//}
			}

		}

		// Free memory
		profiler_libins_time_record_t record_temp;
		while (record != record_t) {
			record_temp = record->next;
			profiler_libins_task_node_t node = record->node;
			node->record_count--;
			if (node->record_count == 0 && node->ended == 1) {

				// Write libins_node
				OTF_KeyValueList * kvlist;
				kvlist = OTF_KeyValueList_new();
				assert(kvlist);
				profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_TASK_LEVEL, node->level);
				assert(profiler_otf_errno == 0);
				profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_TASK_TREE_PATH, (unsigned char *) node->tree_path, strlen(node->tree_path));
				assert(profiler_otf_errno == 0);
				// Write task_node
				profiler_otf_errno = OTF_Writer_writeDefFunctionKV(myWriter, worker+1, node->index, node->function_name, 0, node->head_scl, kvlist);
				assert(profiler_otf_errno);

				// Delete libins_node
				g_envs[node->worker].num_appins_nodes--;
				profiler_free_libins_task_node(node->worker, node);
				num_nodes_deleted++;

			}
			profiler_free_libins_time_record(worker, record);
			record = record_temp;
		}
	}
	env->head_lr = NULL;
	env->tail_lr = NULL;
	env->num_libins_records = 0;

	// Get stop time
	profiler_time_t stop_time = profiler_get_curtime();

	// Output period info
	myth_internal_lock_lock(profiler_lock_fp_overview);
	fprintf(profiler_fp_overview, "\nprofiler_libins_write_to_file()\n");
	fprintf(profiler_fp_overview, "worker: %d\n", worker);
	fprintf(profiler_fp_overview, "number of libins records (written/total): %d/%d\n", num_records_written, num_records);
	fprintf(profiler_fp_overview, "number of deleted libins nodes (on various workers): %d\n", num_nodes_deleted);
	fprintf(profiler_fp_overview, "number of libins nodes (of this worker): %d\n", num_nodes);
	fprintf(profiler_fp_overview, "from: %llu\n", start_time);
	fprintf(profiler_fp_overview, "to  : %llu\n", stop_time);
	fprintf(profiler_fp_overview, "Time cost: %llu\n", stop_time-start_time);
	myth_internal_lock_unlock(profiler_lock_fp_overview);

#endif /*PROFILER_LIB_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}


void profiler_otf_init(OTF_FileManager **manager, OTF_Writer **writer, char * trace_name) {
	// OTF: initialize
	*manager = OTF_FileManager_open(100);
	assert(*manager);
	*writer = OTF_Writer_open(trace_name, profiler_num_workers, *manager);
	assert(*writer);
	profiler_otf_errno = OTF_Writer_writeDefTimerResolution(*writer, 0, 1000000);
	assert(profiler_otf_errno);

	int i;

	// OTF: register DefKeyValue
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_OFF, OTF_INT32, "profiler_off", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_NUM_WORKERS, OTF_INT32, "profiler_num_workers", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_MEM_SIZE_LIMIT, OTF_INT32, "profiler_mem_size_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_DEPTH_LIMIT, OTF_INT32, "profiler_depth_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_NUM_PAPI_EVENTS, OTF_INT32, "profiler_num_papi_events", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_WATCH_FROM, OTF_INT32, "profiler_watch_from", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_WATCH_TO, OTF_INT32, "profiler_watch_to", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_WATCH_MODE, OTF_INT32, "profiler_watch_mode", NULL);
	assert(profiler_otf_errno);
	for (i=0; i<profiler_num_papi_events; i++) {
		profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_PAPI_EVENT_1+i, OTF_UINT64, "papi_event_i", papi_event_names[i]);
		assert(profiler_otf_errno);
	}
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_TASK_LEVEL, OTF_INT32, "task level", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_TASK_TREE_PATH, OTF_BYTE_ARRAY, "task tree path", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_TIME_RECORD_TYPE, OTF_INT32, "time record type", NULL);
	assert(profiler_otf_errno);

	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_START_FUNCTION, OTF_INT32, "start function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_START_SOURCE, OTF_INT32, "start source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_START_TIME_RECORD_TYPE, OTF_INT32, "start time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_STOP_FUNCTION, OTF_INT32, "stop function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_STOP_SOURCE, OTF_INT32, "stop source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_STOP_TIME_RECORD_TYPE, OTF_INT32, "stop time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other start time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other stop time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_FILE_NAME, OTF_BYTE_ARRAY, "file name", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_LINE_NUMBER, OTF_INT32, "line number", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_FUNCTION, OTF_BYTE_ARRAY, "function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_NUM_SUBTASKS, OTF_INT32, "number of subtasks", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_SPAWN_INDEX, OTF_INT32, "spawn index", NULL);
	assert(profiler_otf_errno);
	for (i=0; i<MAX_LENGTH_SUBTASK_LIST; i++) {
		profiler_otf_errno = OTF_Writer_writeDefKeyValue(*writer, 0, KEY_PROFILER_SYNC_TASK_1+i, OTF_INT32, "sync_task_i", NULL);
		assert(profiler_otf_errno);
	}

	// OTF: write KeyValue
	OTF_KeyValueList * kvlist;
	kvlist = OTF_KeyValueList_new();
	assert(kvlist);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_OFF, (int) profiler_off);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_NUM_WORKERS, (int) profiler_num_workers);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_MEM_SIZE_LIMIT, (int) profiler_mem_size_limit);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_DEPTH_LIMIT, (int) profiler_depth_limit);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_NUM_PAPI_EVENTS, (int) profiler_num_papi_events);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_WATCH_FROM, (int) profiler_watch_from);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_WATCH_TO, (int) profiler_watch_to);
	assert(profiler_otf_errno == 0);
	profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_WATCH_MODE, (int) profiler_watch_mode);
	assert(profiler_otf_errno == 0);
	for (i=0; i<profiler_num_papi_events; i++) {
		profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1+i, 1);
		assert(profiler_otf_errno == 0);
	}

	// OTF: write a NoOpKV event
	profiler_otf_errno = OTF_Writer_writeDefinitionCommentKV(*writer, 0, "Overview information", kvlist);
	assert(profiler_otf_errno);

	// OTF: create MasterControl entries for all processes
	OTF_MasterControl * mc = OTF_Writer_getMasterControl(*writer);
	for (i=0; i<profiler_num_workers; i++) {
		OTF_MasterControl_append(mc, i+1, i+1);
	}
	// OTF: force creating all streams
	for (i=0; i<profiler_num_workers; i++) {
		OTF_Writer_getStream(*writer, i+1);
	}
	// OTF: register processes
	for (i=0; i<profiler_num_workers; i++) {
		char * worker_name = (char *) malloc(20 * sizeof(char));
		sprintf(worker_name, "Worker thread %d", i);
		profiler_otf_errno = OTF_Writer_writeDefProcess(*writer, i+1, i+1, worker_name, 0);
		assert(profiler_otf_errno);
	}
}

void profiler_otf_fini(OTF_FileManager **manager, OTF_Writer **writer) {
	// OTF: finalize
	OTF_Writer_close(*writer);
	OTF_FileManager_close(*manager);
}


void profiler_appins_init() {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return;

	char * trace_file_name = malloc( ( strlen(profiler_trace_name) + 15 ) * sizeof(char) );
	sprintf(trace_file_name, "tsprof/%s_app", profiler_trace_name);
	profiler_otf_init(&profiler_otf_manager, &profiler_otf_writer, trace_file_name);

#endif /*PROFILER_APP_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

void profiler_appins_fini() {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return;

	profiler_otf_fini(&profiler_otf_manager, &profiler_otf_writer);

#endif /*PROFILER_APP_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

void profiler_appins_write_to_file(int worker) {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return;

	/* Get start time */
	profiler_time_t start_time = profiler_get_curtime();

	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Write appins_records */
	profiler_appins_time_record_t record = env->head_ar;
	profiler_appins_time_record_t record_t;
	int num_records_written = 0;
	int num_records = env->num_appins_records;
	int num_nodes_deleted = 0;
	int num_nodes = env->num_appins_nodes;
	while (record != NULL) {
		// Get element
		record_t = record->next;
		// Create key-value list for appins_time_record
		OTF_KeyValueList * kvlist;
		kvlist = OTF_KeyValueList_new();
		assert(kvlist);

		// Append level, tree_path, file, line, counters
		profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_TASK_LEVEL, record->node->level);
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_TASK_TREE_PATH, (unsigned char *) record->node->tree_path, strlen(record->node->tree_path));
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_FILE_NAME, (unsigned char *) record->node->file, strlen(record->node->file));
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_FUNCTION, (unsigned char *) record->node->function, strlen(record->node->file));
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_LINE_NUMBER, record->line);
		assert(profiler_otf_errno == 0);
		int i;
		for (i=0; i<profiler_num_papi_events; i++) {
			profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1 + i, record->values[i]);
			assert(profiler_otf_errno == 0);
		}
		if (record->inscode == PROFILER_APPINS_SPAWN) {
			profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_SPAWN_INDEX, record->subtask_list[0]);
			assert(profiler_otf_errno == 0);
		}
		if (record->inscode == PROFILER_APPINS_SYNC) {
			int num = record->subtask_list[0];
			for (i=0; i<num; i++) {
				profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_SYNC_TASK_1 + i, record->subtask_list[i+1]);
				assert(profiler_otf_errno == 0);
			}
		}
		if (record->inscode == PROFILER_APPINS_END) {
			profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_NUM_SUBTASKS, record->node->subtask_count);
			assert(profiler_otf_errno == 0);
		}
		// Write appins_time_record
		unsigned int counter = (profiler_num_papi_events > 0)?record->values[0]:0;
		profiler_otf_errno = OTF_Writer_writeCounterKV(profiler_otf_writer, record->time, worker+1, record->inscode, counter, kvlist);
		//assert(profiler_otf_errno);
		if (profiler_otf_errno != 1) {
			printf("OTF Error at %s:%d:\n%sInscode=%d\nWorker=%d\n\n", __FILE__, __LINE__, otf_strerr, record->inscode, worker);
			//assert(profiler_otf_errno);
		}

		// Adjust task_node
		record->node->record_count--;
		if (record->node->record_count == 0 && record->node->ended) {
			g_envs[record->node->worker].num_appins_nodes--;
			profiler_free_appins_task_node(record->node->worker, record->node);
			num_nodes_deleted++;
		}

		// Free time_record
		profiler_free_appins_time_record(worker, record);

		// Increment counter
		num_records_written++;
		record = record_t;
	}
	env->head_ar = NULL;
	env->tail_ar = NULL;
	env->num_appins_records = 0;
	//fprintf(stderr, "profiler_function_write(): %d out of %d\n", num_records_written, num_records);

	// Get stop time
	profiler_time_t stop_time = profiler_get_curtime();

	// Output period info
	myth_internal_lock_lock(profiler_lock_fp_overview);
	fprintf(profiler_fp_overview, "\nprofiler_appins_write_to_file()\n");
	fprintf(profiler_fp_overview, "worker: %d\n", worker);
	fprintf(profiler_fp_overview, "number of appins records (written/total): %d/%d\n", num_records_written, num_records);
	fprintf(profiler_fp_overview, "number of deleted appins nodes (on various workers): %d\n", num_nodes_deleted);
	fprintf(profiler_fp_overview, "number of appins nodes (of this worker): %d\n", num_nodes);
	fprintf(profiler_fp_overview, "from: %llu\n", start_time);
	fprintf(profiler_fp_overview, "to  : %llu\n", stop_time);
	fprintf(profiler_fp_overview, "Time cost: %llu\n", stop_time-start_time);
	myth_internal_lock_unlock(profiler_lock_fp_overview);

#endif /*PROFILER_APP_INSTRUMENT_ON*/
#endif /*PROFILER_ON*/
}

char * profiler_appins_create_tree_path(int worker, char * tree_path, int next_value) {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return NULL;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return NULL;

	int length = strlen(tree_path) + 5;
	char * new_tree_path = (char *) myth_flmalloc(worker, length * sizeof(char));
	sprintf(new_tree_path, "%s_%d", tree_path, next_value);
	return new_tree_path;

#else
	return NULL;
#endif /*PROFILER_APP_INSTRUMENT_ON*/
#else
	return NULL;
#endif /*PROFILER_ON*/
}

void * profiler_appins_begin(const char *file, const char *function, void *parent_node_void, char spawn_index, int line) {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return NULL;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return NULL;
	
	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Get current worker thread */
	int worker = env->rank;

	/* Get parent level and tree path */
	profiler_appins_task_node_t parent_node = (profiler_appins_task_node_t) parent_node_void;
	int level = (parent_node==NULL)?1:(parent_node->level+1);
	char * parent_tree_path = (parent_node==NULL)?"0":(parent_node->tree_path);

	/* Allocate memory */
	profiler_appins_task_node_t new_node;
	new_node = profiler_malloc_appins_task_node(worker);

	/* Increment env's appins node counter */
	env->num_appins_nodes++;

	/* Set fields */
	new_node->file = (char *) myth_flmalloc(worker, (strlen(file)+1) * sizeof(char) );
	strcpy(new_node->file, file);
	new_node->function = (char *) myth_flmalloc(worker, (strlen(function)+1) * sizeof(char) );
	strcpy(new_node->function, function);
	new_node->level = level;
	new_node->tree_path = profiler_appins_create_tree_path(worker, parent_tree_path, spawn_index);
	new_node->worker = worker;
	new_node->subtask_count = 0;
	new_node->record_count = 0;
	new_node->ended = 0;
	
	/* Call profiler_appins_instrument() for PROFILER_APPINS_BEGIN */
	profiler_appins_instrument((void *) new_node, line, PROFILER_APPINS_BEGIN);

	/* Return */
	return (void *) new_node;

#else
	return NULL;
#endif /* PROFILER_APP_INSTRUMENT_ON */
#else
	return NULL;
#endif /* PROFILER_ON */
}

char profiler_appins_instrument(void * node_void, int line, int inscode, ...) {
#ifdef PROFILER_ON
#ifdef PROFILER_APP_INSTRUMENT_ON
	/* PROFILER_OFF */
	if (profiler_off) return -1;
	/* PROFILER_APP_INS_OFF */
	if (profiler_app_ins_off) return -1;

	/* Return value */
	char retval = -1;
	
	/* Get environment */
	myth_running_env_t env;
	env = myth_get_current_env();

	/* Get current worker thread */
	int worker = env->rank;

	/* Get time, counter values */
	profiler_time_t time = 0;
	switch (inscode) {
	case PROFILER_APPINS_SPAWN:
	case PROFILER_APPINS_PAUSE:
	case PROFILER_APPINS_END:
	  // Time
	  time = profiler_get_curtime();
	  // Counter values
	  if (profiler_num_papi_events > 0)
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		  profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
	  // Break
	  break;
	}

	/* Get task_node */
	profiler_appins_task_node_t node = (profiler_appins_task_node_t) node_void;
	
	/* Measurement data limitation (must be after record data assignment) */
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);

	/* Allocate memory */
	profiler_appins_time_record_t new_record;
	new_record = profiler_malloc_appins_time_record(worker);

	/* Attach to env's time record list */
	if (env->num_appins_records == 0) {
		env->head_ar = new_record;
		env->tail_ar = new_record;
	} else {
		//assert(env->tail != NULL);
		env->tail_ar->next = new_record;
		env->tail_ar = new_record;
	}
	env->num_appins_records++;

	/* Set up fields */
	new_record->line = line;
	new_record->inscode = inscode;
	new_record->node = node;
	new_record->next = NULL;

	/* Increment node's counter*/
	node->record_count++;

	/* Particular processes basing on inscode*/
	switch (inscode) {
	  va_list vargs;
	case PROFILER_APPINS_SPAWN:
	  retval = node->subtask_count++;
	  new_record->subtask_list[0] = retval;
	  break;
	case PROFILER_APPINS_SYNC:
	  va_start(vargs, inscode);
	  int num = va_arg(vargs, int);
	  if (num > MAX_LENGTH_SUBTASK_LIST) {
		fprintf(stderr, "Error: num=%d > MAX_LENGTH_SUBTASK_LIST=%d\n", num, MAX_LENGTH_SUBTASK_LIST);
		num = MAX_LENGTH_SUBTASK_LIST;
	  }
	  int i;
	  char id;
	  new_record->subtask_list[0] = num;
	  for (i=1; i<num+1; i++) {
		id = (char) va_arg(vargs, int);
		new_record->subtask_list[i] = id;
	  }
	  va_end(vargs);
	  break;
	case PROFILER_APPINS_END:
	  node->ended = 1;
	  break;
	}
	
	/* Get time, counter values */
	switch (inscode) {
	case PROFILER_APPINS_BEGIN:
	case PROFILER_APPINS_RESUME:
	case PROFILER_APPINS_SYNC:
	  // Counter values
	  if (profiler_num_papi_events > 0)
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
		  profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
	  // Time
	  time = profiler_get_curtime();
	  // Break
	  break;
	}

	/* Assign time, counter values */
	new_record->time = time;
	int i;
	for (i=0; i<profiler_num_papi_events; i++)
	  new_record->values[i] = g_envs[worker].values[i];

	/* Return */
	return retval;

#endif /* PROFILER_APP_INSTRUMENT_ON */
#endif /* PROFILER_ON */
}

