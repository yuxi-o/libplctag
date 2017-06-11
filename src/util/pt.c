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
#include <util/pt.h>


struct pt_entry {
    struct pt_entry *next;
    void *args;
    void *ctx;
    pt_func func;
};


static volatile struct pt_entry *pt_list = NULL;

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



/*
 * Library initialization point for protothreads.
 */


int pt_init(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO,"Initializing Protothread utility.");

    /* this is a mutex used to synchronize most activities in this protocol */
    rc = mutex_create((mutex_p*)&global_pt_mut);

    if (rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create global tag mutex!");
        return rc;
    }

    /* create the background PT runner thread */
    rc = thread_create((thread_p*)&pt_thread, pt_runner, 32*1024, NULL);

    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_INFO,"Unable to create PT runner thread!");
        return rc;
    }


    pdebug(DEBUG_INFO,"Finished initializing Protothread utility.");

    return rc;
}


/*
 * called when the whole program is going to terminate.
 */
void pt_teardown(void)
{
    pdebug(DEBUG_INFO,"Releasing global Protothread resources.");

    pdebug(DEBUG_INFO,"Terminating PT thread.");
    /* kill the IO thread first. */
    library_terminating = 1;

    /* wait for the thread to die */
    thread_join(pt_thread);
    thread_destroy((thread_p*)&pt_thread);

    pdebug(DEBUG_INFO,"Freeing global PT mutex.");
    /* clean up the mutex */
    mutex_destroy((mutex_p*)&global_pt_mut);

    pdebug(DEBUG_INFO,"Finished tearing down Protothread utility resources.");
}



int pt_schedule(pt_func func, void *args)
{
    struct pt_entry *entry = mem_alloc(sizeof(struct pt_entry));

    if(!entry) {
        pdebug(DEBUG_ERROR,"Cannot allocate new pt entry!");
        return PLCTAG_ERR_NO_MEM;
    }

    entry->func = func;
    entry->args = args;
    entry->next = NULL;

    critical_block(global_pt_mut) {
        /* insert at the end of the list */
        struct pt_entry **walker = (struct pt_entry **)&pt_list;

        while(*walker) {
            walker = &(*walker)->next;
        }

        /* now we are at the end */
        *walker = entry;
    }

    return PLCTAG_STATUS_OK;
}


/*
 * Removes a PT from the list.  Returns the PT if found.
 * If not found, returns NULL.
 */

static struct pt_entry *pt_remove(struct pt_entry *entry)
{
    struct pt_entry **walker = (struct pt_entry **)&pt_list;  /* safe, pt_list is global and cannot move */
    int found = 0;

    critical_block(global_pt_mut) {
        /*
         * since the list could have mutated since we started
         * walking it, we need to find the entry we want to
         * delete.
         */
        while(*walker && *walker != entry) {
            walker = &(*walker)->next;
        }

        /* should not need this check, but... */
        if(*walker == entry) {
            *walker = entry->next;
            found = 1;
        }
    }

    if(found) {
        return entry;
    } else {
        return NULL;
    }
}


static struct pt_entry *pt_next(struct pt_entry *entry)
{
    struct pt_entry *result = NULL;

    critical_block(global_pt_mut) {
        if(entry) {
            result = entry->next;
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
        struct pt_entry *curr = NULL;

        critical_block(global_pt_mut) {
            curr = (struct pt_entry *)pt_list;
        }

        pdebug(DEBUG_SPEW,"Starting to walk PT list.");

        while(curr) {
            struct pt_entry *next = NULL;

            pdebug(DEBUG_SPEW,"Running PT %p",curr);

            if(curr->func(curr->args,&curr->ctx) == PT_TERMINATE) {
                struct pt_entry *tmp = NULL;

                next = pt_next(curr);

                tmp = pt_remove(curr);

                if(tmp) {
                    if(curr->args) mem_free(curr->args);
                    if(curr->ctx) mem_free(curr->ctx);
                    mem_free(curr);
                }

                curr = next;
            } else {
                curr = pt_next(curr);
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

