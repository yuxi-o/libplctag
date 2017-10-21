/***************************************************************************
 *   Copyright (C) 2017 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library/Lesser General Public License as*
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

/*
 * Implement a kind of thread pool for executing jobs that might block.
 */

#ifndef __UTIL__JOB_H__
#define __UTIL__JOB_H__

#include <platform.h>

typedef enum { JOB_DONE, JOB_RERUN } job_exit_type;


typedef struct job_t *job_p;


typedef job_exit_type (*job_function)(int arg_count, void **arg);

extern job_p job_create(job_function func, int is_blocking, int num_args, ...);


/* needed for set up of the PT service */
extern int job_service_init(void);
extern void job_service_teardown(void);



#endif
