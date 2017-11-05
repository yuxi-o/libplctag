/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include <lib/libplctag.h>
#include <platform.h>
#include <util/debug.h>
#include <util/rc_thread.h>
#include <util/refcount.h>
#include <util/vector.h>


struct rc_thread_impl_t {
    thread_p thread;
    rc_thread_func func;
    int arg_count;
    int thread_abort;
    void **args;
    void *dummy[];
};

typedef struct rc_thread_impl_t *rc_thread_impl_p;

struct rc_thread_t {
    rc_thread_impl_p impl;
};


#define MIN_THREADS (10)
static mutex_p rc_thread_mutex = NULL;
static vector_p thread_list = NULL;


static volatile int library_terminating = 0;
static thread_p reaper_thread = NULL;

/* pointer to running rc_thread_impl struct.  Used for aborting. */
THREAD_LOCAL rc_thread_impl_p this_thread = NULL;


static void rc_thread_destroy(void *rc_thread_arg, int extra_arg_count, void **extra_args);
static THREAD_FUNC(rc_thread_handler);
static THREAD_FUNC(reaper_thread_handler);



rc_thread_p rc_thread_create_impl(int arg_count, rc_thread_func func, ...)
{
    va_list va;
    rc_thread_p result = NULL;
    rc_thread_impl_p impl = NULL;
    int rc = PLCTAG_STATUS_OK;

    /* create the thread struct. */
    impl = mem_alloc(sizeof(struct rc_thread_impl_t) + (arg_count*sizeof(void*)));
    if(!impl) {
        pdebug(DEBUG_ERROR,"Unable to allocate RCThread implementation struct!");
        return NULL;
    }

    result = rc_alloc(sizeof(struct rc_thread_t), rc_thread_destroy);
    if(!result) {
        pdebug(DEBUG_ERROR,"Unable to create rc_thread_t struct!");
        mem_free(impl);
        return NULL;
    }

    impl->func = func;
    impl->arg_count = arg_count;
    impl->args = (void **)(impl+1); /* point past the thread struct. */

    /* fill in the extra args. */
    va_start(va, func);
    for(int i=0; i < impl->arg_count; i++) {
        impl->args[i] = va_arg(va, void *);
    }
    va_end(va);

    rc = thread_create(&impl->thread, rc_thread_handler, 32*1024, impl);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_WARN,"Unable to create thread!");
        mem_free(impl);
        rc_dec(result);
        return NULL;
    }

    result->impl = impl;

    return result;
}


int rc_thread_abort(rc_thread_p thread)
{
    if(thread && thread->impl) {
        thread->impl->thread_abort = 1;
        return PLCTAG_STATUS_OK;
    }

    return PLCTAG_ERR_NULL_PTR;
}


int rc_thread_check_abort()
{
    if(this_thread) {
        return this_thread->thread_abort || library_terminating;
    }

    pdebug(DEBUG_WARN,"this_thread pointer is NULL!");

    return 1; /* abort if something is wrong. */
}



/***********************************************************************
 ************************ Helper Functions *****************************
 **********************************************************************/

void rc_thread_destroy(void *rc_thread_arg, int extra_arg_count, void **extra_args)
{
    rc_thread_p thread = rc_thread_arg;

    (void)extra_arg_count;
    (void)extra_args;

    pdebug(DEBUG_INFO,"Starting.");

    if(!thread) {
        pdebug(DEBUG_WARN,"Null pointer to thread passed!");
        return;
    }

    /* signal the thread to die. */
    rc_thread_abort(thread);

    pdebug(DEBUG_INFO,"Done.");

    return;
}



THREAD_FUNC(rc_thread_handler)
{
    rc_thread_impl_p impl = arg;

    pdebug(DEBUG_INFO,"Starting.");

    if(!impl) {
        pdebug(DEBUG_WARN,"Thread pointer is NULL!");
        THREAD_RETURN(0);
    }

    if(!impl->func) {
        pdebug(DEBUG_WARN,"Thread function pointer is NULL!");
        mem_free(impl);
        THREAD_RETURN(0);
    }

    /* store the current thread in thread local storage. */
    this_thread = impl;

    /* the thread function must call rc_thread_check_abort() periodically! */
    impl->func(impl->arg_count, impl->args);

    /* store the thread pointer for later reaping. */
    critical_block(rc_thread_mutex) {
        vector_put(thread_list, vector_length(thread_list), impl->thread);
    }

    /* done, free the memory for the thread impl. */
    mem_free(impl);

    pdebug(DEBUG_INFO,"Done.");

    THREAD_RETURN(0);
}


THREAD_FUNC(reaper_thread_handler)
{
    (void) arg;

    pdebug(DEBUG_INFO,"Starting.");

    while(!library_terminating) {
        thread_p thread = NULL;

        critical_block(rc_thread_mutex) {
            thread = vector_remove(thread_list, 0);
        }

        if(thread) {
            thread_join(thread);
            thread_destroy(&thread);
        }

        sleep_ms(5); /* MAGIC */
    }

    pdebug(DEBUG_INFO,"Done.");

    THREAD_RETURN(0);
}


/***********************************************************************
 *************************  Module Functions ***************************
 **********************************************************************/

/* needed for set up of the rc_thread service */
int rc_thread_service_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing RCThread utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&rc_thread_mutex);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create RCThread mutex!");
        return rc;
    }

    /* create the vectors in which we will have the jobs stored. */
    thread_list = vector_create(MIN_THREADS, MIN_THREADS/4);
    if(!thread_list ) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of RCThreads!");
        return PLCTAG_ERR_CREATE;
    }

    rc = thread_create(&reaper_thread, reaper_thread_handler, 32*1024, NULL);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR,"Unable to create reaper thread!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Finished initializing RCThread utility.");

    return rc;
}


/*
 * called when the whole program is going to terminate.
 */
void rc_thread_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Releasing RCThread service resources.");

    /*
     * kill all the threads that are remaining.   The whole program is
     * going down so it does not matter if they do not clean up.
     */

    pdebug(DEBUG_INFO,"Terminating reaper thread.");
    library_terminating = 1;
    thread_join(reaper_thread);
    thread_destroy(&reaper_thread);

    pdebug(DEBUG_INFO,"Terminating RCThread threads.");

    /* now join all the threads that the reaper did not get. */
    for(int i=0; i < vector_length(thread_list); i++) {
        thread_p thread = vector_get(thread_list, i);
        thread_join(thread);
        thread_destroy(&thread);
    }

    pdebug(DEBUG_INFO,"Freeing RCThread list.");
    /* free the vector of jobs */
    vector_destroy(thread_list);

    pdebug(DEBUG_INFO,"Freeing RCThread mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&rc_thread_mutex);

    pdebug(DEBUG_INFO,"Finished tearing down RCThread utility resources.");
}



