/*----------------------------------------------------------------------------*/
/* TM interface.                                                              */
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#ifndef __TM_KIT_H
#define __TM_KIT_H

/*---------------------------------------------------------------------------*/
#define MAX_DOUBLE 1.797693e+308

/*----------------------------------------------------------------------------*/
/* Type of time values                                                        */
/*----------------------------------------------------------------------------*/
#define COMPOSITETIME 1
#if !COMPOSITETIME /*-------------------------------------------------------*/

    typedef double TM_Time;

    #define TM_TS(A)           (A)
    #define TM_TIE(A)          (0)

    #define TM_EQ(A,B)         ((A) == (B))
    #define TM_NE(A,B)         ((A) != (B))
    #define TM_LT(A,B)         ((A) < (B))
    #define TM_LE(A,B)         ((A) <= (B))
    #define TM_GT(A,B)         ((A) > (B))
    #define TM_GE(A,B)         ((A) >= (B))

    #define TM_Add(A,B)        ((A)+(B))
    #define TM_Sub(A,B)        ((A)-(B))
    #define TM_Mult(A,B)       ((A)*(B))
    #define TM_Div(A,B)        ((A)/(B))

    #define TM_setZero(A)      ((A) = 0.0)
    #define TM_isZero(A)       ((A) == 0.0)

    #define TM_Max(A,B)        ((A)>(B) ? (A) : (B))
    #define TM_Min(A,B)        ((A)<(B) ? (A) : (B))

    #define TM_ZERO  0
    #define TM_NEG1  -1
    #define TM_IDENT MAX_DOUBLE /*TM_Min(TM_IDENT,X)==X*/

    #define TM_STR(_s,A)       (sprintf(_s,"%f",(A))?_s:_s)

#else  /*COMPOSITETIME*//*--------------------------------------------------*/

    typedef struct
    {
        double ts;
        double tie;
    } TM_Time;

    #define TM_TS(A)           ((A).ts)
    #define TM_TIE(A)          ((A).tie)

    #define TM_EQ(A,B)  ((A).ts == (B).ts && (A).tie == (B).tie)
    #define TM_NE(A,B)  (!TM_EQ(A, B))
    #define TM_LT(A,B)  ((A).ts < (B).ts || ((A).ts==(B).ts && (A).tie<(B).tie))
    #define TM_LE(A,B)  (TM_LT(A,B) || TM_EQ(A,B))
    #define TM_GT(A,B)  (!TM_LE(A,B))
    #define TM_GE(A,B)  (!TM_LT(A,B))

    static TM_Time TM_Add(TM_Time A, TM_Time B)
        {TM_Time t; t.ts=A.ts+B.ts; t.tie=A.tie+B.tie; return t;}
    static TM_Time TM_Sub(TM_Time A, TM_Time B)
        {TM_Time t; t.ts=A.ts-B.ts; t.tie=A.tie-B.tie; return t;}
    static TM_Time TM_Mult(TM_Time A, TM_Time B)
        {TM_Time t; t.ts=A.ts*B.ts; t.tie=A.tie*B.tie; return t;}
    static TM_Time TM_Div(TM_Time A, TM_Time B)
        {TM_Time t; t.ts=A.ts/B.ts; t.tie=A.tie/B.tie; return t;}

    #define TM_setZero(A) ((A).ts = (A).tie = 0.0)
    #define TM_isZero(A) ((A).ts == 0.0 && (A).tie == 0.0)

    #define TM_Max(A,B) (TM_GT(A,B) ? (A) : (B))
    #define TM_Min(A,B) (TM_LT(A,B) ? (A) : (B))

    extern TM_Time TM_ZERO;
    extern TM_Time TM_NEG1;
    extern TM_Time TM_IDENT;/*TM_Min(TM_IDENT,X)==X*/

    #define TM_STR(_s,A)       (sprintf(_s,"%f:%f",(A).ts,(A).tie)?_s:_s)

#endif /*COMPOSITETIME*//*--------------------------------------------------*/

