/*----------------------------------------------------------------------------*/
/* A "hybrid/combination" TM implementation!                                  */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "mycompat.h"
#include "fm.h"
#include "tm.h"

/*----------------------------------------------------------------------------*/
FILE *tmfp = 0;

/*----------------------------------------------------------------------------*/
typedef struct
{
    TM_Time LBTS;            /*Most recently known value*/
    TM_TimeQual qual;        /*Qualification of recent LBTS; see TM_TimeQual*/

    long epoch_id;           /*Epoch number corresponding to recent LBTS*/
    int modindex;            /*Which module delivered this recent LBTS value?*/

    TM_Time rep_ts;          /*What min ts this PE supplied most recently?*/
    TM_TimeQual rep_qual;    /*What min qual this PE supplied most recently?*/

    TM_LBTSStartedProc sproc;/*Callback for getting this PE's snap shot value*/

    TM_LBTSDoneProc *dproc;  /*List of callbacks waiting for new LBTS value*/
    int max_dproc;           /*Limit on #callbacks that can wait for new LBTS*/
    int n_dproc;             /*Actual #callbacks waiting for new LBTS*/
} TMLBTSInfo;

/*----------------------------------------------------------------------------*/
typedef struct
{
    long nlbts;             /*Total number of LBTS computations so far*/
    TIMER_TYPE start;       /*When did TM_Init() end?*/
} TMStatistics;

/*----------------------------------------------------------------------------*/
typedef struct _TMModuleStateStruct
{
    TMModule cl;            /*Module metaclass info*/
    int index;              /*Index of this module into array of modules*/
    int inited;             /*Has this module been initialized?*/
    int finied;             /*Has this module been finalied?*/
    long nwins;             /*#times this module advanced time before others*/
    struct _TMModuleStateStruct* next; /*Next in linked list of modules*/
} TMModuleState;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int nmods;             /*Number of TM modules registered*/
    int maxmods;           /*Current limit on #modules; expanded as needed*/
    int activemod;         /*Which module is being ticked currently*/
    TMModuleState *mods;   /*Array of registered modules*/
} TMModuleList;

/*----------------------------------------------------------------------------*/
typedef struct
{
    int debug;              /*Debugging level*/
    int myid;               /*My processor ID*/
    int N;                  /*Total number of processors*/
    TMLBTSInfo lbts;        /*How LBTS values must be acquired/stored/reported*/
    TMModuleList modlist;   /*List of TM sub-modules*/
    TMStatistics stats;     /*Self-explanatory*/
} TMState;

/*----------------------------------------------------------------------------*/
/* Global TM state, and cached pointers into the global TM state              */
/* In multi-threaded implementation, pass TM state as argument to API calls,  */
/* and move the cached pointers into the functions (as local variables).      */
/*----------------------------------------------------------------------------*/
static TMState tm_state, *st;
static TMLBTSInfo *lbts;
static TMModuleList *modlist;
static TMStatistics *stats;

static char tmp_str[1000];/*Scratch to hold timestamp->string conversion*/

/*----------------------------------------------------------------------------*/
void TM_AddModule( TMModule *mod )
{
    if( modlist->nmods >= modlist->maxmods )
    {
        void *p = modlist->mods;
        int n = (modlist->maxmods += 10)*sizeof(TMModuleState);
        modlist->mods = (TMModuleState*) (p ?  realloc(p,n) : malloc(n));
        MYASSERT( modlist->mods, ("!") );
    }
    MYASSERT( modlist->nmods < modlist->maxmods, ("!") );

    {
        int i = modlist->nmods++;
        TMModuleState *lmod = &modlist->mods[i];
        lmod->cl = *mod;
        lmod->index = i;
        lmod->inited = FALSE;
        lmod->finied = FALSE;
        lmod->nwins = 0;
        lmod->next = 0;
        if( i > 0 ) (lmod-1)->next = lmod;
    }
}

