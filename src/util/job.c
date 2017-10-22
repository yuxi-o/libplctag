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


#include <platform.h>
#include <lib/libplctag.h>
#include <util/debug.h>
#include <util/vector.h>
#include <util/job.h>
#include <util/refcount.h>


#define MIN_NONBLOCKING_JOBS (100)
#define MIN_NONBLOCKING_THREADS (4)
#define MIN_BLOCKING_JOBS (20)
#define MIN_BLOCKING_THREADS (8)

typedef enum { WAITING, RUNNING, DEAD } job_status;

struct job_t {
    int is_blocking;
    job_status status;
    int arg_count;
    void** args;
    job_function func;
};


static volatile vector_p nonblocking_jobs = NULL;
static volatile vector_p nonblocking_threads = NULL;
static volatile vector_p blocking_jobs = NULL;
static volatile vector_p blocking_threads = NULL;

/* job mutex protecting the job and thread lists. */
static volatile mutex_p job_mutex = NULL;

/* job runner thread */
THREAD_FUNC(job_runner);


static volatile int library_terminating = 0;



static void job_destroy(void *job_arg, int arg_count, void **args);

/*
 * FIXME - handle blocking jobs!
 */

job_p job_create(job_function func, int is_blocking, int arg_count, ...)
{
    va_list va;
    job_p entry = rc_alloc(sizeof(struct job_t) + (arg_count*sizeof(void*)), job_destroy, 0);
    int status = PLCTAG_STATUS_OK;

    if(!entry) {
        pdebug(DEBUG_ERROR,"Cannot allocate new job entry!");
        return NULL;
    }

    entry->is_blocking = is_blocking;
    entry->status = RUNNING;
    entry->func = func;
    entry->arg_count = arg_count;
    entry->args = (void **)(entry+1); /* point past the job struct. */

    /* fill in the extra args. */
    va_start(va, arg_count);
    for(int i=0; i < arg_count; i++) {
        entry->args[i] = va_arg(va, void *);
    }
    va_end(va);

    critical_block(job_mutex) {
        /* just insert at the end. */
        if(is_blocking) {
            if((status = vector_put(blocking_jobs, vector_length(blocking_jobs), entry)) != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"Unable to insert new job into blocking job list!");
            }
        } else {
            if((status = vector_put(nonblocking_jobs, vector_length(nonblocking_jobs), entry)) != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"Unable to insert new job into nonblocking job list!");
            }
        }
    }

    /* if we did not insert it into the job list successfully, punt. */
    if(status != PLCTAG_STATUS_OK) {
        entry = rc_dec(entry);
    }

    return entry;
}




THREAD_FUNC(job_runner)
{
    vector_p job_list = arg;

    pdebug(DEBUG_INFO,"Job Runner starting.");

    if(!job_list) {
        pdebug(DEBUG_ERROR,"Job list is NULL!");
        THREAD_RETURN(0);
    }

    while(!library_terminating) {
        /* scan over all the entries looking for one that is valid and not running. */
        int index = 0;
        int done = 0;

        do {
            job_p job = NULL;

            done = 1;

            /* find a protothread that is ready to run. */
            critical_block(job_mutex) {
                for(int i=index; i < vector_length(job_list); i++) {
                    /* get a reference */
                    job = rc_inc(vector_get(job_list,i));

                    /* if it is dead or an invalid reference, then remove it. */
                    if(!job) {
                        pdebug(DEBUG_INFO,"Job %d is invalid.", i);
                        vector_remove(job_list, i);
                        i--; /* the list changed size.*/

                        continue;
                    } else if(job->status == WAITING) {
                        /* yes, so mark it as running to prevent other threads from changing it. */
                        job->status = RUNNING;
                        index = i;
                        done = 0;

                        break;
                    } else {
                        /* not a job we are interested in, either dead or running. */
                        job = rc_dec(job);
                    }
                }
            }

            if(job) {
                pdebug(DEBUG_DETAIL,"Calling job function");
                if(job->func(job->arg_count, job->args) == JOB_DONE) {
                    job->status = DEAD;
                } else {
                    /* mark it as ready to run. */
                    job->status = WAITING;
                }

                /* release the reference. */
                rc_dec(job);

                /* do not rerun that job again right away. */
                index++;
            }
        } while(!library_terminating && !done);

        /* reschedule the CPU. */
        if(!library_terminating) {
            sleep_ms(1);
        }
    }

    pdebug(DEBUG_DETAIL,"Job runner thread function exiting.");

    THREAD_RETURN(0);
}



