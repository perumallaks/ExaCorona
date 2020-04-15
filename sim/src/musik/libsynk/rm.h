/*----------------------------------------------------------------------------*/
/* Reduction management interface.                                            */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#ifndef __REDUCE_H
#define __REDUCE_H
/*----------------------------------------------------------------------------*/
/* Reduction Management (RM) Kit.                                             */
/* Performs distributed reduction of all v_i at processor i:                  */
/*                                                                            */
/*     M = v_1 $ v_2 $ v_3 $ v_4 $ ... $ v_n                                  */
/*                                                                            */
/* in such a way that every processor obtains the reduced value "M" at the    */
/* end of the reduction operation.                                            */
/*                                                                            */
/* Here, '$' is any operator that is both commutative and associative.        */
/*                                                                            */
/* The RM kit guarantees that the '$' operator is applied to every v_i exactly*/
/* once in the course of the distributed reduction operation.  In other words,*/
/* every value is reduced once, and no value is reduced more than once.       */
/*                                                                            */
/* However, the reduction is free to permute the order of v_i.  In other      */
/* words, the order of operands for the operators given in the original       */
/* expression may not be preserved, and in fact, the commutativity and        */
/* associativity of the operator may be exploited for runtime efficiency.     */
/*                                                                            */
/* The state chart for legal sequence of calls to RM is as follows:           */
/*                                                                            */
/*----------------------------------------------------------------------------*/
/*                                                                            */
/*                            UNDEFINED                                       */
/*                                |                                           */
/*                                | rm_register()                             */
/*                                V                                           */
/*                            REGISTERED                                      */
/*                                |                                           */
/*                                | rm_init()                                 */
/*                                V                                           */
/*            +---+----------> ACTIVE <--------+                              */
/*            |   ^               | |          | rm_receive_start()           */
/*            |   |               | |          | rm_receive_value()           */
/*            |   |               | +----------+                              */
/*            |   |               |                                           */
/*            |   |blocked        | rm_resume()                               */
/*            |   |               V                                           */
/*            |   |           PROCESSING..............> start/value msgs sent */
/*            |   |               |                                           */
/*  rm_init() |   +---------------+                                           */
/*            |                   |                                           */
/*            |                   | done                                      */
/*            |                   V                                           */
/*            |                 DONE                                          */
/*            |                   |                                           */
/*            +-------------------+                                           */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>

/*----------------------------------------------------------------------------*/
typedef enum
{
    RM_UNDEFINED,
    RM_REGISTERED,
    RM_ACTIVE,
    RM_PROCESSING,
    RM_DONE
} RMStatus;

/*----------------------------------------------------------------------------*/
/* Reduction management utilities.                                            */
/*----------------------------------------------------------------------------*/
typedef enum
{
    RM_SCHEDULE_UNDEFINED,
    RM_SCHEDULE_STAR,
    RM_SCHEDULE_ALL_TO_ALL,
    RM_SCHEDULE_BUTTERFLY,
    RM_SCHEDULE_GROUPED_BFLY,
    RM_SCHEDULE_PHASE_BFLY /*Added by Alfred Park <park@cc.gatech.edu> 09Dec02*/
} RMScheduleType;

/*----------------------------------------------------------------------------*/
/* Abstract type of reduced value.                                            */
/*----------------------------------------------------------------------------*/
struct RVALUE_TYPE_STRUCT; /*Forward declaration*/
typedef struct RVALUE_TYPE_STRUCT RVALUE_TYPE;
typedef RVALUE_TYPE *(*RVALUE_NEW_FUNC)( void );/*alloc space for a new rvalue*/
typedef void (*RVALUE_DELETE_FUNC)(RVALUE_TYPE *a);/*free previously allocated*/
typedef void (*RVALUE_INIT_FUNC)(RVALUE_TYPE *a); /*such that, (*a) $ v == v */
typedef void (*RVALUE_ASSIGN_FUNC)(RVALUE_TYPE *a, RVALUE_TYPE *b);/*(*a)=(*b)*/
typedef void (*RVALUE_REDUCE_FUNC)(RVALUE_TYPE *a,RVALUE_TYPE *b);/*(*a)$=(*b)*/
typedef void (*RVALUE_PRINT_FUNC)(FILE *fp, RVALUE_TYPE *a);
typedef struct
{
    RVALUE_NEW_FUNC    rv_new;
    RVALUE_DELETE_FUNC rv_delete;
    RVALUE_INIT_FUNC   rv_init;
    RVALUE_ASSIGN_FUNC rv_assign;
    RVALUE_REDUCE_FUNC rv_reduce;
    RVALUE_PRINT_FUNC  rv_print;
} RVALUE_CLASS;

