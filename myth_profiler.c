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


#define PROFILER_RUNTIME_INSTRUMENT_OFF 1

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
void profiler_function_init();
void profiler_function_fini();
void profiler_function_instrument(int , char * , char * , int , int );
void profiler_function_write();


unsigned long long profiler_get_curtime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1.0E6 + tv.tv_usec;
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
	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	unsigned int usage = env->num_task_nodes * sizeof(profiler_task_node) +
							env->num_time_records * sizeof(profiler_time_record) +
							env->num_function_records * sizeof(profiler_function_record);
	if (usage < profiler_mem_size_limit << 20)
		return 1;
	else
		return 0;
}
profiler_task_node_t profiler_malloc_task_node(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	profiler_task_node_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_task_node));
	//assert(ret != NULL);

	return ret;
}

profiler_time_record_t profiler_malloc_time_record(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	profiler_time_record_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_time_record));
	//assert(ret != NULL);

	return ret;
}

long long * profiler_malloc_long_long_array(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	long long * ret = NULL;
	if (profiler_num_papi_events > 0) {
		ret = myth_flmalloc(worker, profiler_num_papi_events * sizeof(long long));
		//assert(ret != NULL);
	}

	return ret;
}

profiler_function_record_t profiler_malloc_function_record(int worker) {
	// PROFILER OFF
	if (profiler_off) return NULL;

	profiler_function_record_t ret = NULL;
	ret = myth_flmalloc(worker, sizeof(profiler_function_record));
	//assert(ret != NULL);

	return ret;
}

void profiler_free_task_node(int worker, profiler_task_node_t node) {
#ifdef PROFILER_ON
	assert(node);
	if (node->level != 0)
		myth_flfree(worker, strlen(node->tree_path) * sizeof(char), node->tree_path);
	myth_flfree(worker, sizeof(profiler_task_node), node);
#endif /*PROFILER_ON*/
}

void profiler_free_long_long_array(int worker, long long * values) {
#ifdef PROFILER_ON
	if (profiler_num_papi_events > 0) {
		assert(values);
		myth_flfree(worker, profiler_num_papi_events * sizeof(long long), values);
	}
#endif /*PROFILER_ON*/
}

void profiler_free_time_record(int worker, profiler_time_record_t record) {
#ifdef PROFILER_ON
	assert(record);
	profiler_free_long_long_array(worker, record->values);
	myth_flfree(worker, sizeof(profiler_time_record), record);
#endif /*PROFILER_ON*/
}

void profiler_free_function_record(int worker, profiler_function_record_t record) {
#ifdef PROFILER_ON
	assert(record);
	profiler_free_long_long_array(worker, record->values);
	myth_flfree(worker, sizeof(profiler_function_record), record);
#endif /*PROFILER_ON*/
}

//char * profiler_get_data_file_name_for_worker() {
//	return profiler_filename;
//}

