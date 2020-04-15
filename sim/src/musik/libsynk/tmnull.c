/*----------------------------------------------------------------------------*/
/* A NULL-message algorithm ala CMB.                                          */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "fm.h"
#include "tm.h"

/*----------------------------------------------------------------------------*/
typedef struct TMMNullMesgStruct
{
    long from_pe;         /*Which processor sent this?*/
    TM_Time lbts;         /*LBTS from from_pe to this pe*/
    TM_TimeQual qual;     /*LBTS qual*/
} TMMNullMesg;

/*----------------------------------------------------------------------------*/
#define hton_TMMNullMesg(/*TMMNullMesg **/m) \
    do{ \
        (m)->from_pe = htonl((m)->from_pe); \
        hton_TM_Time(&(m)->lbts); \
        hton_TM_TimeQual(&(m)->qual); \
    }while(0)
#define ntoh_TMMNullMesg(/*TMMNullMesg **/m) \
    do{ \
        (m)->from_pe = ntohl((m)->from_pe); \
        ntoh_TM_Time(&(m)->lbts); \
        ntoh_TM_TimeQual(&(m)->qual); \
    }while(0)

/*----------------------------------------------------------------------------*/
typedef struct
{
    int pe;
    TM_Time lbts;
    TM_TimeQual qual;
} ConnectionInfo;
typedef struct
{
    int n;
    ConnectionInfo conn[MAX_PE];
} ConnectionList;
typedef ConnectionList Inputs, Outputs;

/*----------------------------------------------------------------------------*/
typedef struct
{
    long ID;                /*Current (active) epoch identifier*/
    TM_Time LBTS;           /*Most recently known value*/
    TM_TimeQual qual;       /*Qualification of recent LBTS; see TM_TimeQual*/
    TM_LBTSStartedProc sproc;/*Callback to get this PE's current guarantee*/
    TM_LBTSDoneProc *dproc; /*List of callbacks waiting for new LBTS value*/
    int max_dproc;          /*Limit on #callbacks that can wait for new LBTS*/
    int n_dproc;            /*Actual #callbacks waiting for new LBTS*/
} LBTSInfo;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int updated;      /*Has the status changed in some way?*/
    int inprogress;   /*Is a null-mesg activity already in progress?*/
    TM_Time transient_ts; /*Timestamps of events received while inprogress*/
} Computation;

/*----------------------------------------------------------------------------*/
typedef struct
{
    struct
    {
        int on;    /*Piggyback LBTS onto events?*/
    } piggyback_lbts;
    struct
    {
        int on;    /*Send nulls more often than for deadlock-avoidance?*/
        int count; /*How many idle ticks happened with no null msg exchange*/
        int freq;  /*Send null msgs every this many idle ticks*/
    } aggressive_sends;
} Options;

/*----------------------------------------------------------------------------*/
typedef struct
{
    TIMER_TYPE start; /*When did TMMNull_Init() end?*/
    struct
    {
        long nsent;       /*Total number of messages sent*/
        long nrecd;       /*Total number of messages received*/
    } nulls, events;
} Statistics;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int debug;              /*Debugging level*/
    int myid;               /*My processor ID*/
    int N;                  /*Total number of processors*/
    unsigned int fmh;       /*FM handle ID*/
    Inputs  in;             /*Incoming connections*/
    Outputs out;            /*Outgoing connections*/
    LBTSInfo lbts;          /*How LBTS values must be acquired/stored/reported*/
    Computation comp;       /*Status of distributed null-mesg computation*/
    Options opt;            /*Tunable parameters*/
    Statistics stats;       /*Self-explanatory*/
} TMState;

/*----------------------------------------------------------------------------*/
/* Global TM state, and cached pointers into the global TM state              */
/* In multi-threaded implementation, pass TM state as argument to API calls,  */
/* and move the cached pointers into the functions (as local variables).      */
/*----------------------------------------------------------------------------*/
static TMState tm_state, *st;
static LBTSInfo *lbts;
static Computation *comp;
static Options *opt;
static Statistics *stats;

