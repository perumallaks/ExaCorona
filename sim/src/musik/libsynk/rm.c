/*----------------------------------------------------------------------------*/
/* Reduction management functions.                                            */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mycompat.h"
#include "rm.h"

#ifndef FALSE
  #define TRUE 1
  #define FALSE 0
#endif

/*----------------------------------------------------------------------------*/
static int dbg = 0;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int must_recv;    /* TRUE if this processor must receive; FALSE if it */
                      /* must send -- at this point in the schedule */

    /* Interpretation & use of the following fields depend on type of action */

    int id;           /* processor ID (e.g. source or destination) */
    int heard;        /* Do we know if this processor started its processing? */
    int procd;        /* Recd(sent) from(to) this processor? */
    RVALUE_TYPE *recd_rvalue; /* Recd(sent) value from(to) this processor */
    int jumpstart;    /* If must_recv, should this processor be jumpstarted? */
    int reduce_or_overwrite; /* How to use recd_rvalue: to reduce or overwrite*/
    int rslot;        /* if must_recv: rslot is ordinal pos of id in self acts,
                         else: rslot is ordinal pos of self in id's acts*/
} RMAction;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int N;
    int myid;
    int maxn;
    RVALUE_CLASS rv_class;
} RMUserInfo;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int dbg;          /*Debugging on/off?*/
    RMStatus status;
    int started;      /* Has a reduction started? */
    int done;         /* Has this reduction completed? */

    RMScheduleType old_schedule;/*Currently used schedule type*/
    RMScheduleType new_schedule;/*Change to this schedule type on next rm_init*/

    RVALUE_TYPE *min_so_far;/* Minimum reduced value so far at this processor */
    int do_jumpstarting; /*Should jumpstart messages be sent during reduction?*/

    #define MAXACTIONS 1000 /*CUSTOMIZE*/
    int index_of_blocked_action;
    int nactions;
    RMAction actions[MAXACTIONS];
} RMState;

/*----------------------------------------------------------------------------*/
typedef struct
{
    RMUserInfo info;
    RMState state;
} RMUser;

/*----------------------------------------------------------------------------*/
static void rm_state_alloc( RMState *st, RVALUE_CLASS *rvc, int maxn )
{
    int k = 0;
    st->min_so_far = rvc->rv_new();
    for( k = 0; k < MAXACTIONS; k++ )
    {
        RMAction *act = &st->actions[k];
	act->recd_rvalue = rvc->rv_new();
    }
}

/*----------------------------------------------------------------------------*/
static void rm_state_init( RMState *st, RVALUE_CLASS *rvc, int i, int N )
{
    st->started = FALSE;
    st->done = FALSE;
    st->index_of_blocked_action = -1;

    rvc->rv_init( st->min_so_far );

    /* Compute/Update the communication pattern/schedule for this processor */
    if( st->old_schedule == st->new_schedule )
    {
        int k = 0;

        for( k = 0; k < st->nactions; k++ )
        {
            RMAction *act = &st->actions[k];
            act->heard = FALSE;
            act->procd = FALSE;
	    rvc->rv_init( act->recd_rvalue );
        }
    }
    else
    {
        int k = 0;
	int ids[MAXACTIONS], rs[MAXACTIONS], js[MAXACTIONS], ro[MAXACTIONS];
	int rt[MAXACTIONS];

        st->nactions = 0;
        compute_schedule( st->new_schedule,
	    i, N, MAXACTIONS, &st->nactions, ids, rs, js, ro, rt );
#if !NODEBUG
if(st->dbg>1){if(i<8||st->dbg>=2){static int x=0; if(x++==0){print_schedule( stdout, i, N, st->nactions, ids, rs, js, ro, rt);fflush(stdout);}}}
#endif

        for( k = 0; k < st->nactions; k++ )
        {
            RMAction *act = &st->actions[k];
            act->id = ids[k];
            act->must_recv = rs[k];
            act->heard = FALSE;
            act->procd = FALSE;
	    rvc->rv_init( act->recd_rvalue );
            act->jumpstart = js[k];
            act->reduce_or_overwrite = ro[k];
            act->rslot = rt[k];
	    assert( !( rs[k] == FALSE && ro[k] == TRUE ) );
        }
	st->old_schedule = st->new_schedule;
    }
}

/*----------------------------------------------------------------------------*/
static int rm_find_proc( RMUser *usr, int pe, int rs, int first_action )
{
    int k = 0;
    RMState *st = &usr->state;

    for( k = first_action; k < st->nactions; k++ )
    {
        RMAction *a = &st->actions[k];
        assert( 0 <= a->id && a->id < usr->info.N );
	if( a->id == pe && a->must_recv == rs ) break;
    }

    if( k >= st->nactions ) k = -1;

    return k;
}

