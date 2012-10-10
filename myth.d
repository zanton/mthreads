myth_constructor.o: myth_constructor.c myth_original_lib.h myth_config.h \
 config.h myth_init.h myth_io.h myth_internal_lock.h myth_mem_barrier.h \
 myth_log.h myth_sched.h myth_context.h myth_worker.h myth_wsqueue.h \
 myth_misc.h myth_tls.h myth_desc.h myth_sched_proto.h myth_io_proto.h \
 myth_tls_proto.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_func.h myth_wsqueue_func.h myth_wsqueue_proto.h \
 myth_log_func.h myth_io_func.h myth_io_struct.h myth_io_poll.h \
 myth_sync_func.h myth_sync.h myth_tls_func.h
myth_context_gvar.o: myth_context_gvar.c myth_config.h config.h \
 myth_context.h
myth_if_native.o: myth_if_native.c myth_init.h myth_original_lib.h \
 myth_config.h config.h myth_io.h myth_internal_lock.h myth_mem_barrier.h \
 myth_log.h myth_sched.h myth_context.h myth_worker.h myth_wsqueue.h \
 myth_misc.h myth_tls.h myth_desc.h myth_sync.h myth_if_native.h \
 myth_worker_func.h myth_malloc_wrapper.h myth_sched_proto.h \
 myth_sched_func.h myth_wsqueue_func.h myth_wsqueue_proto.h \
 myth_log_func.h myth_io_proto.h myth_io_func.h myth_io_struct.h \
 myth_io_poll.h myth_sync_func.h myth_tls_func.h
myth_if_pthread.o: myth_if_pthread.c myth_init.h myth_original_lib.h \
 myth_config.h config.h myth_io.h myth_internal_lock.h myth_mem_barrier.h \
 myth_log.h myth_sched.h myth_context.h myth_worker.h myth_wsqueue.h \
 myth_misc.h myth_tls.h myth_desc.h myth_sched_proto.h myth_io_proto.h \
 myth_tls_proto.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_func.h myth_wsqueue_func.h myth_wsqueue_proto.h \
 myth_log_func.h myth_io_func.h myth_io_struct.h myth_io_poll.h \
 myth_sync_func.h myth_sync.h myth_tls_func.h myth_if_pthread.h
myth_init.o: myth_init.c myth_worker.h myth_sched.h myth_context.h \
 myth_config.h config.h myth_io.h myth_internal_lock.h myth_mem_barrier.h \
 myth_original_lib.h myth_log.h myth_wsqueue.h myth_misc.h \
 myth_worker_proto.h myth_desc.h myth_tls.h myth_malloc_wrapper.h \
 myth_io_proto.h myth_io_func.h myth_sched_proto.h myth_wsqueue_proto.h \
 myth_io_struct.h myth_sched_func.h myth_wsqueue_func.h \
 myth_worker_func.h myth_log_func.h myth_init.h myth_io_poll.h \
 myth_tls_func.h
myth_io.o: myth_io.c myth_desc.h myth_context.h myth_config.h config.h \
 myth_wsqueue.h myth_internal_lock.h myth_mem_barrier.h \
 myth_original_lib.h myth_misc.h myth_worker.h myth_sched.h myth_io.h \
 myth_log.h myth_io_proto.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_proto.h myth_sched_func.h myth_wsqueue_func.h \
 myth_wsqueue_proto.h myth_log_func.h myth_io_func.h myth_io_struct.h \
 myth_io_poll.h myth_init.h
myth_log.o: myth_log.c myth_log.h myth_config.h config.h
myth_malloc_wrapper.o: myth_malloc_wrapper.c myth_config.h config.h \
 myth_sched.h myth_context.h myth_sched_func.h myth_mem_barrier.h \
 myth_misc.h myth_original_lib.h myth_wsqueue.h myth_internal_lock.h \
 myth_wsqueue_func.h myth_desc.h myth_worker.h myth_io.h myth_log.h \
 myth_wsqueue_proto.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_proto.h myth_log_func.h myth_init.h myth_io_func.h \
 myth_io_proto.h myth_io_struct.h myth_io_poll.h
myth_misc.o: myth_misc.c myth_misc.h myth_config.h config.h \
 myth_original_lib.h
myth_original_lib.o: myth_original_lib.c myth_config.h config.h \
 myth_misc.h myth_original_lib.h pthread_so_path.def
myth_sched.o: myth_sched.c myth_config.h config.h myth_wsqueue.h \
 myth_internal_lock.h myth_mem_barrier.h myth_original_lib.h myth_misc.h \
 myth_sched.h myth_context.h myth_wsqueue_func.h myth_desc.h \
 myth_worker.h myth_io.h myth_log.h myth_wsqueue_proto.h \
 myth_sched_func.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_proto.h myth_log_func.h myth_init.h myth_io_func.h \
 myth_io_proto.h myth_io_struct.h myth_io_poll.h
myth_sync.o: myth_sync.c myth_misc.h myth_config.h config.h \
 myth_original_lib.h myth_desc.h myth_context.h myth_wsqueue.h \
 myth_internal_lock.h myth_mem_barrier.h myth_worker.h myth_sched.h \
 myth_io.h myth_log.h myth_sync.h myth_sync_proto.h myth_worker_func.h \
 myth_malloc_wrapper.h myth_sched_proto.h myth_sched_func.h \
 myth_wsqueue_func.h myth_wsqueue_proto.h myth_log_func.h myth_io_proto.h \
 myth_io_func.h myth_io_struct.h myth_io_poll.h myth_init.h \
 myth_sync_func.h
myth_test_.o: myth_test_.c myth.h myth_if_native.h
myth_tls.o: myth_tls.c myth_internal_lock.h myth_config.h config.h \
 myth_mem_barrier.h myth_original_lib.h myth_tls.h myth_desc.h \
 myth_context.h myth_wsqueue.h myth_misc.h myth_worker.h myth_sched.h \
 myth_io.h myth_log.h
myth_worker.o: myth_worker.c myth_worker.h myth_sched.h myth_context.h \
 myth_config.h config.h myth_io.h myth_internal_lock.h myth_mem_barrier.h \
 myth_original_lib.h myth_log.h myth_wsqueue.h myth_misc.h \
 myth_worker_proto.h myth_desc.h myth_worker_func.h myth_malloc_wrapper.h \
 myth_sched_proto.h myth_sched_func.h myth_wsqueue_func.h \
 myth_wsqueue_proto.h myth_log_func.h myth_io_proto.h myth_io_func.h \
 myth_io_struct.h myth_io_poll.h myth_init.h
search_shlib_path.o: search_shlib_path.c