/*----------------------------------------------------------------------------*/
static int TMMNull_RecvNullMesg( FM_stream *strm, unsigned senderID );

/*----------------------------------------------------------------------------*/
static void TMMNull_ConfigTopology( void )
{
    int i = 0;
    for( i = 0, st->in.n = TM_TopoGetNumSenders(); i < st->in.n; i++ )
    {
        ConnectionInfo *conn = &st->in.conn[i];
        int pe = TM_TopoGetSender(i);
        conn->pe = pe;
        conn->lbts = TM_ZERO;
        conn->qual = TM_TIME_QUAL_INCL;
#if !NODEBUG
if(st->debug>2){printf("SenderPE[%d]=%d\n",i,pe);fflush(stdout);}
#endif
    }

    for( i = 0, st->out.n = TM_TopoGetNumReceivers(); i < st->out.n; i++ )
    {
        ConnectionInfo *conn = &st->out.conn[i];
        int pe = TM_TopoGetReceiver(i);
        conn->pe = pe;
        conn->lbts = TM_ZERO;
        conn->qual = TM_TIME_QUAL_INCL;
#if !NODEBUG
if(st->debug>2){printf("ReceiverPE[%d]=%d\n",i,pe);fflush(stdout);}
#endif
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_PrintNullMesg(FILE *fp, TMMNullMesg *m)
{
    fprintf( fp, "{TMMNullMesg from_pe=%ld, lbts=%lf, qual=%s}",
                    m->from_pe, TM_TS(m->lbts), TM_TIME_QUAL_STR(m->qual) );
}

/*----------------------------------------------------------------------------*/
static void TMMNull_SendNullMesg( int to_pe, TM_Time ts, TM_TimeQual qual)
{
    TMMNullMesg msg;
    FM_stream *strm = 0;

    msg.from_pe = st->myid;
    msg.lbts = ts;
    msg.qual = qual;

    strm = FM_begin_message( to_pe, sizeof(TMMNullMesg), st->fmh );
    hton_TMMNullMesg( &msg );
    FM_send_piece( strm, &msg, sizeof(msg) );
    FM_end_message( strm );

    ++stats->nulls.nsent;
}

/*----------------------------------------------------------------------------*/
static void TMMNull_Init(TMM_Closure closure, TMM_LBTSStartedProc sproc)
{
    char *dbgstr = getenv("TMNULL_DEBUG");     /* Integer [0,inf] */

    MYASSERT( closure == &tm_state, ("Closures should match") );

    st = &tm_state;
    lbts = &st->lbts;
    comp = &st->comp;
    opt = &st->opt;
    stats = &st->stats;

    st->debug            = dbgstr?atoi(dbgstr):0;
    st->myid             = (int) FM_nodeid;
    st->N                = (int) FM_numnodes;
    st->fmh              = -1;

    LSCFGLD( "TMNULL_DEBUG", (long)st->debug, "TMNULL debug intensity" );

    lbts->ID             = 0;
    lbts->LBTS           = TM_ZERO;
    lbts->qual           = TM_TIME_QUAL_INCL;
    lbts->sproc          = sproc;
    lbts->max_dproc      = 1000;
    lbts->n_dproc        = 0;
    lbts->dproc          = (TM_LBTSDoneProc *)malloc(
                           sizeof(TM_LBTSDoneProc)*lbts->max_dproc);

    comp->updated        = TRUE;
    comp->inprogress     = FALSE;
    comp->transient_ts   = TM_IDENT;

    stats->nulls.nsent   = 0;
    stats->nulls.nrecd   = 0;
    stats->events.nsent  = 0;
    stats->events.nrecd  = 0;

    opt->piggyback_lbts.on      = TRUE;
    opt->aggressive_sends.on    = FALSE;
    opt->aggressive_sends.count = 0;
    opt->aggressive_sends.freq  = FM_numnodes; /*One round trip around all PEs*/

    TMMNull_ConfigTopology();

    FML_RegisterHandler( &st->fmh, TMMNull_RecvNullMesg );

    FML_Barrier();

#if !NODEBUG
if(st->debug>=0){printf("%d: TMNull initialized.\n",st->myid);fflush(stdout);}
#endif
    TIMER_NOW(stats->start);
}

/*----------------------------------------------------------------------------*/
static void TMMNull_Fini(TMM_Closure closure)
{
}

/*----------------------------------------------------------------------------*/
static void TMMNull_SetLBTSStartProc( TMM_Closure closure, TM_LBTSStartedProc s)
    { lbts->sproc = s; }
/*----------------------------------------------------------------------------*/
static long TMMNull_CurrentEpoch( TMM_Closure closure ) { return lbts->ID; }
/*----------------------------------------------------------------------------*/
static void TMMNull_Recent_LBTS( TMM_Closure closure, TM_Time *pts )
    { if( pts ) *pts = lbts->LBTS; }
/*----------------------------------------------------------------------------*/
static void TMMNull_PutTag( TMM_Closure closure, char *ptag, int *nbytes )
{
    TM_Time min_ext_la = TM_TopoGetMinExternalLA();
    TM_Time piggy_lbts = TM_Add( lbts->LBTS, min_ext_la );
    *((TM_Time*)ptag) = opt->piggyback_lbts.on ? piggy_lbts : TM_NEG1;
    *nbytes = sizeof(TM_Time);
}

/*----------------------------------------------------------------------------*/
static void TMMNull_Out( TMM_Closure closure, TM_Time ts, long nevents )
{
    stats->events.nsent += nevents;
}

/*----------------------------------------------------------------------------*/
static void TMMNull_In(TMM_Closure closure,TM_Time ts,char *ptag,int *nbytes)
{
    TM_Time piggy_lbts = *((TM_Time*)ptag);
    *nbytes = sizeof(TM_Time);

    stats->events.nrecd++;

    if( comp->inprogress )
    {
        TM_Time min_ext_la = TM_TopoGetMinExternalLA();
        ts = TM_Sub( ts, min_ext_la ); /*Caller adds min LA to ts*/
        comp->transient_ts = TM_Min( comp->transient_ts, ts );
        comp->updated = TRUE;
    }

    /*XXX Not sure if the piggybacked lbts must be corrected for transients*/
    /*But, better be sure than sorry*/
    piggy_lbts = TM_Min(piggy_lbts, comp->transient_ts);

    if( TM_GE(piggy_lbts, TM_ZERO) && TM_GT(piggy_lbts, lbts->LBTS) )
    {
        int i = 0;
#if !NODEBUG
if(st->debug>0){printf("Piggyback advances LBTS from %lf to %lf\n",TM_TS(lbts->LBTS),TM_TS(piggy_lbts));}
#endif
        lbts->LBTS = piggy_lbts;
        lbts->qual = TM_TIME_QUAL_INCL;

        /*Update any lagging input connections lbts*/
        for( i = 0; i < st->in.n; i++ )
        {
            ConnectionInfo *conn = &st->in.conn[i];
            if( conn->pe != st->myid && TM_GT(piggy_lbts, conn->lbts) )
            {
#if !NODEBUG
if(st->debug>0){printf("Piggyback updates lagging LBTS of input PE %d from %lf to %lf\n",conn->pe,TM_TS(conn->lbts),TM_TS(piggy_lbts));}
#endif
                conn->lbts = piggy_lbts;
                conn->qual = TM_TIME_QUAL_INCL;
            }
        }
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_AddDoneProc( TM_LBTSDoneProc dproc )
{
    if( dproc )
    {
        MYASSERT( lbts->n_dproc < lbts->max_dproc, ("!") );
        lbts->dproc[lbts->n_dproc++] = dproc;
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_DoNullSends( void )
{
    int i = 0, j = 0;

    if( !comp->updated )
    {
#if !NODEBUG
if(st->debug>4){printf("TMMNull_DoNullSends() nothing to do.\n");fflush(stdout);}
#endif
        return;
    }

    comp->updated = FALSE;

    for( j = 0; j < st->out.n; j++ ) /*For each receiver*/
    {
        ConnectionInfo *outconn = &st->out.conn[j];
        int rpe = outconn->pe;
        TM_Time external_la = TM_TopoGetReceiverLAByIndex( j );
        TM_Time lbts_j = TM_IDENT, *old_lbts_j = &outconn->lbts;
        TM_TimeQual qual_j=TM_TIME_QUAL_INCL, *old_qual_j = &outconn->qual;
#if !NODEBUG
if(st->debug>1){printf("TMMNull_DoNullSends() receiver PE %d has external_la %lf.\n",rpe,TM_TS(external_la));fflush(stdout);}
#endif
        for( i = 0; i < st->in.n; i++ ) /*For each sender*/
        {
            TM_Time temp_min_ts = TM_ZERO;
            ConnectionInfo *inconn = &st->in.conn[i];
            int spe = inconn->pe;
            TM_Time lbts_i = inconn->lbts;
            TM_TimeQual qual_i = inconn->qual;
            TM_Time pair_la = TM_IDENT;
#if !NODEBUG
if(st->debug>1){printf("TMMNull_DoNullSends() to %d lbts[%d(spe%d)]=%lf.\n",rpe,i,inconn->pe,TM_TS(lbts_i));fflush(stdout);}
#endif

            if( !TM_TopoGetInternalLA( spe, rpe, &pair_la ) ) continue;

            temp_min_ts = TM_Min( lbts_j, TM_Add(lbts_i, pair_la) );
            qual_j = TM_GT(temp_min_ts, lbts_j) ? qual_j :
                       TM_LT(temp_min_ts,lbts_j) ? qual_i :
                       (qual_i < qual_j ? qual_i : qual_j);
            lbts_j = temp_min_ts;
        }

        if( comp->inprogress && TM_LE(comp->transient_ts,lbts_j) )
        {
            lbts_j = comp->transient_ts;
            qual_j = TM_TIME_QUAL_INCL;
        }

        lbts_j = TM_Add(lbts_j, external_la);

#if 0 /*XXX*/
        #define ABS(t) ((t)<0?(-(t)):(t))
        #define TM_EQ2(t1,t2) (ABS((t1)-(t2))<=1e-10)
        MYASSERT( TM_GE(lbts_j,*old_lbts_j) ||
                  (TM_EQ2(lbts_j,*old_lbts_j) && qual_j>=*old_qual_j),
                  ("Null ts %308.6lf,%s shouldn't decrease from %308.6lf,%s %s",
                   lbts_j*1e+300,TM_TIME_QUAL_STR(qual_j),
                   1e+300**old_lbts_j,TM_TIME_QUAL_STR(*old_qual_j),
                   ((lbts_j-*old_lbts_j)<0?"-ve":"+ve"));TM_PrintState());
#endif /*XXX*/

#if !NODEBUG
if(st->debug>1){printf("TMMNull_DoNullSends() to %d lbts_j %lf old_lbts_j %lf.\n",rpe,TM_TS(lbts_j),TM_TS(*old_lbts_j));fflush(stdout);}
#endif

        /*Send only if new lbts is better than previously sent*/
        if( TM_GT(lbts_j, *old_lbts_j) ||
            (TM_EQ(lbts_j, *old_lbts_j) && qual_j > *old_qual_j) )
        {
            *old_lbts_j = lbts_j;
            *old_qual_j = qual_j;
            TMMNull_SendNullMesg( rpe, *old_lbts_j, *old_qual_j );
#if !NODEBUG
if(st->debug>1){printf("TMMNull_DoNullSends() sent NULL(%lf,%s) to %d.\n",TM_TS(*old_lbts_j),TM_TIME_QUAL_STR(*old_qual_j), rpe);fflush(stdout);}
#endif
        }
    }
#if !NODEBUG
if(st->debug>1){printf("TMMNull_DoNullSends() done.\n");fflush(stdout);}
#endif
}

/*----------------------------------------------------------------------------*/
static int TMMNull_UpdateSender(int spe, TM_Time null_ts,TM_TimeQual null_qual)
{
    int i = 0, updated = FALSE;
    ConnectionInfo *inconn = 0;

    for( i = 0; i < st->in.n; i++ ) if(st->in.conn[i].pe == spe) break;
    MYASSERT(i < st->in.n, ("Null sender %d should be found", spe));

    inconn = &st->in.conn[i];

    if( spe == st->myid || /*Special case: self lbts update can go up/down*/
        TM_LT(inconn->lbts, null_ts) || /*For others, a better ts is required*/
        (TM_EQ(inconn->lbts, null_ts) && inconn->qual < null_qual) )
    {
        inconn->lbts = null_ts;
        inconn->qual = null_qual;
        comp->updated = TRUE;
        updated = TRUE;
    }

    return updated;
}

/*----------------------------------------------------------------------------*/
static void TMMNull_RegisterLocalLBTS( TM_Time min_ts, TM_TimeQual qual,
                                       TM_LBTSDoneProc done_proc, long *ptrans )
{
    TM_Time min_ext_la = TM_TopoGetMinExternalLA();
    int myindex = 0, mype = TM_TopoGetSender( myindex );
    MYASSERT( mype == st->myid, ("%d %d",mype,st->myid) );

    TMMNull_AddDoneProc( done_proc );

    min_ts = TM_Sub(min_ts, min_ext_la);/*Caller already adds min-lookahead*/

    TMMNull_UpdateSender( mype, min_ts, qual );

    comp->inprogress = TRUE;

    if(ptrans) *ptrans = lbts->ID;
}

/*----------------------------------------------------------------------------*/
static long TMMNull_StartLBTS( TMM_Closure closure,
                TM_Time min_ts, TM_TimeQual qual,
                TM_LBTSDoneProc done_proc, long *ptrans )
{
#if !NODEBUG
if(st->debug>1){printf("TMMNull_StartLBTS(%lf,%s).\n",TM_TS(min_ts),TM_TIME_QUAL_STR(qual));fflush(stdout);}
#endif

    TMMNull_RegisterLocalLBTS( min_ts, qual, done_proc, ptrans );

    return TM_SUCCESS;
}

/*----------------------------------------------------------------------------*/
static void TMMNull_EvaluateProgress( void )
{
    int i = 0;
    TM_Time new_min_ts = TM_IDENT;
    TM_TimeQual new_min_qual = TM_TIME_QUAL_INCL;
    TM_Time min_ext_la = TM_TopoGetMinExternalLA();

    for( i = 0; i < st->in.n; i++ ) /*Find min lbts among all incoming*/
    {
        ConnectionInfo *conn = &st->in.conn[i];
        TM_Time this_lbts = conn->lbts;

        if(conn->pe == st->myid && st->N > 1) /*Self, multi-processor*/
        {
            continue; /*Skip self*/
        }

        if( st->N <= 1 )/*Special case for uni-processor*/
        {
            this_lbts = TM_Add(this_lbts, min_ext_la); /*Can progress by minLA*/
        }

        if( TM_GT(new_min_ts, this_lbts)  ||
            (TM_EQ(new_min_ts, this_lbts)  && new_min_qual > conn->qual) )
        {
            new_min_ts = this_lbts;
            new_min_qual = conn->qual;
        }
    }

    if( comp->inprogress && TM_LE(comp->transient_ts, new_min_ts) )
    {
        new_min_ts = comp->transient_ts;
        new_min_qual = TM_TIME_QUAL_INCL;
    }

    MYASSERT( TM_LE(lbts->LBTS, new_min_ts),
              ("%lf %lf",TM_TS(lbts->LBTS),TM_TS(new_min_ts));TM_PrintState() );

    /*Did the LBTS increase?*/
    if( TM_LT(lbts->LBTS, new_min_ts) ||
        (TM_EQ(lbts->LBTS, new_min_ts) && lbts->qual < new_min_qual ) )
    {
        int c;

        /*Yes, got an advance!*/
        lbts->LBTS = new_min_ts;
        lbts->qual = new_min_qual;

#if !NODEBUG
if(st->debug>1){printf("TMMNull_EvaluateProgress() got advance to (%lf,%s).\n",TM_TS(lbts->LBTS),TM_TIME_QUAL_STR(lbts->qual));fflush(stdout);}
#endif

        /*Inform all the waiting dprocs*/
        for( c = 0; c < lbts->n_dproc; c++ )
        {
            lbts->dproc[c]( lbts->LBTS, lbts->qual, lbts->ID );
        }
        lbts->n_dproc = 0;

        /*Mark that we're done with this round*/
        comp->inprogress = FALSE;
        comp->updated = FALSE;
        comp->transient_ts = TM_IDENT;
        lbts->ID++;
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_DoAggressiveSends( void )
{
    if( opt->aggressive_sends.on &&
        ++opt->aggressive_sends.count >= opt->aggressive_sends.freq )
    {
        opt->aggressive_sends.count = 0;
        comp->updated = TRUE;
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_Tick( TMM_Closure closure )
{
    if(!comp->inprogress && !comp->updated)
    {
        TMMNull_DoAggressiveSends();
    }
    else if(comp->inprogress && !comp->updated)
    {
        TMMNull_DoAggressiveSends();
    }
    else
    {
        MYASSERT( comp->updated, ("!") );
        if( !comp->inprogress )
        {
            /*Query for local LBTS*/
            TM_Time min_ts = TM_ZERO;
            TM_TimeQual qual = TM_TIME_QUAL_INCL;
            long sproc_flag;
            TM_LBTSDoneProc dproc = 0;

#if !NODEBUG
if(st->debug>1){printf("** Starting remotely initiated LBTS\n");fflush(stdout);}
#endif

            MYASSERT( lbts->sproc, ("!") );
            sproc_flag = lbts->sproc( lbts->ID, &min_ts, &qual, &dproc );
            if( sproc_flag==TM_DEFER ) {min_ts = lbts->LBTS; qual = lbts->qual;}
            TMMNull_RegisterLocalLBTS( min_ts, qual, dproc, NULL );
        }

        TMMNull_DoNullSends();
        TMMNull_EvaluateProgress();
    }
}

/*----------------------------------------------------------------------------*/
static int TMMNull_RecvNullMesg( FM_stream *strm, unsigned senderID )
{
    TMMNullMesg msg;

    FM_receive( &msg, strm, sizeof(msg) );
    ntoh_TMMNullMesg( &msg );
    ++stats->nulls.nrecd;

#if !NODEBUG
if(st->debug>1){printf("TMMNull_RecvNullMesg: "); TMMNull_PrintNullMesg(stdout,&msg);printf("\n");fflush(stdout);}
#endif

    TMMNull_UpdateSender( msg.from_pe, msg.lbts, msg.qual );

    return FM_CONTINUE;
}

/*----------------------------------------------------------------------------*/
static void TMMNull_NotifyNewLBTS(TMM_Closure closure, TM_Time ts,TM_TimeQual q)
{
    int i = 0;
#if !NODEBUG
if(st->debug>1){printf("TMMNull_NotifyNewLBTS(%lf, %s)\n",TM_TS(ts),TM_TIME_QUAL_STR(q));fflush(stdout);}
#endif
    for( i = 0; i < st->in.n; i++ )
    {
        ConnectionInfo *conn = &st->in.conn[i];

        /*Don't update self; otherwise, we might send out*/
        /*null msgs before events with timestamps <= lbts are sent out*/
        if( conn->pe == st->myid ) continue;

        if( TM_GT(ts, conn->lbts)  ||
            (TM_EQ(ts, conn->lbts)  && q > conn->qual) )
        {
            TMMNull_UpdateSender( conn->pe, ts, q );
        }
    }
}

/*----------------------------------------------------------------------------*/
static void TMMNull_PrintStats( TMM_Closure closure )
{
    TIMER_TYPE stop;
    double secs;
    long nlbts = lbts->ID;
    long totnulls = stats->nulls.nsent+stats->nulls.nrecd;
    long totevents = stats->events.nsent+stats->events.nrecd;

    TIMER_NOW(stop);
    secs = TIMER_DIFF(stop, stats->start);

    printf("TMMNULL-Stastics\n");
    printf("-----------\n");
    printf("NLBTS=           %ld\n", nlbts);
    printf("Time-per-LBTS=   %16.8f microsecs\n", secs/nlbts*1e6);
    printf("NLBTS-per-sec=   %16.8f\n", nlbts/secs);
    printf("Null-mesgs-sent= %ld\n", stats->nulls.nsent);
    printf("Null-mesgs-recd= %ld\n", stats->nulls.nrecd);
    printf("Events-sent=     %ld\n", stats->events.nsent);
    printf("Events-recd=     %ld\n", stats->events.nrecd);
    printf("Events/Null=     ");
    if(totnulls==0)printf("Inf\n");else printf("%lf\n", totevents*1.0/totnulls);
    printf("-----------\n");
}

/*----------------------------------------------------------------------------*/
static void TMMNull_PrintState( TMM_Closure closure )
{
    int i = 0, j = 0;

    printf("------------  TMMNULL STATE START ------------\n");
    printf("myid=%d, N=%d fmh=%d\n", st->myid, st->N, st->fmh);

    printf("LBTS={ID=%ld LBTS=%f (%s) "
           "sproc=%p, max_dproc=%d, n_dproc=%d, dproc=[",
           lbts->ID, TM_TS(lbts->LBTS), TM_TIME_QUAL_STR(lbts->qual),
           lbts->sproc, lbts->max_dproc, lbts->n_dproc);
    for(i=0; i<lbts->n_dproc; i++) printf("%s%p", (i>0?",":"!"),lbts->dproc[i]);
    printf("]}\n");

    for(j=0;j<2;j++)
    {
        ConnectionList *cl = (j==0?&st->in:&st->out);
        printf("%s=[%d]{\n",(j==0?"in":"out"),cl->n);
        for(i=0;i<cl->n;i++)
        {
            ConnectionInfo *conn = &cl->conn[i];
            printf("\t%d: {pe=%d, lbts=%lf, qual=%s}\n",
                   i,conn->pe,TM_TS(conn->lbts),TM_TIME_QUAL_STR(conn->qual));
        }
    }

    printf("Comp={inprogress=%d, updated=%d, transient_ts=%lf}\n",
            comp->inprogress,comp->updated,TM_TS(comp->transient_ts));

    printf("Stats={Nulls-nsent=%ld,Nulls-nrecd=%ld,"
           "Events-nsent=%ld,Events-nrecd=%ld}\n",
           stats->nulls.nsent, stats->nulls.nrecd,
           stats->events.nsent, stats->events.nrecd);

    printf("\n");
    printf("------------  TMMNULL STATE END ------------\n");
}

/*----------------------------------------------------------------------------*/
void TMMNull_AddModule( void )
{
    TMModule mod = {
        &tm_state,
        TMMNull_Init,
        TMMNull_Fini,
        TMMNull_CurrentEpoch,
        TMMNull_Recent_LBTS,
        TMMNull_StartLBTS,
        TMMNull_PutTag,
        TMMNull_Out,
        TMMNull_In,
        TMMNull_PrintStats,
        TMMNull_PrintState,
        TMMNull_Tick,
        TMMNull_NotifyNewLBTS
    };

    TM_AddModule( &mod );
}

/*----------------------------------------------------------------------------*/