/*----------------------------------------------------------------------------*/
/* A processor has explicitly or implicitly asked us to start.                */
/*----------------------------------------------------------------------------*/
static void rm_do_start(RMUser *usr, RM_SEND_START_MSG start_func,void *closure)
{
    RMUserHandle uh = (RMUserHandle)usr;
    RMState *st = &usr->state;

    if( !st->started )
    {
	int k = 0;

        st->started = TRUE;

	/* Send out start messages to all from whom this */
	/* processor must hear (later) in the schedule */
	for( k = 0; st->do_jumpstarting && k < st->nactions; k++ )
	{
	    RMAction *a = &st->actions[k];
	    if( a->must_recv &&
		a->id != usr->info.myid /*No need to start self*/ )
	    {
		if( a->heard )
		{
		    /* Don't need to start a processor */
		    /* from whom we have already heard */
		    /* (it has already started!), so, do nothing */
		}
		else if( a->jumpstart )
		{
	            start_func( uh, closure, usr->info.myid, a->id, a->rslot );

		    /* Send to another processor in the "other segment" of */
		    /* the butterfly pattern, to rapidly propagate the start */
		    if(0)
		    {
		        int another = (a->id + usr->info.N/4) % usr->info.N;
			assert( 0 <= another && another < usr->info.N );
		        if( another != usr->info.myid && another != a->id )
	                   start_func(uh, closure, usr->info.myid, another, -1);
		    }
		}
		else
		{
		    /* No need to send a jumpstart message to this processor */
		    /* Based on the schedule, the processor is either known  */
		    /* to have started, or will start implicity when it      */
		    /* receives a value message from us (no need to waste    */
		    /* an extra start message)!                              */
		}
	    }
	}
    }
}

/*----------------------------------------------------------------------------*/
static void rm_abort( RMUserHandle uh )
{
    RMUser *usr = (RMUser *)uh;
    RMState *st = &usr->state;

    /*Nothing special for now, since this function is not accessible by user*/
    st->status = RM_DONE;
}

/*----------------------------------------------------------------------------*/
/* Register a user.                                                           */
/*----------------------------------------------------------------------------*/
RMUserHandle rm_register
(
    int N,
    int myid,
    int maxn,
    RVALUE_CLASS *rv_class
)
{
    RMUser *usr = (RMUser *)malloc( sizeof( RMUser ) );
    RMState *st = &usr->state;

    usr->info.N = N; usr->info.myid = myid; usr->info.maxn = maxn;
    usr->info.rv_class = *rv_class;

    rm_state_alloc( st, rv_class, maxn );
    st->status = RM_REGISTERED;
    st->dbg = getenv("RM_DEBUG") ? atoi(getenv("RM_DEBUG")) : 0;
    dbg = st->dbg;
    st->old_schedule = RM_SCHEDULE_UNDEFINED;
    st->new_schedule = RM_SCHEDULE_GROUPED_BFLY;
    st->do_jumpstarting = FALSE;

    if( getenv("RM_DOJUMPSTART") && !strcmp(getenv("RM_DOJUMPSTART"),"true") )
    {
        st->do_jumpstarting = TRUE;
    }

if(0){if(myid==0){static int x=0;if(x++==0){printf("RM: \"do_jumpstarting\"=%s\n",st->do_jumpstarting?"yes":"no");fflush(stdout);}}}
    LSCFGLD( "RM_DEBUG", (long)st->dbg, "RM debug intensity" );
    LSCFGST( "RM_DOJUMPSTART", (st->do_jumpstarting?"true":"false"),
             "RM must send jumpstart messages?" );

    {
    char *sch=getenv("RM_SCHEDULE");
    if(!sch){}
    else if(!strcmp(sch,"A2A")) st->new_schedule = RM_SCHEDULE_ALL_TO_ALL;
    else if(!strcmp(sch,"STAR")) st->new_schedule = RM_SCHEDULE_STAR;
    else if(!strcmp(sch,"BFLY")) st->new_schedule = RM_SCHEDULE_BUTTERFLY;
    else if(!strcmp(sch,"GBFLY")) st->new_schedule = RM_SCHEDULE_GROUPED_BFLY;
    else if(!strcmp(sch,"PBFLY")) st->new_schedule = RM_SCHEDULE_PHASE_BFLY;
    else                         {printf("Bad reduction schedule\n");exit(1);}
    }

    #define STRING_RM_SCHEDULE(_st) (_st==RM_SCHEDULE_ALL_TO_ALL ? "A2A":\
				     _st==RM_SCHEDULE_STAR       ? "STAR":\
				     _st==RM_SCHEDULE_BUTTERFLY  ? "BUTTERFLY":\
				     _st==RM_SCHEDULE_GROUPED_BFLY ? "GBFLY":\
				     _st==RM_SCHEDULE_PHASE_BFLY ? "PBFLY":\
				     "UNDEFINED")
    LSCFGST( "RM_SCHEDULE", STRING_RM_SCHEDULE(st->new_schedule), "RM schedule" );

