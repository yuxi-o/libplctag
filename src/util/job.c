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


#define MIN_JOBS (100)
#define MIN_JOB_THREADS (20)

typedef enum { WAITING, RUNNING, DEAD } job_status;

struct job_t {
    job_status status;
    int arg_count;
    void** args;
    job_function func;
};


static volatile vector_p job_list = NULL;
static volatile vector_p thread_list = NULL;

/* job mutex protecting the job and thread lists. */
static volatile mutex_p job_mutex = NULL;

/* job runner thread */
THREAD_FUNC(job_runner);
THREAD_FUNC(blocking_runner);


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

    entry->status = RUNNING;
    entry->func = func;
    entry->arg_count = arg_count;
    entry->args = (void **)(entry+1); /* point past the PT struct. */

    /* fill in the extra args. */
    va_start(va, arg_count);
    for(int i=0; i < arg_count; i++) {
        entry->args[i] = va_arg(va, void *);
    }
    va_end(va);

    if(is_blocking) {
        thread_p t;
        status = thread_create((thread_p*)&t, blocking_runner, 32*1024, entry);

        if(status != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_ERROR,"Unable to create new blocking job runner!");
        } else {
            /* FIXME - catch return code! */
            status = vector_put(thread_list, vector_length(thread_list), t);
            if(status != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"Unable to insert new thread into list!");
                thread_kill(t);
                thread_destroy(&t);
            }
        }
    } else {
        critical_block(job_mutex) {
            /* just insert at the end. */
            if((status = vector_put(job_list, vector_length(job_list), entry)) != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_ERROR,"Unable to insert new job into list!");
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
    (void)arg;

    pdebug(DEBUG_INFO,"Job Runner starting.");

    thread_detach();

    while(1) {
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
                }

                /* do not rerun the job again. */
                index++;

                /* release the reference. */
                rc_dec(job);
            }
        } while(!done);

        /* reschedule the CPU. */
        sleep_ms(1);
    }

    pdebug(DEBUG_DETAIL,"Job Runner thread function exiting.");

    THREAD_RETURN(0);
}


THREAD_FUNC(blocking_runner)
{
    job_p job = arg;

    thread_detach();

    if(!job) {
        pdebug(DEBUG_WARN,"Job is NULL!");
        thread_stop();
    }

    /* run it until done */
    while(job->func(job->arg_count, job->args) == JOB_RERUN) {
        ; /* just keep running it. */
    }

    rc_dec(job);

    /* FIXME - this may leak! */

    THREAD_RETURN(0);
}



void job_destroy(void *job_arg, int arg_count, void **args)
{
    job_p entry = job_arg;

    (void)arg_count;
    (void)args;

    if(arg_count !=0 || !entry) {
        pdebug(DEBUG_WARN,"Null pointer passed in!");
        return;
    }

    pdebug(DEBUG_INFO,"Destroying job");

    /* remove the entry from the list. */
    critical_block(job_mutex) {
        for(int i=0; i < vector_length(job_list); i++) {
            job_p tmp = vector_get(job_list, i);

            if(tmp == entry) {
                vector_remove(job_list, i);
                break;
            }
        }
    }

    /*
     * the args should be cleaned up by the caller, either by a
     * registered clean up function or within the PT itself.
     * The args array is actually allocated along with the PT struct
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

    /* create the vector in which we will have the jobs stored. */
    job_list = vector_create(MIN_JOBS, MIN_JOBS/4);
    if(!job_list ) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of jobs!");
        return PLCTAG_ERR_CREATE;
    }

    /* create the background PT runner thread */
    /* FIXME - this should be some sort of dynamic number */
    thread_list = vector_create(MIN_JOB_THREADS,MIN_JOB_THREADS/4);
    if(!thread_list) {
        pdebug(DEBUG_ERROR,"Unable to allocate vector of job threads!");
        return PLCTAG_ERR_CREATE;
    }

    critical_block(job_mutex) {
        for(int i=0; i < MIN_JOB_THREADS; i++) {
            thread_p t = NULL;

            rc = thread_create((thread_p*)&t, job_runner, 32*1024, NULL);
            if(rc != PLCTAG_STATUS_OK) {
                pdebug(DEBUG_INFO,"Unable to create job runner thread!");
                rc = PLCTAG_ERR_CREATE;
                break;
            }

            vector_put(thread_list,vector_length(thread_list), t);
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
    pdebug(DEBUG_INFO,"Releasing global Protothread resources.");


    /*
     * kill all the threads that are remaining.   The whole program is
     * going down so it does not matter if they do not clean up.
     */

    pdebug(DEBUG_INFO,"Terminating job threads.");

    critical_block(job_mutex) {
        for(int i=0; i< vector_length(thread_list); i++) {
            thread_p t = vector_get(thread_list, i);
            if(t) {
                thread_kill(t);
                thread_destroy(&t);
            }
        }
    }

    pdebug(DEBUG_INFO,"Freeing thread list.");
    vector_destroy(thread_list);

    pdebug(DEBUG_INFO,"Freeing job list.");
    /* free the vector of PTs */
    vector_destroy(job_list);

    pdebug(DEBUG_INFO,"Freeing job mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&job_mutex);

    pdebug(DEBUG_INFO,"Finished tearing down Job utility resources.");
}


