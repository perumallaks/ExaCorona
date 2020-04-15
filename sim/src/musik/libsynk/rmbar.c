/*----------------------------------------------------------------------------*/
/* Efficient barrier implementation using RM services.                        */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "mycompat.h"
#include "rmbar.h"
#include "fm.h"
#include "rm.h"

/*----------------------------------------------------------------------------*/
struct RVALUE_TYPE_STRUCT
{
    long val;
};
/*----------------------------------------------------------------------------*/
static void hton_RVALUE_TYPE( RVALUE_TYPE *v ) { (v)->val = htonl((v)->val); }
static void ntoh_RVALUE_TYPE( RVALUE_TYPE *v ) { (v)->val = ntohl((v)->val); }
/*----------------------------------------------------------------------------*/
typedef struct rv_shell { RVALUE_TYPE rv; struct rv_shell *next; } RVALUE_SHELL;
static RVALUE_SHELL *rv_shells = 0;
static void alloc_rv_shells( void )
    { int i = 0, n = 100;
      MYASSERT( !rv_shells, ("!") );
      rv_shells = (RVALUE_SHELL *)malloc(sizeof(RVALUE_SHELL)*n);
      for(i=0;i<n-1;i++){rv_shells[i].next=&rv_shells[i+1];}
      rv_shells[n-1].next = 0;
    }
static RVALUE_TYPE *rv_new( void )
    { RVALUE_SHELL *s = rv_shells;
      if(!s){alloc_rv_shells();} MYASSERT(rv_shells, ("!"));
      s = rv_shells; rv_shells = s->next;
      return &s->rv; }
static void rv_delete( RVALUE_TYPE *v )
    { RVALUE_SHELL *s = (RVALUE_SHELL *)v;
      s->next = rv_shells; rv_shells = s; }
static void rv_init(RVALUE_TYPE *a)
    { if(a){ (a)->val = 1000000; } }
static void rv_assign( RVALUE_TYPE *a, RVALUE_TYPE *b )
    { if((a)&&(b)) *(a) = *(b); }
static void rv_reduce( RVALUE_TYPE *a, RVALUE_TYPE *b)
    { if((a)&&(b) && (a)->val > (b)->val) { (a)->val = (b)->val; } }
    /*{ if((a)&&(b)) { if((b)->val < (a)->val) (a)->val = (b)->val; } }*/
static void rv_set( RVALUE_TYPE *a, double v )
    { if(a) { (a)->val = v; } }
static void rv_print( FILE *fp, RVALUE_TYPE *a )
    { if(a)fprintf( (fp), "(%ld)", (a)->val ); }
/*----------------------------------------------------------------------------*/
enum { RM_START_MSG, RM_VALUE_MSG };
#define RM_MSG_TYPE_STR(t) ((t)==RM_START_MSG?"RM_START_MSG":"RM_VALUE_MSG")
typedef struct RMMesgStruct
{
    long trans;
    long type;
    long from_pe, to_pe;
    RVALUE_TYPE val;
    struct RMMesgStruct *next; /*Scratch for msg buffering*/
} RMMesg;
#define hton_RMMesg(/*RMMesg **/m) \
    do{ \
	(m)->trans = htonl((m)->trans); \
	(m)->type = htonl((m)->type); \
	(m)->from_pe = htonl((m)->from_pe); \
	(m)->to_pe = htonl((m)->to_pe); \
	hton_RVALUE_TYPE(&(m)->val); \
    }while(0)
#define ntoh_RMMesg(/*RMMesg **/m) \
    do{ \
	(m)->trans = ntohl((m)->trans); \
	(m)->type = ntohl((m)->type); \
	(m)->from_pe = ntohl((m)->from_pe); \
	(m)->to_pe = ntohl((m)->to_pe); \
	ntoh_RVALUE_TYPE(&(m)->val); \
    }while(0)

/*----------------------------------------------------------------------------*/
typedef struct
{
    RMUserHandle usr;
    long trans_num;
} Transaction;
static Transaction action[2];
static long curr_trans = 0;

/*---------------------------------------------------------------------------*/
static int dbg = 0;
static int myid = 0, N = 1;
static unsigned int hid = 0;

/*----------------------------------------------------------------------------*/
void rmb_send_msg( RMMesg *msg )
{
    RMMesg out_msg = *msg;
    FM_stream *stream = 0;
    int dest = msg->to_pe, maxlen = 100;

    msg->next = 0;
    hton_RMMesg( &out_msg );
    stream = FM_begin_message( dest, maxlen, hid );
    FM_send_piece( stream, &out_msg, sizeof(out_msg) );
    FM_end_message( stream );

#if !NODEBUG
if(dbg>2){printf("RMB %ld sent %s#%ld to %ld (%lu bytes)\n",msg->from_pe,RM_MSG_TYPE_STR(msg->type),msg->trans,msg->to_pe,sizeof(out_msg));}
#endif
}

/*----------------------------------------------------------------------------*/
static void rmb_send_start_msg( RMUserHandle usr, void *closure,
    int from_pe, int to_pe, int to_slot )
{
    RMMesg msg;
    msg.trans = (long)closure;
    MYASSERT( usr == action[msg.trans%2].usr, ("!") );
    msg.type = RM_START_MSG; msg.from_pe = from_pe; msg.to_pe = to_pe;
    rv_init( &msg.val );
    rmb_send_msg( &msg );
}