/*----------------------------------------------------------------------------*/
typedef void *RMUserHandle;

/*----------------------------------------------------------------------------*/
typedef void (*RM_SEND_START_MSG)( RMUserHandle usr, void *closure,
				   int from_pe, int to_pe, int to_slot );
typedef void (*RM_SEND_VALUE_MSG)( RMUserHandle usr, void *closure,
                                   int from_pe, int to_pe, RVALUE_TYPE *v,
                                   int to_slot );

/*----------------------------------------------------------------------------*/
/*  RM user invokes this to register for RM service.                          */
/*  RM can be used by several users; each user gets a unique handle.          */
/*----------------------------------------------------------------------------*/
RMUserHandle rm_register
(
    int N,                 /* Number of processors */
    int myid,              /* ID of this processor, 0 <= myid < N */
    int maxn,              /* Max #processors (assuming dynamic join/leave) */
    RVALUE_CLASS *rv_class /* Class describing the type of reduced values */
);

/*----------------------------------------------------------------------------*/
/*  RM user invokes this after registering for the first time, or to reuse    */
/*  this handle after a reduction is completed.                               */
/*----------------------------------------------------------------------------*/
void rm_init( RMUserHandle usr, RMScheduleType new_schedule );

/*----------------------------------------------------------------------------*/
/*  RM is being told that a processor has sent a start message to this proc.  */
/*----------------------------------------------------------------------------*/
void rm_receive_start( RMUserHandle usr, int from_pe );

/*----------------------------------------------------------------------------*/
/*  RM is being told that a processor has sent its current value to this proc.*/
/*----------------------------------------------------------------------------*/
void rm_receive_value( RMUserHandle usr, int from_pe, RVALUE_TYPE *recd_value );

/*----------------------------------------------------------------------------*/
/*  RM is told to start or continue processing the current reduction.         */
/*  Returns 1 if reduction is completed, 0 if more processing is left.        */
/*  The partial result computed so far is returned in the parameter.          */
/*  If the reduction is completed, the returned result is the final value.    */
/*                                                                            */
/*  During processing, RM can send start/value messages to other processors.  */
/*  The corresponding helper functions passed as parameters will be used      */
/*  by RM to perform the message sends.  RM passes the closure as argument    */
/*  to the helper functions.                                                  */
/*----------------------------------------------------------------------------*/
int rm_resume
(
    RMUserHandle usr,
    RVALUE_TYPE *result_so_far,
    RM_SEND_START_MSG send_start_function,
    RM_SEND_VALUE_MSG send_value_function,
    void *closure
);

/*----------------------------------------------------------------------------*/
/* Returns the current status of the given reduction.                         */
/* Additionally, returns the current (partial) reduced value.                 */
/* The reduced value is the final value iff the status is RM_DONE.            */
/*----------------------------------------------------------------------------*/
RMStatus rm_get_status( RMUserHandle usr, RVALUE_TYPE *rvalue );



/*----------------------------------------------------------------------------*/
/* Returns the receive and send communication schedule for this processor     */
/*----------------------------------------------------------------------------*/
int compute_schedule
(
    RMScheduleType pattern,/* What communication pattern to use */
    int i,      /* ID of this processor */
    int N,      /* Total number of processors */
    int mx,     /* Maximum number of actions */
    int *ssize, /* Returned: Number of processors in the schedule */
    int ids[],  /* Returned: Sequence of processor IDs in the schedule */
    int rs[],   /* Returned: In schedule, rs[x]=T if ids[x] sends to i; else F*/
    int js[],   /* Returned: if rs[x]=F, then js[x]=F; else js[x]=T iff ids[x]
		   must be jumpstarted (to save time)by sending it a start msg*/
    int ro[],   /* Returned: if rs[x]=F, then ro[x]=F;
			     else if rs[x]=T, then
				 ro[x]=T if recd value must be reduced w/ curr
				 ro[x]=F if recd value must overwrite curr */
    int rt[]    /* Returned: if rs[x]=T, then is ordinal pos of id in self acts,
                             else is ordinal pos of self in id's acts*/
);

/*----------------------------------------------------------------------------*/
void print_schedule
(
    FILE *fp,
    int i,
    int N,
    int ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[],
    int rt[]
);

/*----------------------------------------------------------------------------*/
#endif /*__REDUCE_H*/