#if !NODEBUG
if(st->dbg>=1){static int x=0;if(x++==0){printf("\n%d: ** RM using schedule of type %s.\n\n", myid, STRING_RM_SCHEDULE(st->new_schedule));fflush(stdout);}}
#endif

    return (RMUserHandle)usr;
}

/*----------------------------------------------------------------------------*/
/*  Init/Re-init user.                                                        */
/*----------------------------------------------------------------------------*/
void rm_init( RMUserHandle uh, RMScheduleType new_sched )
{
    RMUser *usr = (RMUser *)uh;
    RMUserInfo *info = &usr->info;
    RMState *st = &usr->state;

    if( st->status == RM_ACTIVE ) rm_abort( uh );

    assert( st->status == RM_REGISTERED || st->status == RM_DONE );

    st->new_schedule = new_sched;

    rm_state_init( st, &info->rv_class, info->myid, info->N );
    st->status = RM_ACTIVE;
}

/*----------------------------------------------------------------------------*/
/* A processor has explicitly or implicitly asked us to start.                */
/*----------------------------------------------------------------------------*/
void rm_receive_start( RMUserHandle uh, int from_pe )
{
    RMUser *usr = (RMUser *)uh;
    RMState *st = &usr->state;
    int k = 0;

    assert( st->status == RM_ACTIVE );
    assert( 0 <= from_pe && from_pe < usr->info.N );

    k = rm_find_proc( usr, from_pe, TRUE, 0 );

    if( k < 0 )
    {
	/* It is possible that we receive a start message from a processor */
	/* which is not in the schedule, because that processor can send */
	/* to processors that are not in its schedule; see the code in this */
	/* function below */
#if !NODEBUG
if(st->dbg>=2){printf("%d: heard STARTX from %d\n",usr->info.myid,from_pe);fflush(stdout);}
#endif
    }
    else
    {
        RMAction *a = &st->actions[k];
	a->heard = TRUE;

#if !NODEBUG
if(st->dbg>=2){printf("%d: heard START from %d\n",usr->info.myid,from_pe);fflush(stdout);}
#endif
    }

    st->status = RM_ACTIVE;
}

/*----------------------------------------------------------------------------*/
/* A processor has sent its value.                                            */
/*----------------------------------------------------------------------------*/
void rm_receive_value( RMUserHandle uh, int from_pe, RVALUE_TYPE *recd_value )
{
    RMUser *usr = (RMUser *)uh;
    RMState *st = &usr->state;
    RVALUE_CLASS *rvc = &usr->info.rv_class;
    int k = -1;

    assert( st->status == RM_ACTIVE );

#if !NODEBUG
if(st->dbg>=2){printf("%d: recd new value from %d: ",usr->info.myid,from_pe);rvc->rv_print(stdout, recd_value); printf("\n");fflush(stdout);}
#endif

    do
    {
        k = rm_find_proc( usr, from_pe, TRUE, k+1 );
        assert( 0 <= k && k < st->nactions );
	{
	  RMAction *a = &st->actions[k];
	  if( !a->procd )
	  {
	    a->procd = TRUE;
	    a->heard = TRUE;
	    rvc->rv_assign( a->recd_rvalue, recd_value );
	    break;
	  }
	}
    }while(1);

    st->status = RM_ACTIVE;
}