/*----------------------------------------------------------------------------*/
static void rmb_send_value_msg( RMUserHandle usr, void *closure,
    int from_pe, int to_pe, RVALUE_TYPE *v, int to_slot )
{
    RMMesg msg;
    msg.trans = (long)closure;
    MYASSERT( usr == action[msg.trans%2].usr, ("!") );
    msg.type = RM_VALUE_MSG; msg.from_pe = from_pe; msg.to_pe = to_pe;
    rv_assign( &msg.val, v );
    rmb_send_msg( &msg );
}

/*---------------------------------------------------------------------------*/
static int fm_handler( FM_stream *stream, unsigned int sender )
{
    RMMesg msg, *m = &msg;

#if !NODEBUG
if(dbg>2){printf("RMB fm_handler() from %d\n", sender);fflush(stdout);}
#endif

    FM_receive( m, stream, sizeof(*m) );
    ntoh_RMMesg( m );

    if( !(0<=m->from_pe && m->from_pe<N &&
          0<=m->to_pe && m->to_pe<N &&
          (m->type==RM_START_MSG || m->type==RM_VALUE_MSG)) )
    {
        printf( "Error on read: %ld, %ld\n", m->from_pe, m->to_pe );
    }
    else
    {
	    Transaction *act = 0;

	    if( m->trans == curr_trans )
	    {
		act = &action[m->trans%2];
		MYASSERT( act->trans_num == m->trans, ("!") );
	    }
	    else if( m->trans == curr_trans+1 )
	    {
		act = &action[m->trans%2];
		MYASSERT( act->trans_num == m->trans, ("!") );
#if !NODEBUG
if(dbg>2){printf("*** %d: Future msg! %ld,%ld\n", myid, curr_trans, m->trans);}
#endif
	    }
	    else if( m->trans > curr_trans+1 )
	    {
		MYASSERT(0, ("%d: ERROR! %ld, %ld\n",myid,curr_trans,m->trans));
	    }
	    else
	    {
		/*This msg must be a START msg for prev trans.  Just drop it.*/
		MYASSERT( m->type == RM_START_MSG && m->trans<curr_trans, ("!") );
	    }

	    if( m->type == RM_START_MSG )
	    {
#if !NODEBUG
if(dbg>2){printf("%d START_MSG%ld<-%ld\n",myid,m->trans,m->from_pe);}
#endif
		if( !act )
		{
#if !NODEBUG
if(dbg>2){printf("%d ignoring old START_MSG%ld\n", myid, m->trans);}
#endif
		}
		else
		{
	            rm_receive_start( act->usr, m->from_pe );
		}
	    }
	    else /*m->type == RM_VALUE_MSG*/
	    {
#if !NODEBUG
if(dbg>2){printf("%d VALUE_MSG%ld<-%ld\n",myid,m->trans,m->from_pe);}
#endif
		MYASSERT( act /*Action must exist!*/, ("!") );
                rm_receive_value( act->usr, m->from_pe, &m->val );
	    }
    }

    return FM_CONTINUE;
}

/*---------------------------------------------------------------------------*/
void rmb_init( void )
{
    {char *estr = getenv("RMB_DEBUG"); dbg = estr ? atoi(estr) : 0;}

#if !NODEBUG
if(dbg>0){printf("RMB_DEBUG=%d\n",dbg);fflush(stdout);}
#endif

    myid = FM_nodeid;
    N = FM_numnodes;
    FML_RegisterHandler( &hid, fm_handler );

    {
      int i = 0;
      RVALUE_CLASS rv_class;
      rv_class.rv_new = rv_new;
      rv_class.rv_delete = rv_delete;
      rv_class.rv_init = rv_init;
      rv_class.rv_assign = rv_assign;
      rv_class.rv_reduce = rv_reduce;
      rv_class.rv_print = rv_print;

      for(i=0;i<2;i++)
      {
	Transaction *act = &action[i];
        act->usr = rm_register( N, myid, MAX_PE, &rv_class );
        rm_init( act->usr, RM_SCHEDULE_GROUPED_BFLY );
        act->trans_num = i;
      }

      curr_trans = 0;
    }
}

/*---------------------------------------------------------------------------*/
void rmb_barrier( void )
{
    int done = FALSE;
    Transaction *act = &action[curr_trans%2];
    MYASSERT( act->trans_num == curr_trans, ("!") );
    MYASSERT( rm_get_status( act->usr, 0 ) == RM_ACTIVE, ("!") );

    /*Contribute self value*/
    {
        RVALUE_TYPE rval;
        rv_set( &rval, curr_trans );
        rm_receive_value( act->usr, myid, &rval );
    }

#if !NODEBUG
if(dbg>0){printf("PE %d: rmb_barrier() #%ld started...\n",myid,curr_trans);fflush(stdout);}
#endif
    do
    {
	RVALUE_TYPE min_val;
	FM_extract(1);
	done = rm_resume( act->usr, &min_val,
			  rmb_send_start_msg,
			  rmb_send_value_msg,
			  (void*)curr_trans );
	MYASSERT( !done || min_val.val == curr_trans,
	        ("%ld %ld",min_val.val,curr_trans) );
    }while( !done );
#if !NODEBUG
if(dbg>0){printf("PE %d: rmb_barrier() #%ld done.\n",myid,curr_trans);fflush(stdout);}
#endif

    rm_init( act->usr, RM_SCHEDULE_GROUPED_BFLY );
    act->trans_num += 2;
    curr_trans++;
}

/*----------------------------------------------------------------------------*/