char * profiler_get_data_file_name_for_general() {
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

	// PROFILER_DEPTH_LIMIT environment variable
	env_var = getenv(ENV_PROFILER_DEPTH_LIMIT);
	if (env_var) {
		profiler_depth_limit = atoi(env_var);
	}

	// PROFILER_NUM_PAPI_EVENTS environment variable
	env_var = getenv(ENV_PROFILER_NUM_PAPI_EVENTS);
	if (env_var) {
		profiler_num_papi_events = atoi(env_var);
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

	// General initialization
	profiler_num_workers = worker_thread_num;

	int i;

	// Make prof folder
	mkdir(DIR_FOR_PROF_DATA, S_IRWXU | S_IRWXG | S_IROTH);
	// Open overview data file
	profiler_fp_overview = fopen(profiler_get_data_file_name_for_general(), "w");
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

	// OTF: initialize
	myManager = OTF_FileManager_open(100);
	assert(myManager);
	char * trace_file_name = malloc( ( strlen(profiler_trace_name) + 11 )* sizeof(char) );
	sprintf(trace_file_name, "tsprof/%s_lib", profiler_trace_name);
	myWriter = OTF_Writer_open(trace_file_name, profiler_num_workers, myManager);
	assert(myWriter);
	profiler_otf_errno = OTF_Writer_writeDefTimerResolution(myWriter, 0, 1000000);
	assert(profiler_otf_errno);

	// OTF: register DefKeyValue
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_OFF, OTF_INT32, "profiler_off", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_NUM_WORKERS, OTF_INT32, "profiler_num_workers", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_MEM_SIZE_LIMIT, OTF_INT32, "profiler_mem_size_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_DEPTH_LIMIT, OTF_INT32, "profiler_depth_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_NUM_PAPI_EVENTS, OTF_INT32, "profiler_num_papi_events", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_WATCH_FROM, OTF_INT32, "profiler_watch_from", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_WATCH_TO, OTF_INT32, "profiler_watch_to", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_WATCH_MODE, OTF_INT32, "profiler_watch_mode", NULL);
	assert(profiler_otf_errno);
	for (i=0; i<profiler_num_papi_events; i++) {
		profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_PAPI_EVENT_1+i, OTF_UINT64, "papi_event_i", papi_event_names[i]);
		assert(profiler_otf_errno);
	}
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_TASK_LEVEL, OTF_INT32, "task level", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_TASK_TREE_PATH, OTF_BYTE_ARRAY, "task tree path", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_TIME_RECORD_TYPE, OTF_INT32, "time record type", NULL);
	assert(profiler_otf_errno);

	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_START_FUNCTION, OTF_INT32, "start function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_START_SOURCE, OTF_INT32, "start source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_START_TIME_RECORD_TYPE, OTF_INT32, "start time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_STOP_FUNCTION, OTF_INT32, "stop function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_STOP_SOURCE, OTF_INT32, "stop source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_STOP_TIME_RECORD_TYPE, OTF_INT32, "stop time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other start time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(myWriter, 0, KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other stop time record types", NULL);
	assert(profiler_otf_errno);


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
	profiler_otf_errno = OTF_Writer_writeDefinitionCommentKV(myWriter, 0, "Overview information", kvlist);
	assert(profiler_otf_errno);

	// OTF: create MasterControl entries for all processes
	OTF_MasterControl * mc = OTF_Writer_getMasterControl(myWriter);
	for (i=0; i<profiler_num_workers; i++) {
		OTF_MasterControl_append(mc, i+1, i+1);
	}
	// OTF: create all streams
	for (i=0; i<profiler_num_workers; i++) {
		OTF_Writer_getStream(myWriter, i+1);
	}
	// OTF: register processes
	for (i=0; i<profiler_num_workers; i++) {
		char * worker_name = (char *) malloc(20 * sizeof(char));
		sprintf(worker_name, "Worker thread %d", i);
		profiler_otf_errno = OTF_Writer_writeDefProcess(myWriter, i+1, i+1, worker_name, 0);
		assert(profiler_otf_errno);
	}

	// OTF: initialize second OTF trace
	profiler_function_init();

#endif /*PROFILER_ON*/
}

void profiler_init_worker(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	// OTF: write event BeginProcess
	unsigned long long time = profiler_get_curtime();
	//printf("worker %d is about to writeBeginProcess.\n", worker);
	profiler_otf_errno = OTF_Writer_writeBeginProcess(myWriter, time, worker + 1);
	assert(profiler_otf_errno);

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
	g_envs[worker].head = NULL;
	g_envs[worker].tail = NULL;
	g_envs[worker].num_time_records = 0;
	g_envs[worker].head_node = NULL;
	g_envs[worker].tail_node = NULL;
	g_envs[worker].num_task_nodes = 0;
	g_envs[worker].head_fr = NULL;
	g_envs[worker].tail_fr = NULL;
	g_envs[worker].num_function_records = 0;

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

void profiler_fini() {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off)
		return ;

	// Destroy overview file lock
	myth_internal_lock_destroy(profiler_lock_fp_overview);
	//free(profiler_lock_fp_overview);

	// Close overview file
	fclose(profiler_fp_overview);

	// OTF: finalize
	OTF_Writer_close(myWriter);
	OTF_FileManager_close(myManager);

	// OTF: initialize second OTF trace
	profiler_function_fini();

#endif /*PROFILER_ON*/
}

void profiler_fini_worker(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
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
	profiler_otf_errno = OTF_Writer_writeEndProcess(myWriter, profiler_get_curtime(), worker + 1);
	assert(profiler_otf_errno);
#endif /*PROFILER_ON*/
}

