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

struct pt_entry {
	pt_state state;
	int pt_pc;
    void *args;
    pt_func func;
};


static volatile vector_p pt_list = NULL;

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
    pt_list = vector_create(100, 50, RC_WEAK_REF); /* make it a weak list */
    if(!pt_list) {
		pdebug(DEBUG_ERROR,"Unable to allocate a vector!");
		return PLCTAG_ERR_CREATE;
	}

    /* create the background PT runner thread */
    rc = thread_create((thread_p*)&pt_thread, pt_runner, 32*1024, NULL);
    if(rc != PLCTAG_STATUS_OK) {
		rc_dec(pt_list);
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
    rc_dec(pt_list);

    pdebug(DEBUG_INFO,"Freeing global PT mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&global_pt_mut);

    pdebug(DEBUG_INFO,"Finished tearing down Protothread utility resources.");
}



pt pt_create(pt_func func, void *args)
{
    struct pt_entry *entry = rc_alloc(sizeof(struct pt_entry), pt_entry_destroy);

    if(!entry) {
        pdebug(DEBUG_ERROR,"Cannot allocate new pt entry!");
        return NULL;
    }

	entry->state = WAITING;
    entry->args = rc_inc(args);
    entry->func = func;

    critical_block(global_pt_mut) {
		/*
		 * Note: The "<=" is deliberate here.   If we cannot
		 * find an empty slot earlier, we will go one past the
		 * end of the current vector content and place the new
		 * element there.
		 *
		 * FIXME - check if this does not succeed.
		 */
		for(int i=0; i <= vector_length(pt_list); i++) {
			/* this takes a strong reference */
			struct pt_entry *tmp = vector_get(pt_list, i);

			if(!tmp) {
				if(vector_put(pt_list, i, entry) != PLCTAG_STATUS_OK) {
					pdebug(DEBUG_ERROR,"Unable to add new PT entry!");
					entry = rc_dec(entry);
				}
				break;
			} else {
				/* release the reference */
				rc_dec(tmp);
			}
		}
    }

    return entry;
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
		for(int index=0; index < vector_length(pt_list); index++) {
			pt current_pt = NULL;
			
			critical_block(global_pt_mut) {
				/* this gets a strong reference if the reference is valid. */
				current_pt = vector_get(pt_list, index);

				/* did we get a valid ref? */
				if(current_pt) {
					/* is it ready to run? */
					if(current_pt->state == WAITING) {
						/* yes, so mark it as running to prevent other threads from changing it. */
						current_pt->state = RUNNING;
					} else {
						/* no? is it dead? */
						if(current_pt->state == DEAD) {
							/* clean it up */
							vector_put(pt_list, index, NULL);
						}

						/* we are done with this one, it is not available for executing. */
						current_pt = rc_dec(current_pt);
					}
				}
			}

			/* if we have a current PT, then it is valid, we have a strong reference to
			 * it and we are the ones that changed it to RUNNING thus locking it from other
			 * threads.
			 */

			if(current_pt) {
				if(current_pt->func(&current_pt->pt_pc, current_pt->args) == PT_TERMINATE) {
					current_pt->state = DEAD;
					/*
					 * Do not clean this up here.   Do that in the mutex-protected block
					 * above on the next run.
					 */
				}

				/* we are done with the reference. */
				current_pt = rc_dec(current_pt);
			}
		}

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
	struct pt_entry *entry = arg;

	if(!arg) {
		pdebug(DEBUG_WARN,"Null pointer passed in!");
		return;
	}

	entry->args = rc_dec(entry->args);
}