/*----------------------------------------------------------------------------*/
void hton_TM_Time( TM_Time *pt );
void ntoh_TM_Time( TM_Time *pt );

/*----------------------------------------------------------------------------*/
/*
 * Type of time value "qualifications".
 *
 * Every time value T is associated with a qualification q.
 * The time qualification q denotes whether the time value T is inclusive
 * or exclusive.  An inclusive time value T represents all time values
 * less than or equal to T.  An exclusive time value T represents all
 * time values strictly greater than T.
 *
 * Qualifications are ordered: inclusive qualification, qi, is less than
 * exclusive qualification, qe.
 * 
 * Given two time values A & B with their respective qualifications,
 * A=<T1,q1> and B=<T2,q2>, then:
 *
 *    A < B  iff (T1 < T2) or (T1 == T2 and q1 < q2).
 *    A == B iff (T1 == T2 and q1 == q2).
 */
/*----------------------------------------------------------------------------*/
enum { TM_TIME_QUAL_INCL, TM_TIME_QUAL_EXCL };
typedef unsigned char TM_TimeQual;
#define hton_TM_TimeQual( /*TM_TimeQual * */pq ) do{/*nothing*/}while(0)
#define ntoh_TM_TimeQual( /*TM_TimeQual * */pq ) do{/*nothing*/}while(0)
#define TM_TIME_QUAL_STR( /*TM_TimeQual*/ q ) \
    ((q) == TM_TIME_QUAL_INCL ? "Inclusive" : \
     (q) == TM_TIME_QUAL_EXCL ? "Exclusive" : "Error-Bad-Time-Qual" )

/*---------------------------------------------------------------------------*/
/* Return codes */
#define TM_SUCCESS    0
#define TM_FAILED     1

/*----------------------------------------------------------------------------*/
/* Return codes from LBTS Started handler */
#define TM_ACCEPT     1234
#define TM_DEFER      5678

/*----------------------------------------------------------------------------*/
/* Handler called when LBTS computation done */
typedef void (*TM_LBTSDoneProc)(TM_Time, TM_TimeQual, long);

/*----------------------------------------------------------------------------*/
/* Function called when another PE starts LBTS computation */
typedef long (*TM_LBTSStartedProc)(long, TM_Time *, TM_TimeQual *,
                                   TM_LBTSDoneProc *);

/*----------------------------------------------------------------------------*/
/* Type of tag added to messages */
typedef struct { long tags[4/*CUSTOMIZE*/]; } TM_TagType;
#define hton_TM_TagType(/*TM_TagType* */_t) ((*(_t)) = htonl(*(_t)))/*XXX*/
#define ntoh_TM_TagType(/*TM_TagType* */_t) ((*(_t)) = ntohl(*(_t)))/*XXX*/

/*----------------------------------------------------------------------------*/
/* Exported functions                                                         */
/*----------------------------------------------------------------------------*/
void TM_Init(long);
void TM_InitDone(void);
void TM_Finalize(void);
void TM_SetLBTSStartProc(TM_LBTSStartedProc);
long TM_CurrentEpoch(void);
void TM_Recent_LBTS(TM_Time *);
void TM_Recent_Qual(TM_TimeQual *);
long TM_StartLBTS(TM_Time, TM_TimeQual, TM_LBTSDoneProc, long *);
void TM_PutTag(TM_TagType *);
void TM_Out(TM_Time, long );
void TM_In(TM_Time, TM_TagType);
void TM_PrintStats(void);
void TM_PrintState(void);
void TM_Tick(void);