/*----------------------------------------------------------------------------*/
int rm_resume
(
    RMUserHandle uh,
    RVALUE_TYPE *result_so_far,
    RM_SEND_START_MSG send_start_function,
    RM_SEND_VALUE_MSG send_value_function,
    void *closure
)
{
    RMUser *usr = (RMUser *)uh;
    RMState *st = &usr->state;
    RVALUE_CLASS *rvc = &usr->info.rv_class;

    int k = 0;
    int blocked = FALSE;

    assert( st->status == RM_ACTIVE );

    /*Performance optimization*/
    if( st->index_of_blocked_action >= 0 )
    {
        RMAction *a = &st->actions[st->index_of_blocked_action];
        if( a->must_recv && !a->procd )
	    return 0;
        st->index_of_blocked_action = -1;
    }

    st->status = RM_PROCESSING;

    rm_do_start( usr, send_start_function, closure );

    rvc->rv_init( st->min_so_far );

    for( k = 0; k < st->nactions; k++ )
    {
	RMAction *a = &st->actions[k];
	if( a->must_recv && !a->procd )
	{
	    blocked = TRUE;
	    st->index_of_blocked_action = k;
	    break;
	}
	else if( a->must_recv && a->procd )
	{
	    /*Already received the value; use it to reduce/overwrite min*/
	    if( a->reduce_or_overwrite )
	    {
	        rvc->rv_reduce( st->min_so_far, a->recd_rvalue ); /*reduce*/
	    }
	    else
	    {
	        rvc->rv_assign( st->min_so_far, a->recd_rvalue ); /*overwrite*/
	    }
	}
	else if( !a->must_recv && !a->procd )
	{
	    send_value_function( uh, closure,
				 usr->info.myid, a->id, st->min_so_far,
				 a->rslot );
	    a->procd = TRUE;
	}
	else
	{
	    /*Sending already done; do nothing*/
	}
    }

    st->done = !blocked;

    if( st->done )
    {
#if !NODEBUG
if(st->dbg>=2){printf("%d: DONE! Min_so_far: ", usr->info.myid);rvc->rv_print(stdout, st->min_so_far); printf("%d: \n", usr->info.myid);fflush(stdout);}
#endif

        st->status = RM_DONE;
    }
    else
    {
        st->status = RM_ACTIVE;
    }

    rvc->rv_assign( result_so_far, st->min_so_far );
    return (st->done ? 1 : 0);
}

/*----------------------------------------------------------------------------*/
RMStatus rm_get_status( RMUserHandle uh, RVALUE_TYPE *rvalue )
{
    RMUser *usr = (RMUser *)uh;
    RMState *st = &usr->state;
    RVALUE_CLASS *rvc = &usr->info.rv_class;

    rvc->rv_assign( rvalue, st->min_so_far );

    return st->status;
}

/*----------------------------------------------------------------------------*/
/* Reduction management utilities.                                            */
/* Author(s): Kalyan Perumalla <http://www.cc.gatech.edu/~kalyan> 20July2000  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Returns the highest exponent of 2 which is contained in N                  */
/*----------------------------------------------------------------------------*/
static int highest_exponent_of_2_in( int N )
{
    int k = 0, M = N;
    while( M > 1 )
    {
        k++;
        M = M/2;
    }

#if !NODEBUG
if(dbg>=2){printf("highest_exponent_of_2_in(%d) = %d\n",N,k);fflush(stdout);}
#endif

    return k;
}