void job_destroy(void *job_arg, int arg_count, void **args)
{
    job_p job = job_arg;

    (void)args;

    if(arg_count !=0 || !job) {
        pdebug(DEBUG_WARN,"Null pointer passed in!");
        return;
    }

    pdebug(DEBUG_INFO,"Destroying job");

    /* remove the entry from the list. */
    critical_block(job_mutex) {
        if(job->is_blocking) {
            for(int i=0; i < vector_length(blocking_jobs); i++) {
                job_p tmp = vector_get(blocking_jobs, i);

                if(tmp == job) {
                    vector_remove(blocking_jobs, i);
                    break;
                }
            }
        } else {
            for(int i=0; i < vector_length(nonblocking_jobs); i++) {
                job_p tmp = vector_get(nonblocking_jobs, i);

                if(tmp == job) {
                    vector_remove(nonblocking_jobs, i);
                    break;
                }
            }
        }
    }

    /*
     * the args should be cleaned up by the caller, either by a
     * registered clean up function or within the job itself.
     * The args array is actually allocated along with the job struct
     * and is deallocated with it by the RC framework.
     */
}




/*
 * Library initialization point for jobs.
 */


int job_service_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing Job utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&job_mutex);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create global tag mutex!");
        return rc;
    }

    /* create the vectors in which we will have the jobs stored. */
    nonblocking_jobs = vector_create(MIN_NONBLOCKING_JOBS, MIN_NONBLOCKING_JOBS/4);
    if(!nonblocking_jobs ) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of nonblocking jobs!");
        return PLCTAG_ERR_CREATE;
    }

    blocking_jobs = vector_create(MIN_BLOCKING_JOBS, MIN_BLOCKING_JOBS/4);
    if(!blocking_jobs ) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of blocking jobs!");
        return PLCTAG_ERR_CREATE;
    }

    /* FIXME - this should be some sort of dynamic number */
    nonblocking_threads = vector_create(MIN_NONBLOCKING_THREADS,MIN_NONBLOCKING_THREADS/4);
    if(!nonblocking_threads) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of nonblocking job threads!");
        return PLCTAG_ERR_CREATE;
    }

    blocking_threads = vector_create(MIN_BLOCKING_THREADS,MIN_BLOCKING_THREADS/4);
    if(!blocking_threads) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of blocking job threads!");
        return PLCTAG_ERR_CREATE;
    }

    critical_block(job_mutex) {
        /* create the nonblocking job threads. */
        for(int i=0; i < MIN_NONBLOCKING_THREADS; i++) {
            thread_p t = NULL;

            rc = thread_create((thread_p*)&t, job_runner, 32*1024, nonblocking_jobs);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_INFO,"Unable to create job runner thread!");
                rc = PLCTAG_ERR_CREATE;
                break;
            }

            vector_put(nonblocking_threads,vector_length(nonblocking_threads), t);
        }

        /* create the blocking job threads. */
        for(int i=0; i < MIN_BLOCKING_THREADS; i++) {
            thread_p t = NULL;

            rc = thread_create((thread_p*)&t, job_runner, 32*1024, blocking_jobs);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_INFO,"Unable to create job runner thread!");
                rc = PLCTAG_ERR_CREATE;
                break;
            }

            vector_put(blocking_threads,vector_length(blocking_threads), t);
        }
    }

    pdebug(DEBUG_INFO,"Finished initializing Job utility.");

    return rc;
}


/*
 * called when the whole program is going to terminate.
 */
void job_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Releasing Job service resources.");

    /*
     * kill all the threads that are remaining.   The whole program is
     * going down so it does not matter if they do not clean up.
     */

    pdebug(DEBUG_INFO,"Terminating job threads.");

    library_terminating = 1;

    /* get the non-blocking threads first. */
    for(int i=0; i < vector_length(nonblocking_threads); i++) {
        thread_join(vector_get(nonblocking_threads, i));
    }

    /* now the blocking threads. */
    for(int i=0; i < vector_length(blocking_threads); i++) {
        thread_join(vector_get(blocking_threads, i));
    }

    pdebug(DEBUG_INFO,"Freeing thread lists.");
    vector_destroy(nonblocking_threads);
    vector_destroy(blocking_threads);

    pdebug(DEBUG_INFO,"Freeing job lists.");
    /* free the vector of jobs */
    vector_destroy(nonblocking_jobs);
    vector_destroy(blocking_jobs);

    pdebug(DEBUG_INFO,"Freeing job mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&job_mutex);

    pdebug(DEBUG_INFO,"Finished tearing down Job utility resources.");
}