profiler_task_node_t profiler_create_root_node(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return NULL;

	if (PROFILER_RUNTIME_INSTRUMENT_OFF) return NULL;

	// Allocate memory
	profiler_task_node_t new_node;
	new_node = profiler_malloc_task_node(worker);

	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Attach to env's time record list
	if (env->num_task_nodes == 0) {
		env->head_node = new_node;
		env->tail_node = new_node;
	} else {
		//assert(env->tail != NULL);
		env->tail_node->next = new_node;
		env->tail_node = new_node;
	}
	env->num_task_nodes++;

	// Set up fields
	new_node->tree_path = "0";
	new_node->index = profiler_RSHash(new_node->tree_path, strlen(new_node->tree_path));
	new_node->level = 0;
	new_node->function_name = "function_name"; // not implemented yet
	new_node->head_scl = 0; // not implemented yet
	new_node->tail_scl = 0; // not implemented yet
	new_node->num_child_tasks = 0;

	// Return
	return new_node;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

profiler_task_node_t profiler_create_new_node(profiler_task_node_t parent, int worker, int level) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return NULL;

	if (PROFILER_RUNTIME_INSTRUMENT_OFF) return NULL;

	// Out of profiling limit
	if (parent == NULL || parent->level == profiler_depth_limit)
		return NULL;

	// Measurement data limitation (must be after record data assignment)
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);

	// Allocate memory
	profiler_task_node_t new_node;
	new_node = profiler_malloc_task_node(worker);

	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Attach to env's time record list
	if (env->num_task_nodes == 0) {
		env->head_node = new_node;
		env->tail_node = new_node;
	} else {
		//assert(env->tail != NULL);
		env->tail_node->next = new_node;
		env->tail_node = new_node;
	}
	env->num_task_nodes++;

	// Set up fields
	parent->num_child_tasks++;
	int length = strlen(parent->tree_path) + 4;
	//printf("length=%d\n", length);
	new_node->tree_path = (char *) myth_flmalloc(worker, length * sizeof(char));
	sprintf(new_node->tree_path, "%s_%d", parent->tree_path, parent->num_child_tasks);
	new_node->index = profiler_RSHash(new_node->tree_path, strlen(new_node->tree_path));
	new_node->level = parent->level + 1;
	new_node->function_name = "function_name"; // not implemented yet
	new_node->head_scl = 0; // not implemented yet
	new_node->tail_scl = 0; // not implemented yet
	new_node->num_child_tasks = 0;

	// Return
	return new_node;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

//void profiler_delete_task_node(profiler_task_node_t node) {
//#ifdef PROFILER_ON
//	if (node != NULL)
//		profiler_free_task_node(node->worker, node);
//#endif /*PROFILER_ON*/
//}

profiler_time_record_t profiler_create_time_record(int type, int worker, double time) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return NULL;

	// Allocate memory
	profiler_time_record_t record;
	record = profiler_malloc_time_record(worker);

	// Set up fields
	record->type = type;
	record->task_index = 0;
	record->time = time;
	record->scl = 0;
	record->values = profiler_malloc_long_long_array(worker);
	int i;
	for (i=0; i<profiler_num_papi_events; i++) {
		record->values[i] = 0;
	}
	record->next = NULL;

	// Return
	return record;
#else
	return NULL;
#endif /*PROFILER_ON*/
}

void profiler_add_time_start(void * thread_t, int worker, int start_code) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	if (PROFILER_RUNTIME_INSTRUMENT_OFF) return;

	myth_thread_t thread = (myth_thread_t) thread_t;
	profiler_task_node_t node = thread->node;

	// Out of profiling limit
	if (node == NULL) {
		return;
	}

	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Create time record
	profiler_time_record_t record = NULL;
	record = profiler_create_time_record(start_code << 1, worker, 0);

	// Attach to env's time record list
	if (env->num_time_records == 0) {
		env->head = record;
		env->tail = record;
	} else {
		//assert(env->tail != NULL);
		env->tail->next = record;
		env->tail = record;
	}
	env->num_time_records++;

	// Task index
	record->task_index = node->index;

	// Counter values
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
		int i;
		for (i=0; i<profiler_num_papi_events; i++)
			record->values[i] = g_envs[worker].values[i];
	}

	//TODO: Must be at the end
	// Time
	record->time = profiler_get_curtime();

	// Measurement data limitation (must be after record data assignment)
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);
#endif /*PROFILER_ON*/
}