/*----------------------------------------------------------------------------*/
static void TM_AddDoneProc( TM_LBTSDoneProc dproc )
{
    if( dproc )
    {
        MYASSERT( lbts->n_dproc < lbts->max_dproc, ("!") );
        lbts->dproc[lbts->n_dproc++] = dproc;
    }
}

/*----------------------------------------------------------------------------*/
static void TM_InitMod( TMModuleState *mod );
/*----------------------------------------------------------------------------*/
static void TM_LBTSDone( TM_Time new_lbts, TM_TimeQual new_qual, long epoch_id )
{
    TMModuleState *amod = &modlist->mods[modlist->activemod];

#if !NODEBUG
if(st->debug>0){fprintf(tmfp,"TM_LBTSDone(LBTS=%s, qual=%s) epoch=%ld activemod=%d nwins=%ld\n",TM_STR(tmp_str,new_lbts), TM_TIME_QUAL_STR(new_qual), lbts->epoch_id, modlist->activemod, amod->nwins);fflush(tmfp);}
#endif

    if( TM_GT(new_lbts, lbts->LBTS) ||
        (TM_EQ(new_lbts, lbts->LBTS) && new_qual > lbts->qual) )
    {
        lbts->LBTS = new_lbts;
        lbts->qual = new_qual;
        lbts->modindex = modlist->activemod;
        amod->nwins++;
#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"** WIN **\n");}
#endif

        /*Inform other TM modules of this new value*/
        {
            TMModuleState *mod = modlist->mods;
            while( mod )
            {
                if(mod != amod)
                {
                    if(!mod->inited)TM_InitMod( mod );
                    mod->cl.newlbts(mod->cl.closure, lbts->LBTS, lbts->qual);
                }
                mod = mod->next;
            }
        }
    }

    /*Inform all the waiting dprocs*/
    {
        int c;
        for( c = 0; c < lbts->n_dproc; c++ )
        {
            lbts->dproc[c]( lbts->LBTS, lbts->qual, lbts->epoch_id );
        }
        lbts->n_dproc = 0;
        lbts->epoch_id++;
    }
#if !NODEBUG
if(st->debug>3){TM_PrintState();}
#endif
}

/*----------------------------------------------------------------------------*/
static long TM_LBTSStarted( long epoch_id, TM_Time *min_ts,
                            TM_TimeQual *qual, TM_LBTSDoneProc *dproc )
{
    long sproc_flag = TM_ACCEPT;

    if( lbts->n_dproc > 0 )
    {
        *min_ts = lbts->rep_ts;
        *qual = lbts->rep_qual;
        *dproc = &TM_LBTSDone;
    }
    else
    {
        MYASSERT( lbts->sproc, ("!") );
        sproc_flag = lbts->sproc( lbts->epoch_id, min_ts, qual, dproc );
        if(sproc_flag == TM_DEFER) { *min_ts = lbts->LBTS; *qual = lbts->qual; }
        else {if( *dproc ) { TM_AddDoneProc( *dproc ); *dproc = &TM_LBTSDone; }}

        lbts->rep_ts = *min_ts;
        lbts->rep_qual = *qual;
    }

#if !NODEBUG
if(st->debug>0){fprintf(tmfp,"TM_LBTSStarted(min_ts=%s, qual=%s)\n",TM_STR(tmp_str,*min_ts),TM_TIME_QUAL_STR(*qual));fflush(tmfp);}
if(st->debug>3){TM_PrintState();}
#endif

    return sproc_flag;
}

/*----------------------------------------------------------------------------*/
static void TM_InitMod( TMModuleState *mod )
{
    if( !mod->inited )
    {
        mod->inited = TRUE;
        mod->cl.init(mod->cl.closure, TM_LBTSStarted);
    }
}

/*----------------------------------------------------------------------------*/
static void TM_FiniMod( TMModuleState *mod )
{
    if( mod->inited && !mod->finied )
    {
        mod->inited = FALSE;
        mod->finied = TRUE;
        mod->cl.fini(mod->cl.closure);
    }
}