/*----------------------------------------------------------------------------*/
/* Returns the number which is the highest power of 2 contained in N          */
/*----------------------------------------------------------------------------*/
static int highest_power_of_2_in( int N )
{
    int m = 1, M = N;
    while( M > 1 )
    {
        m *= 2;
        M = M/2;
    }

#if !NODEBUG
if(dbg>=2){printf("highest_power_of_2_in(%d) = %d\n",N,m);fflush(stdout);}
#endif

    return m;
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on star pattern.                  */
/*----------------------------------------------------------------------------*/
static void compute_all_to_all_schedule
(
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[]
)
{
    int ss = 0;  /* Size of the schedule returned */

    assert( 0 <= i && i < N );
    #define CHK() do{if(ss>=mx){printf("Add more actions!\n");exit(1);return;}}while(0)

    /* First receive local value from self */
    {CHK(); ids[ss]=i; rs[ss]=TRUE; js[ss]=FALSE; ro[ss]=TRUE; ss++;}

    {
	int j = 0;
	for( j = 0; j < N; j++ )
	{
	    if( j != i )
              {CHK(); ids[ss]=j; rs[ss]=FALSE; js[ss]=FALSE;
	       ro[ss]=FALSE; ss++;}/*send*/
	}
	for( j = 0; j < N; j++ )
	{
	    if( j != i )
              {CHK(); ids[ss]=j; rs[ss]=TRUE; js[ss]=FALSE;
	       ro[ss]=TRUE; ss++;} /*recv*/
	}
    }

    #undef CHK

    *ssize = ss;
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on star pattern.                  */
/*----------------------------------------------------------------------------*/
static void compute_star_schedule
(
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[]
)
{
    int ss = 0;  /* Size of the schedule returned */
    int hub = 0; /* ID of hub processor of the star pattern */

    assert( 0 <= i && i < N );
    #define CHK() do{if(ss>=mx){printf("Add more actions!\n");exit(1);return;}}while(0)

    /* First receive local value from self */
    {CHK(); ids[ss]=i; rs[ss]=TRUE; js[ss]=FALSE; ro[ss]=TRUE; ss++;}

    if( i != hub )
    {
        {CHK(); ids[ss]=hub; rs[ss]=FALSE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;} /*send to hub*/

        {CHK(); ids[ss]=hub; rs[ss]=TRUE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;}/*recv from hub*/
    }
    else
    {
	int j = 0;
	for( j = 0; j < N; j++ )
	{
	    if( j != hub )
              {CHK(); ids[ss]=j; rs[ss]=TRUE; js[ss]=FALSE;
	       ro[ss]=TRUE; ss++;} /*recv*/
	}
	for( j = 0; j < N; j++ )
	{
	    if( j != hub )
              {CHK(); ids[ss]=j; rs[ss]=FALSE; js[ss]=FALSE;
	       ro[ss]=FALSE; ss++;}/*send*/
	}
    }

    #undef CHK

    *ssize = ss;
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on butterfly pattern.             */
/*----------------------------------------------------------------------------*/
static void compute_butterfly_schedule
(
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[]
)
{
    int lgm = highest_exponent_of_2_in(N); /* Largest lgm so that (2^lgm <= N)*/
    int m = highest_power_of_2_in(N);      /* Same as 2^lgm */
    int ss = 0;                            /* Size of the schedule returned */

    assert( (1<<lgm) == m );
    assert( 0 <= i && i < N );
    #define CHK() do{if(ss>=mx){printf("Add more actions!\n");exit(1);return;}}while(0)

    /* First receive local value from self */
    {CHK(); ids[ss]=i; rs[ss]=TRUE; js[ss]=FALSE; ro[ss]=TRUE; ss++; }

    if( i >= m )
    {
        /* i is an outlier processor */

        {CHK(); ids[ss]=i - m; rs[ss]=FALSE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;} /*send*/

        {CHK(); ids[ss]=i - m; rs[ss]=TRUE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;} /*recv*/
    }
    else
    {
        int k = 0;

        /* i is a butterfly processor */
        if( i + m < N )
        {
            /*Receive from the outlier processor that i is responsible for*/

            {CHK(); ids[ss]=i + m; rs[ss]=TRUE; js[ss]=TRUE;
	     ro[ss]=TRUE; ss++;}  /*recv*/
        }

        for( k = 0; k < lgm; k++ )
        {
            int partner = i ^ (1 << k); /* i toggled in the k'th bit */

            {CHK(); ids[ss]=partner; rs[ss]=FALSE; js[ss]=FALSE;
	     ro[ss]=FALSE; ss++;}/*send*/

            {CHK(); ids[ss]=partner; rs[ss]=TRUE; js[ss]=TRUE;
	     ro[ss]=TRUE; ss++;} /*recv*/
        }

        if( i + m < N )
        {
            /*Send to the outlier processor*/

            {CHK(); ids[ss]=i + m; rs[ss]=FALSE; js[ss]=FALSE;
	     ro[ss]=FALSE; ss++;}  /*send*/
        }
    }

    #undef CHK

    *ssize = ss;
}

/*----------------------------------------------------------------------------*/
/* Returns a newly allocated duplicate of given string.                       */
/*----------------------------------------------------------------------------*/
static char *copystr( const char *orig )
{
    char *copy = (char *)malloc(strlen(orig)+1);
    strcpy( copy, orig );
    return copy;
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on a "grouped" butterfly pattern. */
/*----------------------------------------------------------------------------*/
static void get_gids( const char *orig_estr, int gids[], int N )
{
    int j = 0;
    char *estr = copystr(orig_estr);

    /*Parse off gids of the form "g1 n@g2 gk ... ".*/
    /*The n@g2 is short for g2 appearing n times. */
    for( j = 0; j < N; )
    {
	int inst = 0, ninstances = 1, k = -1;
	char *blank = 0, *at = 0;

	blank = strchr( estr, ' ' );
	if(blank) *blank = 0;

	at = strchr( estr, '@' );
	if( at )
	{
	    *at = 0;
	    ninstances = atoi(estr);
	    assert( 0 <= ninstances && j+ninstances <= N );
	    estr = at+1;
	}

	k = atoi(estr);
	assert( 0 <= k && k < N );
	for( inst = 0; inst < ninstances; inst++ )
	{
	    assert( j < N );
	    gids[j++] = k;
	}

	if(blank) estr = blank+1;
    }
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on a "grouped" butterfly pattern. */
/*----------------------------------------------------------------------------*/
static void compute_grouped_bfly_schedule
(
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[]
)
{
    #if MPI_AVAILABLE
        #define MAXN 512000 /*XXX CUSTOMIZE*/
    #elif TCP_AVAILABLE
        #define MAXN 128 /*XXX CUSTOMIZE*/
    #else
        #define MAXN 1024 /*XXX CUSTOMIZE*/
    #endif

    int ss = 0;                            /* Size of the schedule returned */
    int j = 0, gids[MAXN];
    static int checkedestr = 0;
    static char *estr = 0;

    if( !checkedestr )
    {
        checkedestr = 1;
        estr = getenv("RM_NODEGROUPS");
    }

    #if 0
    if(!estr) {compute_butterfly_schedule(i,N,mx,ssize,ids,rs,js,ro,rt); return;}

#if !NODEBUG
if(dbg>=1){printf("Using grouping info: \"%s\"\n",estr);fflush(stdout);}
#endif
        get_gids( estr, gids, N );

    #else
    if(!estr)
    {
        int gsz=12; /*XXX CUSTOMIZE*/
        static int checkedgstr = 0;
        static char *gstr = 0;

        if( !checkedgstr )
        {
            checkedgstr = 1;
            gstr = getenv("RM_GROUPSIZE");
        }

        if( gstr ) gsz = atoi(gstr);
        if(gsz<=0)gsz=1;

        for( j = 0; j < N; j++ ) gids[j]= j-(j%gsz);

if(dbg>=1){static int x = 0; if(x++==0){printf("%d: Using grouped bfly w/ grpsz %d\n",i,gsz);fflush(stdout);}}
        {static int x = 0; if(x++==0){LSCFGLD( "RM_GROUPSIZE", (long)gsz, "RM grouped butterfly group size" );}}
    }
    #endif

    assert( 0 < N && N <= MAXN );
    assert( 0 <= i && i < N );

    for( j = 0; j < N; j++ ) assert( gids[j] == gids[gids[j]] );

    #define CHK() do{if(ss>=mx){printf("Add more actions!\n");exit(1);return;}}while(0)

    if( gids[i] != i )
    {

        /* First receive local value from self */
        {CHK(); ids[ss]=i; rs[ss]=TRUE; js[ss]=FALSE; ro[ss]=TRUE; ss++; }

        /*Then, send to & recv from this processor's group leader*/

        {CHK(); ids[ss]= gids[i]; rs[ss]=FALSE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;} /*send*/

        {CHK(); ids[ss]= gids[i]; rs[ss]=TRUE; js[ss]=FALSE;
	 ro[ss]=FALSE; ss++;} /*recv*/
    }
    else
    {
        /*Receive from members of group this processor is head/leader of*/
        for( j = 0; j < N; j++ )
        {
	    if( j != i && gids[j] == i )
	    {
                {CHK(); ids[ss]=j; rs[ss]=TRUE; js[ss]=TRUE;
	         ro[ss]=TRUE; ss++;}  /*recv*/
	    }
        }

        /* Reduce with other group heads using butterfly schedule */
	{
	#define MAXHACTIONS 1000
        int nheads = 0, heads[MAXN], myheadid = -1, hssize = 0;
        int hids[MAXHACTIONS], hrs[MAXHACTIONS];
	int hjs[MAXHACTIONS], hro[MAXHACTIONS];
	for( j = 0; j < N; j++ )
	{
	    if( gids[j] == j )
	    {
	        heads[nheads] = j;
	        if( gids[j] == i ) myheadid = nheads;
		nheads++;
	    }
	}
	assert( 0 < nheads && nheads <= N );
	assert( 0 <= myheadid && myheadid < nheads );
        compute_schedule( RM_SCHEDULE_BUTTERFLY, myheadid, nheads,
	                  MAXHACTIONS, &hssize, hids, hrs, hjs, hro, NULL );
	#undef MAXHACTIONS
	for( j = 0; j < hssize; j++ )
	{
            {CHK(); ids[ss]=heads[hids[j]]; rs[ss]=hrs[j]; js[ss]=hjs[j];
	     ro[ss]=hro[j]; ss++;}
	}
	}

        /*Send to members of group this processor is head of*/
        for( j = 0; j < N; j++ )
        {
	    if( j != i && gids[j] == i )
	    {
                {CHK(); ids[ss]=j; rs[ss]=FALSE; js[ss]=FALSE;
	         ro[ss]=FALSE; ss++;}  /*send*/
	    }
        }
    }

    #undef CHK

    *ssize = ss;

    #undef MAXN
}

/*----------------------------------------------------------------------------*/
/* Returns the communication schedule based on a "phase" butterfly pattern.   */
/*----------------------------------------------------------------------------*/
static void compute_phase_bfly_schedule
(
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[]
)
{
    #define MAXN 4096

    int ss = 0;                            /* Size of the schedule returned */
    int j = 0, gids[MAXN], k, first, last, span;
    char *estr = getenv("RM_NODEGROUPS");

    int lgm, m;

    if (dbg>0) printf("%d: N: %d, i: %d\n", i, N, i);

if(!estr) {compute_butterfly_schedule(i,N,mx,ssize,ids,rs,js,ro); return;}

#if !NODEBUG
if(dbg>=1){printf("%d: Using grouping info: \"%s\"\n",i,estr);fflush(stdout);}
#endif

    assert( !!estr );
    assert( 0 < N && N <= MAXN );
    assert( 0 <= i && i < N );

    get_gids( estr, gids, N );

    for( j = 0; j < N; j++ ) assert( gids[j] == gids[gids[j]] );

    /* find grouping for this processor */
    first = -1;
    last = -1;
    for( j = 0; j < N; j++ ) {
      if( gids[j] == gids[i] ) {
	if( first == -1 )
	  first = j;
      } else if( first != -1 ) {
	break;
      }
    }
    last = j - 1;
    span = last - first + 1;
    lgm = highest_exponent_of_2_in(span);
    m = highest_power_of_2_in(span);
    
#if !NODEBUG
if (dbg>0) printf("lgm = %d, m = %d, first = %d, last = %d\n", lgm, m, first, last);
#endif

    #define CHK() do{if(ss>=mx){printf("Add more actions!\n");exit(1);return;}}while(0)


    /* First receive local value from self */
    {CHK(); ids[ss]=i; rs[ss]=TRUE; js[ss]=FALSE; ro[ss]=TRUE; ss++; }

    /* i is an outlier processor */
    if ( i - first >= m ) {
      {CHK(); ids[ss]=i - m; rs[ss]=FALSE; js[ss]=FALSE;
      ro[ss]=FALSE; ss++;} /*send*/	  
      
      {CHK(); ids[ss]=i - m; rs[ss]=TRUE; js[ss]=FALSE;
      ro[ss]=FALSE; ss++;} /*recv*/
    } else {
      /* i is a butterfly processor */
      if (i - first + m < span) {
	/* recv from outlier */
	{CHK(); ids[ss]=i + m; rs[ss]=TRUE; js[ss]=TRUE;
	ro[ss]=TRUE; ss++;}
      }
      for( k = first; k < (first + lgm); k++ ) {
	int partner = i ^ (1 << (k - first));
	
	if (dbg>0) printf("i = %d, k = %d, partner = %d\n", i,k,partner);
	
	{CHK(); ids[ss]=partner; rs[ss]=FALSE; js[ss]=FALSE;
	ro[ss]=FALSE; ss++;} /*send*/
	
	{CHK(); ids[ss]=partner; rs[ss]=TRUE; js[ss]=TRUE;
	ro[ss]=TRUE; ss++;} /*recv*/
      }
    }

    if ( i - first + m < span ) {
      /* send to outlier */
      {CHK(); ids[ss]=i + m; rs[ss]=FALSE; js[ss]=FALSE;
      ro[ss]=FALSE; ss++;} /*send*/
    }


    if( gids[i] == i ) {

        /* Reduce with other group heads using butterfly schedule */
	{
	#define MAXHACTIONS 1000
        int nheads = 0, heads[MAXN], myheadid = -1, hssize = 0;
        int hids[MAXHACTIONS], hrs[MAXHACTIONS];
	int hjs[MAXHACTIONS], hro[MAXHACTIONS];
	for( j = 0; j < N; j++ )
	{
	    if( gids[j] == j )
	    {
	        heads[nheads] = j;
	        if( gids[j] == i ) myheadid = nheads;
		nheads++;
	    }
	}
	assert( 0 < nheads && nheads <= N );
	assert( 0 <= myheadid && myheadid < nheads );
        compute_schedule( RM_SCHEDULE_BUTTERFLY, myheadid, nheads,
	                  MAXHACTIONS, &hssize, hids, hrs, hjs, hro, NULL );
	#undef MAXHACTIONS
	for( j = 1; j < hssize; j++ ) /* HACK alert: Ignore 0th position */
	{
            {CHK(); ids[ss]=heads[hids[j]]; rs[ss]=hrs[j]; js[ss]=hjs[j];
	     ro[ss]=hro[j]; ss++;}
	}
	}

        /*Send to members of group this processor is head of*/
        for( j = 0; j < N; j++ )
        {
	    if( j != i && gids[j] == i )
	    {
                {CHK(); ids[ss]=j; rs[ss]=FALSE; js[ss]=FALSE;
	         ro[ss]=FALSE; ss++;}  /*send*/
	    }
        }
    } else {
      /* each group memeber needs to get new value from leader */
      {CHK(); ids[ss]=gids[i]; rs[ss]=TRUE; js[ss]=FALSE;
      ro[ss]=FALSE; ss++;} /*recv*/
    }

    #undef CHK

    *ssize = ss;

    #undef MAXN
}

/*----------------------------------------------------------------------------*/
/* Computes the list of indices rindex[] where rindex[p] is the n'th processor*/
/* from which p would receive i'th processor's updates                        */
/*----------------------------------------------------------------------------*/
void compute_rslots
(
    RMScheduleType pattern,
    int i,
    int N,
    int ssize,
    const int ids[],
    const int rs[],
    const int js[],
    const int ro[],
    int rt[]
)
{
    int j = 0, nrecvs = 0;
    if( !rt ) return;
    for( j = 0; j < ssize; j++ )
    {
      if( rs[j] )
      {
        rt[j] = nrecvs++;
      }
      else
      {
	int k = 0, nbrid = ids[j], nbrnrecvs = 0;
	#define MAXNACTIONS 1000
        int nssize = 0;
        int nids[MAXNACTIONS], nrs[MAXNACTIONS];
	int njs[MAXNACTIONS], nro[MAXNACTIONS], nrt[MAXNACTIONS];
        compute_schedule( pattern, nbrid, N,
	                  MAXNACTIONS, &nssize, nids, nrs, njs, nro, NULL );
	#undef MAXNACTIONS
        for( k = 0; k < nssize; k++ )
        {
            if( nrs[k] && nids[k] == i )
            {
                break;
            }

            if( nrs[k] )
            {
                nbrnrecvs++;
            }
        }
        assert( k < nssize ); /*Must find i in nbr's schedule*/
        rt[j] = nbrnrecvs;
      }
    }
}

/*----------------------------------------------------------------------------*/
/* Returns the receive and send communication schedule for this processor     */
/*----------------------------------------------------------------------------*/
int compute_schedule
(
    RMScheduleType pattern,
    int i,
    int N,
    int mx,
    int *ssize,
    int ids[],
    int rs[],
    int js[],
    int ro[],
    int rt[]
)
{
   int retval = 0;
   switch( pattern )
   {
   case RM_SCHEDULE_STAR:
     {compute_star_schedule(i, N, mx, ssize, ids, rs, js, ro); break;}
   case RM_SCHEDULE_ALL_TO_ALL:
     {compute_all_to_all_schedule(i, N, mx, ssize, ids, rs, js, ro); break;}
   case RM_SCHEDULE_BUTTERFLY:
     {compute_butterfly_schedule(i, N, mx, ssize, ids, rs, js, ro); break;}
   case RM_SCHEDULE_GROUPED_BFLY:
     {compute_grouped_bfly_schedule(i, N, mx, ssize, ids, rs, js, ro);break;}
   case RM_SCHEDULE_PHASE_BFLY:
     {compute_phase_bfly_schedule(i, N, mx, ssize, ids, rs, js, ro); break;}
   default:
     {printf("compute_schedule: Unknown pattern %d\n",pattern);retval=-1;break;}
   }
   if( rt ) { compute_rslots( pattern, i, N, *ssize, ids, rs, js, ro, rt ); }
   return retval;
}

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
)
{
    int j = 0;

    fprintf( fp, "%d: ----------------------------\n", i );
    fprintf( fp, "%d: --   Reduction Schedule   --\n", i );
    fprintf( fp, "%d: ----------------------------\n", i );
    for( j = 0; j < ssize; j++ )
    {
        fprintf( fp, "%d:  %3d %s %3d", i, i, (rs[j] ? "<--" : "-->"), ids[j] );
	fprintf( fp, "%s", ((rs[j] && js[j]) ? " js" : "") );
	fprintf( fp, "%s", (rs[j] ? (ro[j] ? " r" : " o") : "" ) );
	fprintf( fp, " %d\n", rt[j] );
    }
    fprintf( fp, "%d: ----------------------------\n", i );
    fflush( fp );
}

/*----------------------------------------------------------------------------*/
int test_schedule( int ac, char *av[] )
{
    int i = 0, N = 13, sched = RM_SCHEDULE_PHASE_BFLY;
    int ssize = 0;
    int ids[MAXACTIONS], rs[MAXACTIONS], js[MAXACTIONS], ro[MAXACTIONS];
    int rt[MAXACTIONS];

    if( ac > 1 ) N = atoi(av[1]);
    if( ac > 2 ) sched = atoi(av[2]);

    printf( "N = %d, schedule = %d\n\n", N, sched );

    for( i = 0; i < N; i++ )
    {
        compute_schedule( sched, i, N, MAXACTIONS, &ssize, ids, rs, js, ro, rt);
        print_schedule( stdout, i, N, ssize, ids, rs, js, ro, rt );
    }
    return 0;
}

/*----------------------------------------------------------------------------*/
#if 0
int main(int ac, char *av[]){return test_schedule(ac,av);}
#endif

/*----------------------------------------------------------------------------*/