void profiler_add_time_stop(void * thread_t, int worker, int stop_code) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	if (PROFILER_RUNTIME_INSTRUMENT_OFF) return;

	myth_thread_t thread = (myth_thread_t) thread_t;
	profiler_task_node_t node = thread->node;

	// Out of profiling limit
	if (node == NULL) {
		return;
	}

	//TODO: Must be at the begining
	// Time
	unsigned long long time = profiler_get_curtime();

	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Create time record
	profiler_time_record_t record = NULL;
	record = profiler_create_time_record((stop_code << 1) + 1, worker, time);

	// Attach to env's time record list
	if (env->num_time_records == 0) {
		env->head = record;
		env->tail = record;
	} else {
		//assert(env->tail != NULL);
		env->tail->next = record;
		env->tail = record;
	}
	env->num_time_records++;

	// Task index
	record->task_index = node->index;

	// Counter values
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
		int i;
		for (i=0; i<profiler_num_papi_events; i++)
			record->values[i] = g_envs[worker].values[i];
	}

	// Measurement data limitation (must be after record data assignment)
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);
#endif /*PROFILER_ON*/
}

void profiler_write_to_file(int worker) {
#ifdef PROFILER_ON
	// PROFILER OFF
	if (profiler_off) return;

	// Get start time
	unsigned long long start_time = profiler_get_curtime();

	// Get environment
	myth_running_env_t env = &g_envs[worker];


	// Write task_nodes
	profiler_task_node_t node = env->head_node;
	profiler_task_node_t node_t;
	int num_nodes_written = 0;
	int num_nodes = env->num_task_nodes;
	while (node != NULL) {
		// Get element
		node_t = node->next;
		// Create key-value list for task_node
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
		// Free memory
		profiler_free_task_node(worker, node);
		// Increment counter
		num_nodes_written++;
		node = node_t;
	}
	env->head_node = NULL;
	env->tail_node = NULL;
	env->num_task_nodes = 0;


	// Write time records
	profiler_time_record_t record = env->head;
	profiler_time_record_t record_t = NULL;
	int num_records_written = 0;
	int num_records = env->num_time_records;
	while (record != NULL) {

		// Key-value list
		OTF_KeyValueList * kvlist;
		kvlist = OTF_KeyValueList_new();
		assert(kvlist);

		// Get numbers of starts and stops
		int i;
		int num_starts = 0;
		int num_stops = 0;
		profiler_time_record_t start_record = NULL;
		profiler_time_record_t stop_record = NULL;
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
					profiler_time_record_t record_temp = record;
					for (i=0; i<num_starts; i++, record_temp=record_temp->next)
						start_types_str[i] = record_temp->type;
				}
				if (num_stops > 1) {
					stop_types_str = malloc(num_stops * sizeof(char));
					int i;
					profiler_time_record_t record_temp = record;
					for (i=0; i<num_stops; i++, record_temp=record_temp->next)
						stop_types_str[i] = record_temp->type;
				}

				if (start_record->time == stop_record->time) {
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
				} else {
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
						printf("%d: Errorrrrrrrrrrrrrrrrrrrr\n%s\n%lld\n", worker, otf_strerr, start_record->time);
						assert(profiler_otf_errno);
					}
					profiler_otf_errno = OTF_Writer_writeLeaveKV(myWriter, stop_record->time, stop_record->task_index, worker+1, stop_record->scl, kvlist2);
					if (profiler_otf_errno == 0) {
						printf("%d: Errorrrrr\n%s\n%lld\n", worker, otf_strerr, stop_record->time);
						assert(profiler_otf_errno);
					}
					num_records_written += 2;

				}
			}

		}

		// Free memory
		profiler_time_record_t record_temp;
		while (record != record_t) {
			record_temp = record->next;
			profiler_free_time_record(worker, record);
			record = record_temp;
		}
	}
	env->head = NULL;
	env->tail = NULL;
	env->num_time_records = 0;


	// Get stop time
	unsigned long long stop_time = profiler_get_curtime();

	// Output period info
	myth_internal_lock_lock(profiler_lock_fp_overview);
	fprintf(profiler_fp_overview, "\nprofiler_write_to_file()\n");
	fprintf(profiler_fp_overview, "%lld\n", start_time);
	fprintf(profiler_fp_overview, "worker: %d\n", worker);
	fprintf(profiler_fp_overview, "number of time records: %d/%d\n", num_records_written, num_records);
	fprintf(profiler_fp_overview, "number of nodes deleted: %d/%d\n", num_nodes_written, num_nodes);
	fprintf(profiler_fp_overview, "%lld\n\n", stop_time);
	myth_internal_lock_unlock(profiler_lock_fp_overview);

	// Call profiler_function_write()
	profiler_function_write();