/*----------------------------------------------------------------------------*/
void TM_Init (long long_k)
{
    char *dbgstr = getenv("TM_DEBUG");

    char tmfname[100];
    sprintf(tmfname,"tm-%ld.log",FM_nodeid);

    tmfp = fopen(tmfname, "w");
    if(!tmfp) tmfp = stdout;

    st = &tm_state;
    lbts = &st->lbts;
    modlist = &st->modlist;
    stats = &st->stats;

    st->debug            = dbgstr?atoi(dbgstr):0;
    st->myid             = (int) FM_nodeid;
    st->N                = (int) FM_numnodes;

    LSCFGLD( "TM_DEBUG", (long)st->debug, "TM debug intensity" );

#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"TM_DEBUG=%d\n",st->debug);}
#endif

    lbts->LBTS           = TM_ZERO;
    lbts->qual           = TM_TIME_QUAL_INCL;
    lbts->epoch_id       = 0;
    lbts->sproc          = 0;
    lbts->max_dproc      = 1000;
    lbts->n_dproc        = 0;
    lbts->dproc          = (TM_LBTSDoneProc *)malloc(
                           sizeof(TM_LBTSDoneProc)*lbts->max_dproc);

    modlist->nmods       = 0;
    modlist->maxmods     = 0;
    modlist->activemod   = -1;
    modlist->mods        = 0;

    stats->nlbts         = 0;

    TM_TopoInit();

    /*Add modules*/
    {
     if(!getenv("TM_NORED" )){void TMMRed_AddModule(void); TMMRed_AddModule();}

    if(FALSE || getenv("TM_DONULL"))/*Add TM-Null only on explicit request?*/
     if(!getenv("TM_NONULL")){void TMMNull_AddModule(void);TMMNull_AddModule();}

     LSCFGST( "TM_NORED", (getenv("TM_NORED")?"TRUE":"FALSE"),
              "TM should not use RED?" );
     LSCFGST( "TM_NONULL", (getenv("TM_NONULL")?"TRUE":"FALSE"),
              "TM should not use NULL?" );
    }

    FML_Barrier();

    TIMER_NOW(stats->start);
#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"TM_Init() done.\n");}
#endif
}