/*----------------------------------------------------------------------------*/
/* Time-managed communication topology interface                              */
/*----------------------------------------------------------------------------*/
void TM_TopoInit( void ); /*Initialize topology*/
int TM_TopoIsAllToAll( void ); /*Is this an all-to-all topology?*/
void TM_TopoPrint( void ); /*Dump current topology information*/
/*----------------------------------------------------------------------------*/
/* Use these for simple all-to-all (symmetric, uniform) topology.             */
/*----------------------------------------------------------------------------*/
void TM_TopoSetAllToAll( void );/*Set topology to all-to-all*/
void TM_TopoSetMinExternalLA( TM_Time );/*Set min external LA among all recvrs*/
TM_Time TM_TopoGetMinExternalLA( void );/*Get min external LA among all recvrs*/
/*----------------------------------------------------------------------------*/
/* Use these for complex/sparse topology.                                     */
/*----------------------------------------------------------------------------*/
void TM_TopoAddSender( int spe );   /*spe sends TSO msgs to this PE*/
void TM_TopoAddReceiver(int rpe, TM_Time external_la);/*this PE sends to rpe*/
int TM_TopoGetNumSenders( void ); /*Get the the #senders to this PE*/
int TM_TopoGetNumReceivers( void );/*Get the the #receivers from this PE*/
int TM_TopoGetSender( int i );    /*Get the i'th sender's PE number; -1 if N/A*/
int TM_TopoGetReceiver( int i ); /*Get the i'th receiver's PE number;-1 if N/A*/
TM_Time TM_TopoGetReceiverLAByIndex(int i);/*Get the i'th receiver's lookahead*/
TM_Time TM_TopoGetReceiverLAByPE(int rpe);/*Get the receiver pe's lookahead*/
/*----------------------------------------------------------------------------*/
/* Internal LA specification for (spe->rpe) pair.  This is the least delay    */
/* any incoming event from spe experiences in this PE before going out to rpe.*/
/* Note that spe==thisPE only becomes a simple special case.                  */
/* If (spe->rpe) was already specified before, then la is reduced as needed.  */
/* Before invoking this, spe should be added as a sender & rpe as a receiver  */
/*----------------------------------------------------------------------------*/
void TM_TopoSetInternalLA( int spe, int rpe, TM_Time la );
int TM_TopoGetInternalLA( int spe, int rpe, TM_Time *la );

/*----------------------------------------------------------------------------*/
/* TM module functions                                                        */
/*----------------------------------------------------------------------------*/
typedef void *TMM_Closure;
typedef TM_LBTSStartedProc TMM_LBTSStartedProc;
typedef void (*TMM_InitFunc)( TMM_Closure, TMM_LBTSStartedProc );
typedef void (*TMM_FiniFunc)( TMM_Closure );
typedef long (*TMM_CurrentEpochFunc)( TMM_Closure );
typedef void (*TMM_Recent_LBTSFunc)( TMM_Closure, TM_Time * );
typedef long (*TMM_StartLBTSFunc)( TMM_Closure,
                                 TM_Time, TM_TimeQual, TM_LBTSDoneProc, long *);
typedef void (*TMM_PutTagFunc)( TMM_Closure, char *, int *nbytes );
typedef void (*TMM_OutFunc)( TMM_Closure, TM_Time, long );
typedef void (*TMM_InFunc)( TMM_Closure, TM_Time, char *, int *nbytes );
typedef void (*TMM_PrintStatsFunc)( TMM_Closure );
typedef void (*TMM_PrintStateFunc)( TMM_Closure );
typedef void (*TMM_TickFunc)( TMM_Closure );
typedef void (*TMM_NotifyNewLBTSFunc)( TMM_Closure, TM_Time, TM_TimeQual );
/*----------------------------------------------------------------------------*/
typedef struct _TMModuleStruct
{
    TMM_Closure                 closure;
    TMM_InitFunc                init;
    TMM_FiniFunc                fini;
    TMM_CurrentEpochFunc        currepoch;
    TMM_Recent_LBTSFunc         recentlbts;
    TMM_StartLBTSFunc           startlbts;
    TMM_PutTagFunc              puttag;
    TMM_OutFunc                 out;
    TMM_InFunc                  in;
    TMM_PrintStatsFunc          printstats;
    TMM_PrintStateFunc          printstate;
    TMM_TickFunc                tick;
    TMM_NotifyNewLBTSFunc       newlbts;
} TMModule;
/*----------------------------------------------------------------------------*/
void TM_AddModule( TMModule *mod );

/*----------------------------------------------------------------------------*/
#endif /*__TM_KIT_H*/
