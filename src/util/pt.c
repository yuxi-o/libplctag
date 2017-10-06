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
#include <util/pt.h>



typedef enum { WAITING, RUNNING, DEAD } pt_state;

struct pt_entry_t {
    pt_state state;
    int pt_line;
    rc_ref arg_ref;
    pt_func func;
};

typedef struct pt_entry_t *pt_entry_p;

static volatile vector_ref pt_list = {0,};

/* global PT mutex protecting the PT list. */
static volatile mutex_p global_pt_mut = NULL;

/* pt runner thread */
static volatile thread_p pt_thread = NULL;
#ifdef _WIN32
DWORD __stdcall pt_runner(LPVOID not_used);
#else
void* pt_runner(void* not_used);
#endif


static volatile int library_terminating = 0;



static void pt_entry_destroy(void *);


/*
 * Library initialization point for protothreads.
 */


int pt_service_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing Protothread utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&global_pt_mut);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create global tag mutex!");
        return rc;
    }

    /* create the vector in which we will have the PTs stored. */
    pt_list = vector_create(100, 50);
    if(!rc_deref(pt_list)) {
        pdebug(DEBUG_ERROR,"Unable to allocate a vector!");
        return PLCTAG_ERR_CREATE;
    }

    /* create the background PT runner thread */
    rc = thread_create((thread_p*)&pt_thread, pt_runner, 32*1024, NULL);
    if(rc != PLCTAG_STATUS_OK) {
        rc_release(pt_list);
        pdebug(DEBUG_INFO,"Unable to create PT runner thread!");
        return rc;
    }

    pdebug(DEBUG_INFO,"Finished initializing Protothread utility.");

    return rc;
}


/*
 * called when the whole program is going to terminate.
 */
void pt_service_teardown(void)
{
    pdebug(DEBUG_INFO,"Releasing global Protothread resources.");

    pdebug(DEBUG_INFO,"Terminating PT thread.");
    /* kill the IO thread first. */
    library_terminating = 1;

    /* wait for the thread to die */
    thread_join(pt_thread);
    thread_destroy((thread_p*)&pt_thread);

    pdebug(DEBUG_INFO,"Freeing PT list.");
    /* free the vector of PTs */
    rc_release(pt_list);

    pdebug(DEBUG_INFO,"Freeing global PT mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&global_pt_mut);

    pdebug(DEBUG_INFO,"Finished tearing down Protothread utility resources.");
}



pt_ref pt_create(pt_func func, rc_ref arg_ref)
{
    pt_ref result = RC_PT_NULL;
    pt_entry_p entry = mem_alloc(sizeof(struct pt_entry_t));

    if(!entry) {
        pdebug(DEBUG_ERROR,"Cannot allocate new pt entry!");
        return result;
    }

    entry->state = WAITING;
    entry->arg_ref = arg_ref;
    entry->func = func;

    result = RC_CAST(pt_ref, rc_make_ref(entry, pt_entry_destroy));

    if(!rc_deref(result)) {
        mem_free(entry);
        return result;
    }

    critical_block(global_pt_mut) {
        /* just insert at the end.   Insert a weak reference. */
        if(vector_put(pt_list, vector_length(pt_list), rc_weak(result)) != PLCTAG_STATUS_OK) {
            pdebug(DEBUG_ERROR,"Unable to insert new protothread into list!");
            rc_release(result);
            result = RC_PT_NULL;
        }
    }

    return result;
}




#ifdef _WIN32
DWORD __stdcall pt_runner(LPVOID not_used)
#else
void* pt_runner(void* not_used)
#endif
{
    pdebug(DEBUG_INFO,"PT Runner starting with arg %p",not_used);

    while(!library_terminating) {
        /* scan over all the entries looking for one that is valid and not running. */
        int index = 0;
        int done = 0;

        do {
            pt_entry_p pt = NULL;

            done = 1;

            /* find a protothread that is ready to run. */
            critical_block(global_pt_mut) {
                for(int i=index; i < vector_length(pt_list); i++) {
                    pt_ref pt_tmp = RC_CAST(pt_ref,vector_get(pt_list,i));

                    /* if it is dead or an invalid reference, then remove it. */
                    if(!(pt = rc_deref(pt_tmp)) || pt->state == DEAD) {
                        vector_remove(pt_list, i);
                        rc_release(pt_tmp);
                        continue;
                    }

                    /* is it ready to run? */
                    if(pt->state == WAITING) {
                        /* yes, so mark it as running to prevent other threads from changing it. */
                        pt->state = RUNNING;
                        index = i;
                        done = 0;
                        break;
                    }
                }
            }

            if(!done) {
                if(pt->func(&pt->pt_line, pt->arg_ref) == PT_TERMINATE) {
                    pt->state = DEAD;
                }

                /* do not rerun the PT again. */
                index++;
            }
        } while(!done);

        /* reschedule the CPU. */
        sleep_ms(1);
    }

    pdebug(DEBUG_DETAIL,"PT Runner thread function exiting.");

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}




void pt_entry_destroy(void *arg)
{
    pt_entry_p entry = arg;

    if(!arg) {
        pdebug(DEBUG_WARN,"Null pointer passed in!");
        return;
    }

    rc_release(entry->arg_ref);
}