/*----------------------------------------------------------------------------*/
void TM_InitDone(void)
{
    TMModuleState *mod = modlist->mods;

    while( mod )
    {
            if(!mod->inited)TM_InitMod( mod );
            mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_Finalize(void)
{
    TMModuleState *mod = modlist->mods;

    while( mod )
    {
            if(!mod->inited)TM_FiniMod( mod );
            mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_SetLBTSStartProc( TM_LBTSStartedProc started_proc )
{
    lbts->sproc = started_proc;
}

/*----------------------------------------------------------------------------*/
long TM_CurrentEpoch( void )
{
    return lbts->epoch_id;
}

/*----------------------------------------------------------------------------*/
void TM_Recent_LBTS( TM_Time *pts )
{
    if( pts ) *pts = lbts->LBTS;
}

/*----------------------------------------------------------------------------*/
void TM_Recent_Qual( TM_TimeQual *pq )
{
    if( pq ) *pq = lbts->qual;
}

/*----------------------------------------------------------------------------*/
long TM_StartLBTS( TM_Time min_ts, TM_TimeQual qual,
                   TM_LBTSDoneProc done_proc, long *ptrans )
{
    TMModuleState *mod = modlist->mods;

#if !NODEBUG
if(st->debug>0){fprintf(tmfp,"TM_StartLBTS(min_ts=%s,qual=%s,dproc=%p,trans=%ld)\n",TM_STR(tmp_str,min_ts),TM_TIME_QUAL_STR(qual),done_proc,*ptrans);fflush(tmfp);}
#endif

    lbts->rep_ts = min_ts;
    lbts->rep_qual = qual;

    TM_AddDoneProc( done_proc );
    *ptrans = lbts->epoch_id;

    while( mod )
    {
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.startlbts(mod->cl.closure, min_ts, qual, &TM_LBTSDone, NULL);
        mod = mod->next;
    }

    return TM_SUCCESS;
}

/*----------------------------------------------------------------------------*/
void TM_PutTag( TM_TagType *ptag )
{
    int nbytes = 0;
    TMModuleState *mod = modlist->mods;

#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"TM_PutTag(%ld)\n",(*((long*)ptag)));}
#endif

    while( mod )
    {
        int tagsz = 0;
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.puttag(mod->cl.closure, ((char*)ptag)+nbytes, &tagsz);
        nbytes += tagsz;
        mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_Out( TM_Time ts, long nevents )
{
    TMModuleState *mod = modlist->mods;

#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"TM_Out(ts=%f,nevents=%ld)\n",TM_TS(ts),nevents);}
#endif

    while( mod )
    {
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.out(mod->cl.closure, ts, nevents);
        mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_In( TM_Time ts, TM_TagType tag )
{
    int nbytes = 0;
    TMModuleState *mod = modlist->mods;

#if !NODEBUG
if(st->debug>1){fprintf(tmfp,"TM_In(ts=%f,tag=%ld)\n",TM_TS(ts),(*((long*)&tag)));}
#endif

    while( mod )
    {
        int tagsz = 0;
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.in(mod->cl.closure, ts, ((char*)&tag)+nbytes, &tagsz);
        nbytes += tagsz;
        mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_PrintStats( void )
{
    TMModuleState *mod = modlist->mods;

    while( mod )
    {
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.printstats(mod->cl.closure);
        fprintf(tmfp,"%d: NWINS= %ld TOT= %ld\n", st->myid,mod->nwins,lbts->epoch_id);
        mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
void TM_PrintState( void )
{
    int i = 0;
    TMModuleState *mod = modlist->mods;

    fprintf(tmfp,"------------  TM STATE START ------------\n");
    fprintf(tmfp,"myid=%d, N=%d\n", st->myid, st->N);

    fprintf(tmfp,"LBTS={LBTS=%f (%s) sproc=%p, max_dproc=%d, n_dproc=%d, dproc=[",
           TM_TS(lbts->LBTS), TM_TIME_QUAL_STR(lbts->qual),
           lbts->sproc, lbts->max_dproc, lbts->n_dproc);
    for(i=0; i<lbts->n_dproc; i++) fprintf(tmfp,"%s%p", (i>0?",":"!"),lbts->dproc[i]);
    fprintf(tmfp,"]}\n");

    fprintf(tmfp,"Stats={nlbts=%ld}\n", stats->nlbts);

    fprintf(tmfp,"\n");

    while( mod )
    {
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.printstate(mod->cl.closure);
        mod = mod->next;
    }

    fprintf(tmfp,"------------  TM STATE END ------------\n");
}

/*----------------------------------------------------------------------------*/
void TM_Tick( void )
{
    TMModuleState *mod = modlist->mods;

    MYASSERT( modlist->nmods > 0, ("At least one TM module required") );

    while( mod )
    {
        modlist->activemod = mod->index;
        if(!mod->inited)TM_InitMod( mod );
        mod->cl.tick(mod->cl.closure);
        modlist->activemod = -1;
        mod = mod->next;
    }
}

/*----------------------------------------------------------------------------*/
/* TM Communication topology:                                                 */
/* All2All by default.  If the user adds at least one sender/receiver, then   */
/* specified topology will be used.                                           */
/*----------------------------------------------------------------------------*/
typedef struct
{
    int all2all;        /*Is it an all-to-all topology?*/

    /*Minimum among lookaheads to all receivers*/
    TM_Time min_external_la;

#define INTERNALLA 0
#if INTERNALLA
    /*Internal lookahead from an input PE to output PE*/
    TM_Time internal_la[MAX_PE][MAX_PE];
#else /*INTERNALLA*/
    TM_Time internal_la;
#endif /*INTERNALLA*/

    /*If NOT all2all, use the following*/

    /*Senders of TSO messages to this PE*/
    int nsenders;
    struct
    {
        int pe;
    } sender[MAX_PE];
    int sindex[MAX_PE]; /*Mapping from spe to i*/

    /*Receivers of TSO messages from this PE*/
    int nrecvrs;
    struct
    {
        int pe;
        TM_Time external_la;
    } recvr[MAX_PE];
    int rindex[MAX_PE]; /*Mapping from rpe to i*/
} TMTopology;

/*----------------------------------------------------------------------------*/
static TMTopology *topo;

/*----------------------------------------------------------------------------*/
void TM_TopoPrint( void )
{
    int i = 0;
    FILE *fp = tmfp;/*XXX*/
    if( !topo ) return;
    fprintf( fp, "***** TM Topology Begin *****\n" );
    fprintf( fp, "\tAll-to-all= %s\n", topo->all2all ? "yes" : "no" );
    fprintf( fp, "\tmin_external_la= %lf\n", TM_TS(topo->min_external_la) );
    fprintf( fp, "\tnsenders= %d\n", topo->nsenders );
    for(i=0;i<topo->nsenders;i++)
        fprintf(fp,"\t\tsender[%d]: PE= %d\n", i, topo->sender[i].pe);
    fprintf( fp, "\tnrecvrs= %d\n", topo->nrecvrs );
    for(i=0;i<topo->nrecvrs;i++)
        fprintf(fp,"\t\trecvr[%d]: PE= %d LA= %lf\n",
                      i, topo->recvr[i].pe, TM_TS(topo->recvr[i].external_la));
    fprintf( fp, "***** TM Topology End *****\n" );
}

/*----------------------------------------------------------------------------*/
static void turnon_all2all( void )
{
    int i = 0, j = 0;
    for( i = 0; i < MAX_PE; i++ )
    {
        for( j = 0; j < MAX_PE; j++ )
        {
#if INTERNALLA
            topo->internal_la[i][j] = TM_ZERO;
#else /*INTERNALLA*/
            topo->internal_la = TM_ZERO;
            break; /*XXX*/
#endif /*INTERNALLA*/
        }
    }
    topo->nsenders = 0;
    topo->nrecvrs = 0;
    topo->min_external_la = TM_IDENT;

    topo->all2all = TRUE;
}

/*----------------------------------------------------------------------------*/
static void turnoff_all2all( void )
{
    int i = 0, j = 0;
    for( i = 0; i < MAX_PE; i++ )
    {
        for( j = 0; j < MAX_PE; j++ )
        {
#if INTERNALLA
            topo->internal_la[i][j] = TM_IDENT;
#else /*INTERNALLA*/
            topo->internal_la = TM_IDENT;
            break;
#endif /*INTERNALLA*/
        }
        topo->sindex[i] = -1;
        topo->rindex[i] = -1;
    }
    topo->nsenders = 0;
    topo->nrecvrs = 0;
    topo->min_external_la = TM_IDENT;

    topo->all2all = FALSE;

    TM_TopoAddSender( st->myid ); /*I am a sender to myself by default*/
}

/*----------------------------------------------------------------------------*/
void TM_TopoInit( void )
{
    topo = (TMTopology *)calloc(1, sizeof(TMTopology));
    turnon_all2all();
}

/*----------------------------------------------------------------------------*/
int TM_TopoIsAllToAll( void )
{
    return topo->all2all;
}

/*----------------------------------------------------------------------------*/
void TM_TopoSetAllToAll( void )
{
    turnon_all2all();
}

/*----------------------------------------------------------------------------*/
static int TM_TopoFindSenderIndex( int spe )
{
    int i=0; for(i=0;i<topo->nsenders;i++)if(topo->sender[i].pe==spe)return i;
    return -1;
}

/*----------------------------------------------------------------------------*/
static int TM_TopoFindReceiverIndex( int rpe )
{
    int i=0; for(i=0;i<topo->nrecvrs;i++)if(topo->recvr[i].pe==rpe)return i;
    return -1;
}

/*----------------------------------------------------------------------------*/
void TM_TopoAddSender( int spe )
{
    int pei = 0;
    if( topo->all2all ) { turnoff_all2all(); }
    if( (pei = TM_TopoFindSenderIndex( spe )) < 0 )
    {
        /*spe doesn't exist*/
        int *i = &topo->nsenders, j = 0;
        MYASSERT( *i < MAX_PE, ("%d %d", *i, MAX_PE) );
        topo->sender[*i].pe = spe;
        topo->sindex[spe] = *i;
        for( j = 0; j < MAX_PE; j++ )
        {
#if INTERNALLA
            TM_Time *ila = &topo->internal_la[*i][j];
#else /*INTERNALLA*/
            TM_Time *ila = &topo->internal_la;
#endif /*INTERNALLA*/
            if(TM_GE(*ila, TM_IDENT)) *ila=TM_ZERO;
        }
        pei = (*i)++;
    }
    else
    {
        /*spe already exists*/
        pei = pei; /*Dummy*/
    }
}

/*----------------------------------------------------------------------------*/
void TM_TopoAddReceiver( int rpe, TM_Time la )
{
    int pej = 0;
    if( topo->all2all ) { turnoff_all2all(); }
    if( (pej = TM_TopoFindReceiverIndex( rpe )) < 0 )
    {
        /*rpe doesn't exist*/
        int i = 0, *j = &topo->nrecvrs;
        MYASSERT( *j < MAX_PE, ("%d %d", *j, MAX_PE) );
        topo->recvr[*j].pe = rpe;
        topo->rindex[rpe] = *j;
        topo->recvr[*j].external_la = la;
        for( i = 0; i < MAX_PE; i++ )
        {
#if INTERNALLA
            TM_Time *jla = &topo->internal_la[i][*j];
#else /*INTERNALLA*/
            TM_Time *jla = &topo->internal_la;
#endif /*INTERNALLA*/
            if(TM_GE(*jla, TM_IDENT)) *jla=TM_ZERO;
        }
        pej = (*j)++;
    }
    else
    {
        /*rpe already exists; simply update la*/
        TM_Time *pla = &topo->recvr[pej].external_la;
        *pla = TM_Min(*pla, la);
    }

    topo->min_external_la = TM_Min(topo->min_external_la, la);
}

/*----------------------------------------------------------------------------*/
int TM_TopoGetNumSenders( void )
    { return topo->all2all ? st->N : topo->nsenders; }
/*----------------------------------------------------------------------------*/
int TM_TopoGetNumReceivers( void )
    { return topo->all2all ? st->N-1 : topo->nrecvrs; }
/*----------------------------------------------------------------------------*/
int TM_TopoGetSender( int i )
    { return topo->all2all ?
             ( i==0 ? st->myid : (st->myid==0 ? i : (i<=st->myid ? i-1 : i)) ):
             topo->sender[i].pe; }
/*----------------------------------------------------------------------------*/
int TM_TopoGetReceiver( int i )
    { return topo->all2all ? (i < st->myid ? i : i+1) : topo->recvr[i].pe; }
/*----------------------------------------------------------------------------*/
void TM_TopoSetMinExternalLA( TM_Time t )
    { topo->min_external_la = TM_Min(topo->min_external_la, t);
fprintf(tmfp,"TM_TopoSetMinExternalLA() min_external_la = %lf\n",TM_TS(topo->min_external_la));}
/*----------------------------------------------------------------------------*/
TM_Time TM_TopoGetMinExternalLA( void )
    { return topo->min_external_la; }
/*----------------------------------------------------------------------------*/
TM_Time TM_TopoGetReceiverLAByIndex(int i)
    { return topo->all2all ? topo->min_external_la :topo->recvr[i].external_la;}
/*----------------------------------------------------------------------------*/
TM_Time TM_TopoGetReceiverLAByPE(int rpe)
{
    int i = 0;

    if( topo->all2all )
        return TM_TopoGetReceiverLAByIndex(0);

    for( i = 0; i < topo->nrecvrs; i++ ) {if( topo->recvr[i].pe == rpe ) break;}

    return (i >= topo->nrecvrs ? TM_NEG1 : topo->recvr[i].external_la);
}

/*----------------------------------------------------------------------------*/
void TM_TopoSetInternalLA( int spe, int rpe, TM_Time la )
{
    TM_Time old_la = TM_ZERO;
    int i = topo->sindex[spe], j = topo->rindex[rpe];

    if( i < 0 ) { TM_TopoAddSender( spe ); i = topo->sindex[spe]; }
    if( j < 0 ) { TM_TopoAddReceiver( rpe, TM_ZERO ); j = topo->rindex[rpe]; }

    MYASSERT( !topo->all2all,
              ("Sender %d & receiver %d should be added first", spe, rpe) );
    MYASSERT( spe == topo->sender[i].pe, ("%d %d", spe, topo->sender[i].pe) );
    MYASSERT( rpe == topo->recvr[j].pe, ("%d %d", rpe, topo->recvr[j].pe) );

#if INTERNALLA
    old_la = topo->internal_la[spe][rpe];
    topo->internal_la[spe][rpe] = TM_Min(old_la, la);
#else /*INTERNALLA*/
    old_la = topo->internal_la;
    topo->internal_la = TM_Min(old_la, la);
#endif /*INTERNALLA*/
}

/*----------------------------------------------------------------------------*/
int TM_TopoGetInternalLA( int spe, int rpe, TM_Time *la )
{
    int returned = 0;
    
    if( topo->all2all )
    {
        *la = TM_ZERO;
        returned = 1;
    }
    else
    {
        int i = topo->sindex[spe], j = topo->rindex[rpe];

        MYASSERT(i<0||spe==topo->sender[i].pe,("%d %d",spe,topo->sender[i].pe));
        MYASSERT(j<0||rpe==topo->recvr[j].pe, ("%d %d", rpe,topo->recvr[j].pe));

        if( i >= 0 && j >= 0 )
        {
#if INTERNALLA
            *la = topo->internal_la[spe][rpe];
#else /*INTERNALLA*/
            *la = topo->internal_la;
#endif /*INTERNALLA*/
            returned = 1;
        }
    }

    return returned;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
void TM_htond( double* pd )                                                   \
{                                                                             \
    int little_endian = (htonl(0x1L) != 0x1L);                                \
if(0){static int x;if(!x){x=1;fprintf(tmfp,"This host is %s-endian!\n",little_endian?"little":"big");}} \
                                                                              \
little_endian = FALSE; /*XXX*/                                                \
                                                                              \
    if( little_endian )                                                       \
    {                                                                         \
        if( sizeof(double) == sizeof(long) )                                  \
        {                                                                     \
            long *l = (long *)pd;                                             \
            *l = htonl(*l);                                                   \
        }                                                                     \
        else if( sizeof(double) == 2*sizeof(long) )                           \
        {                                                                     \
            double d = *pd;                                                   \
            long *pd = (long *)&d;                                            \
            long *pl = (long *)pd;                                            \
            pl[0] = pd[1];                                                    \
            pl[1] = pd[0];                                                    \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            MYASSERT(FALSE,("Unknown network-host converion for TM_Time!"));  \
        }                                                                     \
    }                                                                         \
}

/*----------------------------------------------------------------------------*/
#if !COMPOSITETIME
void hton_TM_Time( TM_Time *pt ) { TM_htond( pt ); }
#else
void hton_TM_Time( TM_Time *pt ) { TM_htond( &pt->ts ); TM_htond( &pt->tie ); }
#endif
void ntoh_TM_Time( TM_Time *pt ) { hton_TM_Time( pt ); }

/*----------------------------------------------------------------------------*/
#if COMPOSITETIME
    TM_Time TM_ZERO = { 0, 0 };
    TM_Time TM_NEG1 = { -1, 0 };
    TM_Time TM_IDENT = {MAX_DOUBLE,MAX_DOUBLE};
#endif

/*----------------------------------------------------------------------------*/