#endif /*PROFILER_ON*/
}

void profiler_otf_init() {
	// OTF: initialize
	profiler_otf_manager = OTF_FileManager_open(100);
	assert(profiler_otf_manager);
	char * trace_file_name = malloc( ( strlen(profiler_trace_name) + 11 )* sizeof(char) );
	sprintf(trace_file_name, "tsprof/%s_app", profiler_trace_name);
	profiler_otf_writer = OTF_Writer_open(trace_file_name, profiler_num_workers, profiler_otf_manager);
	assert(profiler_otf_writer);
	profiler_otf_errno = OTF_Writer_writeDefTimerResolution(profiler_otf_writer, 0, 1000000);
	assert(profiler_otf_errno);

	int i;

	// OTF: register DefKeyValue
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_OFF, OTF_INT32, "profiler_off", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_NUM_WORKERS, OTF_INT32, "profiler_num_workers", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_MEM_SIZE_LIMIT, OTF_INT32, "profiler_mem_size_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_DEPTH_LIMIT, OTF_INT32, "profiler_depth_limit", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_NUM_PAPI_EVENTS, OTF_INT32, "profiler_num_papi_events", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_WATCH_FROM, OTF_INT32, "profiler_watch_from", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_WATCH_TO, OTF_INT32, "profiler_watch_to", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_WATCH_MODE, OTF_INT32, "profiler_watch_mode", NULL);
	assert(profiler_otf_errno);
	for (i=0; i<profiler_num_papi_events; i++) {
		profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_PAPI_EVENT_1+i, OTF_UINT64, "papi_event_i", papi_event_names[i]);
		assert(profiler_otf_errno);
	}
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_TASK_LEVEL, OTF_INT32, "task level", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_TASK_TREE_PATH, OTF_BYTE_ARRAY, "task tree path", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_TIME_RECORD_TYPE, OTF_INT32, "time record type", NULL);
	assert(profiler_otf_errno);

	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_START_FUNCTION, OTF_INT32, "start function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_START_SOURCE, OTF_INT32, "start source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_START_TIME_RECORD_TYPE, OTF_INT32, "start time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_STOP_FUNCTION, OTF_INT32, "stop function", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_STOP_SOURCE, OTF_INT32, "stop source", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_STOP_TIME_RECORD_TYPE, OTF_INT32, "stop time record type", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_START_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other start time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_STOP_OTHER_TIME_RECORD_TYPES, OTF_BYTE_ARRAY, "other stop time record types", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_FILE_NAME, OTF_BYTE_ARRAY, "file name", NULL);
	assert(profiler_otf_errno);
	profiler_otf_errno = OTF_Writer_writeDefKeyValue(profiler_otf_writer, 0, KEY_PROFILER_LINE_NUMBER, OTF_INT32, "line number", NULL);
	assert(profiler_otf_errno);

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
	profiler_otf_errno = OTF_Writer_writeDefinitionCommentKV(profiler_otf_writer, 0, "Overview information", kvlist);
	assert(profiler_otf_errno);

	// OTF: create MasterControl entries for all processes
	OTF_MasterControl * mc = OTF_Writer_getMasterControl(profiler_otf_writer);
	for (i=0; i<profiler_num_workers; i++) {
		OTF_MasterControl_append(mc, i+1, i+1);
	}
	// OTF: create all streams
	for (i=0; i<profiler_num_workers; i++) {
		OTF_Writer_getStream(profiler_otf_writer, i+1);
	}
	// OTF: register processes
	for (i=0; i<profiler_num_workers; i++) {
		char * worker_name = (char *) malloc(20 * sizeof(char));
		sprintf(worker_name, "Worker thread %d", i);
		profiler_otf_errno = OTF_Writer_writeDefProcess(profiler_otf_writer, i+1, i+1, worker_name, 0);
		assert(profiler_otf_errno);
	}
}

void profiler_otf_fini() {
	// OTF: finalize
	OTF_Writer_close(profiler_otf_writer);
	OTF_FileManager_close(profiler_otf_manager);
}

void profiler_function_init() {
#ifdef PROFILER_ON
	profiler_otf_init();
#endif /*PROFILER_ON*/
}

void profiler_function_fini() {
#ifdef PROFILER_ON
	profiler_otf_fini();
#endif /*PROFILER_ON*/
}

void profiler_function_instrument(int level, char * tree_path, char * filename, int line, int code) {
#ifdef PROFILER_ON
	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Get current worker thread
	int worker = env->rank;

	// Measurement data limitation (must be after record data assignment)
	if (profiler_check_memory_usage() == 0)
		profiler_write_to_file(worker);

	// Allocate memory
	profiler_function_record_t new_record;
	new_record = profiler_malloc_function_record(worker);

	// Attach to env's time record list
	if (env->num_function_records == 0) {
		env->head_fr = new_record;
		env->tail_fr = new_record;
	} else {
		//assert(env->tail != NULL);
		env->tail_fr->next = new_record;
		env->tail_fr = new_record;
	}
	env->num_function_records++;

	// Set up fields
	new_record->type = code;
	new_record->file = filename;
	new_record->line = line;
	new_record->level = level;
	new_record->tree_path = tree_path;
	new_record->next = NULL;
	new_record->time = profiler_get_curtime();
	new_record->values = profiler_malloc_long_long_array(worker);

	// Counter values
	if (profiler_num_papi_events > 0) {
		if ((profiler_retval = PAPI_read(g_envs[worker].EventSet, g_envs[worker].values)) != PAPI_OK)
			profiler_PAPI_fail(__FILE__, __LINE__, "PAPI_read", profiler_retval);
		int i;
		for (i=0; i<profiler_num_papi_events; i++)
			new_record->values[i] = g_envs[worker].values[i];
	}
#endif /*PROFILER_ON*/
}

void profiler_function_write() {
#ifdef PROFILER_ON
	// Get environment
	myth_running_env_t env;
	env = myth_get_current_env();

	// Get current worker thread
	int worker = env->rank;

	// Write function_records
	profiler_function_record_t record = env->head_fr;
	profiler_function_record_t record_t;
	int num_records_written = 0;
	//int num_records = env->num_function_records;
	while (record != NULL) {
		// Get element
		record_t = record->next;
		// Create key-value list for function_record
		OTF_KeyValueList * kvlist;
		kvlist = OTF_KeyValueList_new();
		assert(kvlist);

		// Append level, tree_path, file, line, counters
		profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_TASK_LEVEL, record->level);
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_TASK_TREE_PATH, (unsigned char *) record->tree_path, strlen(record->tree_path));
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendByteArray(kvlist, KEY_PROFILER_FILE_NAME, (unsigned char *) record->file, strlen(record->file));
		assert(profiler_otf_errno == 0);
		profiler_otf_errno = OTF_KeyValueList_appendInt32(kvlist, KEY_PROFILER_LINE_NUMBER, record->line);
		assert(profiler_otf_errno == 0);
		int i;
		for (i=0; i<profiler_num_papi_events; i++) {
			profiler_otf_errno = OTF_KeyValueList_appendUint64(kvlist, KEY_PROFILER_PAPI_EVENT_1 + i, record->values[i]);
			assert(profiler_otf_errno == 0);
		}
		// Write function_record
		unsigned int counter = (profiler_num_papi_events > 0)?record->values[0]:0;
		profiler_otf_errno = OTF_Writer_writeCounterKV(profiler_otf_writer, record->time, worker+1, record->type, counter, kvlist);
		//assert(profiler_otf_errno);
		if (profiler_otf_errno != 1) {
			printf("OTF Error at %s:%d:\n%s\nType=%d\nWorker=%d\n", __FILE__, __LINE__, otf_strerr, record->type, worker);
			//assert(profiler_otf_errno);
		}

		// Free memory
		profiler_free_function_record(worker, record);
		// Increment counter
		num_records_written++;
		record = record_t;
	}
	env->head_fr = NULL;
	env->tail_fr = NULL;
	env->num_function_records = 0;
	//fprintf(stderr, "profiler_function_write(): %d out of %d\n", num_records_written, num_records);
#endif /*PROFILER_ON*/
}

char * profiler_function_create_tree_path(char * tree_path, int next_value) {
#ifdef PROFILER_ON
	int length = strlen(tree_path) + 4;
	char * new_tree_path = (char *) malloc(length * sizeof(char));
	sprintf(new_tree_path, "%s_%d", tree_path, next_value);
	return new_tree_path;
#else
	return NULL;
#endif /*PROFILER_ON*/
}
