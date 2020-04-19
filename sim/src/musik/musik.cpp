/*---------------------------------------------------------------------------*/
/*
 *  Internal implementation of the simulator.
 *
 *  The contents of this file represent a snapshot of a specific implementation
 *  of the simulator interface.
 *
 *  Do NOT rely, in any way whatsoever, on the contents of this file.  They
 *  can (and will) change across future versions, sometimes very dramatically.
 *
 *  Author: Kalyan S. Perumalla
 */
/*---------------------------------------------------------------------------*/

#if MPI_AVAILABLE
#include "mpi.h"
#endif

#include <iomanip>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/time.h>

#include "musik.h"

#include <unistd.h>

#define WORDSIZE 8

/*---------------------------------------------------------------------------*/
FreeListMap SimEventBase::free_map;
ostream *_musdbgstrm = &cout;
int _simdbg = 1; ostream *_simdbgstrm = &cout, *_simdbgstrmdefault = &cout;
int _enslvl = 1;
bool _printcfg = false;
long _printdbgmaxfedid = 99999999;
MicroKernel *MicroKernel::instance = 0;
const double SimTime::MAX_TS = 1e37;
const double SimTime::MAX_TIE = 1e37;
const SimTime SimTime::MAX_TIME(SimTime::MAX_TS, SimTime::MAX_TIE);
const SimTime SimTime::MIN_TIME(-SimTime::MAX_TS, -SimTime::MAX_TIE);
const SimTime SimTime::ZERO_TIME(0,0);
const SimLocalPID SimPID::INVALID_LOC_ID = 0xffffffff;
const SimLocalPID SimPID::ANY_LOC_ID = 0xeeeeeeee;
const SimFedID SimPID::INVALID_FED_ID = -1;
const SimFedID SimPID::LOCAL_FED_ID = -2;
const SimFedID SimPID::ANY_FED_ID = -3;
const SimPID SimPID::INVALID_PID( SimPID::INVALID_LOC_ID,
                                  SimPID::INVALID_FED_ID );
const SimPID SimPID::ANY_PID( SimPID::ANY_LOC_ID, SimPID::ANY_FED_ID );
const SimReflectorID INVALID_RID = 0;
unsigned long SimProcessBase::tie_counter = 0;

/*---------------------------------------------------------------------------*/
extern "C" {
#include "fm.h"
#include "tm.h"
}
/*---------------------------------------------------------------------------*/
#define Synk_nodeid   FM_nodeid
#define Synk_numnodes FM_numnodes
#define Synk_MsgType 8765
/*---------------------------------------------------------------------------*/
static struct {
    int nactive;
    long tot_lbts;
    long tot_stopping_lbts;
    TM_Time ts;
    TM_TimeQual qual;
    unsigned int fed_fm_event_hid;
    unsigned int fed_fm_retract_hid;
} synk_lbts = { 0, 0, 0, {TM_ZERO.ts, TM_ZERO.tie}, TM_TIME_QUAL_INCL, 0 };
TIMER_TYPE start_time_with_init, start_time, stop_time;
bool start_timer_started = false;
/*---------------------------------------------------------------------------*/
void ts_synk2musik( SimTime &mts, const TM_Time &sts )
{
    mts.ts = sts.ts;
    mts.tie = sts.tie;
}
/*---------------------------------------------------------------------------*/
void ts_musik2synk( TM_Time &sts, const SimTime &mts )
{
    sts.ts = mts.ts;
    sts.tie = mts.tie;
}
/*---------------------------------------------------------------------------*/
static void Synk_LBTSDone(TM_Time new_lbts, TM_TimeQual new_qual, long trans)
{
    MUSDBG( 2, "LBTSDone trans= "<<trans<<" newlbts= "<<new_lbts );
    MUSDBG( 4, "SimulatorState: "<<*MicroKernel::muk() );
    ENSURE( 1, TM_GE(new_lbts, synk_lbts.ts), new_lbts<<" "<<synk_lbts.ts );
    synk_lbts.ts = new_lbts;
    synk_lbts.qual = new_qual;
    --synk_lbts.nactive;
    ++synk_lbts.tot_lbts;
}
/*---------------------------------------------------------------------------*/
static void Synk_InitiateLBTS( const SimTime &nts )
{
    if( synk_lbts.nactive > 0 ) return;
    MUSDBG( 2, "InitiateLBTS "<<nts );
    MUSDBG( 4, "SimulatorState: "<<*MicroKernel::muk() );
    ++synk_lbts.nactive;
    long trans = 0;
    TM_Time tm_ts; ts_musik2synk( tm_ts, nts );
    TM_StartLBTS(tm_ts, TM_TIME_QUAL_INCL, Synk_LBTSDone, &trans);
}
/*---------------------------------------------------------------------------*/
static long Synk_LBTSStarted( long trans, TM_Time *plbts, TM_TimeQual *pqual,
                              TM_LBTSDoneProc *pdproc )
{
    ++synk_lbts.nactive;
    const SimTime &lmin = MicroKernel::local_min();
    ts_musik2synk( *plbts, lmin );
    *pqual = TM_TIME_QUAL_INCL;
    *pdproc = &Synk_LBTSDone;
    MUSDBG( 2, "LBTSStarted trans= "<<trans<<" localmin= "<<*plbts );
    MUSDBG( 4, "SimulatorState: "<<*MicroKernel::muk() );
    return TM_ACCEPT;
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static bool Synk_FMSendEvent( const SimFedID &fed, SimEventBase *eb )
{
    bool sent = false;
    MUSDBG( 2, "FMSendEvent "<<eb->T() );
    Synk_Hdr *hdr = ((Synk_Hdr*)eb)-1; //XXX Assumes space present before event!

    ENSURE( 0, !eb->is_kernel_event(), "" );
    SimEvent *ev = reinterpret_cast<SimEvent *>(eb);
    int evtype = app_event_type( ev );
    int nevbasebytes = sizeof(SimEventBase::Data);
    int nappevbytes = app_event_data_size( evtype, ev );
    int nsendbytes = nevbasebytes + nappevbytes;

    static char *sendbuf = 0;
    static int sendbufsz = 0;
    if( !sendbuf || sendbufsz < nsendbytes )
    {
        if(sendbuf) { delete [] sendbuf; sendbuf = 0; sendbufsz = 0; }
        sendbufsz = nsendbytes;
        sendbuf = new char[sendbufsz];
        MUSDBG( 3, "Synk_FMSendEvent allocated sendbuf " << sendbufsz );
    }
    ENSURE( 1, sendbuf && nsendbytes <= sendbufsz,
               sendbuf << " " << sendbufsz << " " << nsendbytes );

    SimEventBase::Data *evbasedata =
            reinterpret_cast<SimEventBase::Data*>(sendbuf);
    *evbasedata = ev->data;
    char *appevdata = (char *)(evbasedata+1);
    app_event_data_pack( evtype, ev, appevdata, nappevbytes );

    int totbytes = sizeof(hdr->totsz) + sizeof(Synk_Hdr) + sizeof(evtype) +
                   sizeof(nappevbytes) + nsendbytes;

    FM_stream *stream = FM_begin_message(fed, totbytes,
                                         synk_lbts.fed_fm_event_hid);
    if( stream )
    {
        TM_PutTag( &hdr->tag );

        MUSDBG( 2, "FMSendEvent hdr->totsz="<<hdr->totsz );
        FM_send_piece( stream, &hdr->totsz, sizeof(hdr->totsz) );
        FM_send_piece( stream, hdr, sizeof(Synk_Hdr) );
        FM_send_piece( stream, &evtype, sizeof(evtype) );
        MUSDBG( 2, "FMSendEvent nappevbytes="<<nappevbytes );
        FM_send_piece( stream, &nappevbytes, sizeof(nappevbytes) );
        FM_send_piece( stream, sendbuf, nsendbytes );
        FM_end_message( stream );

        long nsent = 1;
        TM_Time tm_ts; ts_musik2synk( tm_ts, eb->T() );
        TM_Out( tm_ts, nsent );

        sent = true;
    }
    return sent;
}
/*---------------------------------------------------------------------------*/
static int Synk_FMEventHandler( FM_stream *stream, unsigned int src_pe )
{
    MUSDBG( 10, "FMEventHandler" );
    MUSDBG( 10, "FMEventHandler sizeof(Synk_Hdr)="<<sizeof(Synk_Hdr) );
    MUSDBG( 10, "FMEventHandler sizeof(SimEventBase)="<<sizeof(SimEventBase) );
    MUSDBG( 10, "FMEventHandler sizeof(SimEvent)="<<sizeof(SimEvent) );

    Synk_MsgSizeType totsz;
    FM_receive( &totsz, stream, sizeof(totsz) );
    MUSDBG( 10, "FMEventHandler totsz="<<totsz );
    MUSDBG( 10, "FMEventHandler SHDRSZ="<<SHDRSZ );
    ENSURE( 1, (size_t)totsz >= SHDRSZ, totsz << " " << SHDRSZ );

    void *buf = SimEventBase::get_buffer( totsz );
    MUSDBG( 10, "FMEventHandler got buffer "<<totsz );
    Synk_Hdr *hdr = reinterpret_cast<Synk_Hdr *>(buf);
    char *eventbuf = reinterpret_cast<char *>(hdr+1);

    FM_receive( hdr, stream, sizeof(Synk_Hdr) );
    MUSDBG( 8, "FMEventHandler hdr->totsz=="<<hdr->totsz<<" totsz="<<totsz );
    ENSURE( 0, hdr->totsz == totsz, hdr->totsz<<" "<<totsz );

    int evtype = -1;
    FM_receive( &evtype, stream, sizeof(evtype) );
    MUSDBG( 10, "FMEventHandler evtype " << evtype );

    SimEvent *ev = app_event_create( evtype, eventbuf );

    int nappevbytes = 0;
    FM_receive( &nappevbytes, stream, sizeof(nappevbytes) );
    MUSDBG( 3, "FMEventHandler nappevbytes" << nappevbytes );
    int nevbasebytes = sizeof(SimEventBase::Data);
    int nrecvbytes = nevbasebytes + nappevbytes;

    static char *recvbuf = 0;
    static int recvbufsz = 0;
    if( !recvbuf || recvbufsz < nrecvbytes )
    {
        if(recvbuf) { delete [] recvbuf; recvbuf = 0; recvbufsz = 0; }
        recvbufsz = nrecvbytes;
        recvbuf = new char[nrecvbytes];
        MUSDBG( 3, "Synk_FMSendEvent allocated recvbuf " << recvbufsz );
    }
    ENSURE( 1, recvbuf && nrecvbytes <= recvbufsz,
               recvbuf << " " << recvbufsz << " " << hdr->totsz );

    FM_receive( recvbuf, stream, nrecvbytes );
    MUSDBG( 3, "FMEventHandler recvbuf" << nrecvbytes );

    SimEventBase::Data *evbasedata =
            reinterpret_cast<SimEventBase::Data*>(recvbuf);
    char *appevdata = (char *)(evbasedata+1);
    ev->data = *evbasedata;
    app_event_data_unpack( evtype, ev, appevdata, nappevbytes );

    MUSDBG( 3, "!ev->is_kernel_event() " );
    ENSURE( 0, !ev->is_kernel_event(), "" );
    MUSDBG( 3, "erasing aux" );
    ev->erase_aux(); /*XXX Special case!*/

    MUSDBG( 3, "AddIncomingEvent "<<ev->T() );
    SimTime in_ts;
    in_ts = Simulator::sim()->incoming_remote_event( ev, src_pe );
    MUSDBG( 3, "incoming_remote_event done" );

    TM_Time tm_ts; ts_musik2synk( tm_ts, in_ts );
    ENSURE( 1, TM_GE(tm_ts, synk_lbts.ts), tm_ts<<" "<<synk_lbts.ts );
    TM_In(tm_ts, hdr->tag);

    return FM_CONTINUE;
}
/*---------------------------------------------------------------------------*/
struct FMRetractMsg
{
    SimEventBase::EventIDType eid;
    SimTime ts;
    TM_TagType tag;
};
static void Synk_FMSendRetract( const SimFedID &fed,
    const SimEventBase::EventIDType &eid,
    const SimTime &ts )
{
    MUSDBG( 2, "FMSendRetract "<<eid<<" "<<ts );

    long nsent = 1;
    FMRetractMsg m, *rmsg=&m;

    rmsg->eid = eid;
    rmsg->ts = ts;
    TM_PutTag( &rmsg->tag );

    FM_stream *stream = FM_begin_message(fed, sizeof(FMRetractMsg),
        synk_lbts.fed_fm_retract_hid);
    FM_send_piece( stream, rmsg, sizeof(FMRetractMsg) );
    FM_end_message( stream );

    TM_Time tm_ts; ts_musik2synk( tm_ts, ts );
    TM_Out( tm_ts, nsent );
}
/*---------------------------------------------------------------------------*/
static int Synk_FMRetractHandler( FM_stream *stream, unsigned int src_pe )
{
    MUSDBG( 2, "FMRetractHandler" );
    FMRetractMsg m, *rmsg=&m;
    FM_receive( rmsg, stream, sizeof(FMRetractMsg) );
    ENSURE( 1, TM_GE(rmsg->ts, synk_lbts.ts), rmsg->ts<<" "<<synk_lbts.ts );
    TM_Time tm_ts; ts_musik2synk( tm_ts, rmsg->ts );
    TM_In(tm_ts, rmsg->tag);

    MUSDBG( 2, "AddIncomingRetract "<<rmsg->ts );
    Simulator::sim()->incoming_remote_retract( rmsg->eid, rmsg->ts, src_pe );

    return FM_CONTINUE;
}
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static void Synk_Tick( void )
{
    FM_extract(~0);
    TM_Tick();
}
/*---------------------------------------------------------------------------*/
static void Synk_PreInit( int *pac, char ***pav )
{
    MUSDBG( 2, "Pre-initializing "<<getpid() );
    FM_pre_init( pac, pav );

    static ofstream musdbgstrm;
    string mfname = "musik-" + to_string(Synk_nodeid) + ".log";
    musdbgstrm.open(mfname);
    _musdbgstrm = &musdbgstrm;
}
/*---------------------------------------------------------------------------*/
static void Synk_Init( const char *stdout_fname )
{
    MUSDBG( 2, "Initializing pid "<<getpid() );
    { NodeInfo ninfo; FM_process_nodeinfo( &ninfo ); }
    FML_FMInit( stdout_fname );

    FML_RegisterHandler( &synk_lbts.fed_fm_event_hid, &Synk_FMEventHandler );
    FML_RegisterHandler( &synk_lbts.fed_fm_retract_hid, &Synk_FMRetractHandler);

    TM_Init(0); TM_SetLBTSStartProc(Synk_LBTSStarted);

    FML_Barrier();
    MUSDBG( 2, "Initialized" );
}
/*---------------------------------------------------------------------------*/
static void Synk_Start( void )
{
    TM_InitDone();
}
/*---------------------------------------------------------------------------*/
static void Synk_Stop( void )
{
    if(Synk_nodeid==0)MUSDBG( 1, "Stopping" );
    TM_Time tm_ts; ts_musik2synk( tm_ts, SimTime::MAX_TIME );
    while( TM_LT( synk_lbts.ts, tm_ts ) )
    {
        if( synk_lbts.nactive <= 0 )
        {
            Synk_InitiateLBTS( SimTime::MAX_TIME );
            synk_lbts.tot_stopping_lbts++;
        }
        Synk_Tick();
    }

if(0){//BGL
    MUSDBG( 1, "Final barrier" );
    FML_Barrier();

    if(Simulator::sim()->num_feds()>1)
    {
        TIMER_TYPE t1,t2;
        double dt;
        TIMER_NOW(t1);
        do
        {
            FM_extract(~0);
            TIMER_NOW(t2);
            dt=TIMER_DIFF(t2,t1);
        }while(dt<2/*secs*/);
    }
    MUSDBG( 1, "Stopped" );
}//XXX
    TM_Finalize();
    FM_finalize();
    if(Synk_nodeid < _printdbgmaxfedid)TM_PrintStats();
}
/*---------------------------------------------------------------------------*/
void Synk_TopoAddDest( int dest_pe, const SimTime &la )
{
    TM_Time tm_la; ts_musik2synk( tm_la, la );
    TM_TopoAddReceiver( dest_pe, tm_la );
}
/*---------------------------------------------------------------------------*/
void Synk_TopoAddSrc( int src_pe )
{
    TM_TopoAddSender( src_pe );
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
const string *SimEventBase::name( void ) const
{
    static string *s = 0;
    return s ? s : (s=new string( "$:" ));
}

/*---------------------------------------------------------------------------*/
/* Returns whether or not this event is a subclass of given event "type"     */
/*---------------------------------------------------------------------------*/
unsigned long SimEventBase::bytes_alloced_sofar = 0;
bool SimEventBase::isa( const string &s ) const
{
    bool s_is_prefix_of_my_name = name()->find( s ) == 0;
    return s_is_prefix_of_my_name;
}

/*---------------------------------------------------------------------------*/
ostream &SimEventBase::operator>>( ostream &out ) const
{
    out <<data._src<<"->"<<data._dest<<"@"<<T();
    if(lrts()!=T()) out<<"|"<<lrts();
    return out;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
ostream &MicroProcess::operator>>( ostream &out ) const
{
    return out;
}
/*---------------------------------------------------------------------------*/
ostream &SimProcessBase::operator>>( ostream &out ) const
{
    MicroProcess::operator>>( out );
    return out <<PID()<<(can_undo?" Opt":" Con")<<
                 " LA="<<min_la<<
                 " PEL="<<pel<<
                 "        FEL#="<<fel.num()<<
                 " C="<<ects()<<
                 " P="<<epts()<<
                 " E="<<eets()<<
                 ""
                 ;
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::introspect( const char *str ) const
{
    int intensity = _enslvl, badi = -1;
    ENSURE(0, (badi=fel.introspect(intensity))<0,
           "fel "<<badi<<" "<<PID()<<" "<<str<<*this);

    const SimEventBase *badev = 0;
    ENSURE(0, (badev=pel.introspect(intensity))==0,
           "pel "<<*badev<<" "<<PID()<<" "<<str<<*this);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static TIMER_TYPE trace_t1, trace_t2, trace_wct0; //XXX Assumes single threaded
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_prefix( void )
{
    if( !params.trace.generate ) return;
    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        TIMER_TYPE wct; TIMER_NOW(wct);
        double dwct = TIMER_DIFF(wct,trace_wct0)*1e6;
        out << setprecision(10) << (dwct < 0 ? 0 : dwct) << "\t" << fed_id() << "\t" << glbts.ts;
    }
    else
    {
        /*Do nothing*/
    }
}
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_init_start( void )
{
    if( !params.trace.generate ) return;

    ENSURE( 0, !params.trace.initialized, "" );
    char *fname = new char[strlen(params.trace.filename_prefix)+100];
    sprintf( fname, "%s-%ld.%s", params.trace.filename_prefix, fed_id(),
             (params.trace.flatfile ? "log" : "xml") );
    params.trace.tstream.open( fname );
    ENSURE(0, params.trace.tstream, "Can't create trace \""<<fname<<"\"");
    params.trace.initialized = true;

    TIMER_NOW(trace_wct0);

    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(this);
        out << "\t" << "initstart" << endl;
    }
    else
    {
        params.trace.tstream << "<TRACE>" << endl;
    }
}
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_init_end( void )
{
    if( !params.trace.generate ) return;
    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(this);
        out << "\t" << "initend" << endl;
    }
    else
    {
        /*Do nothing*/
    }
}
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_wrapup_start( void )
{
    if( !params.trace.generate ) return;
    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        /*Do nothing*/
    }
    else
    {
        ENSURE( 0, params.trace.initialized, "" );
        params.trace.tstream << endl << "</TRACE>" << endl;
        params.trace.tstream.close();
        params.trace.initialized = false;
    }
}
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_wrapup_end( void )
{
    if( !params.trace.generate ) return;
    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(this);
        out << "\t" << "wrapup" << endl;
    }
    else
    {
        /*Do nothing*/
    }
}
/*---------------------------------------------------------------------------*/
void MicroKernel::trace_sim_addmp( MicroProcess *mp )
{
    if( !params.trace.generate ) return;
    ostream &out = params.trace.tstream;
    if( params.trace.flatfile )
    {
        if( mp->PID().loc_id >= 0 )
        {
            TRACE_SIM_PREFIX(this);
            out << "\t" << "addlp\t" << mp->PID().loc_id << endl;
        }
    }
    else
    {
        /*Do nothing*/
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_process_prefix( void )
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    if( muk->params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(muk);
        out << "\tprocess\t" << PID().loc_id << "\t" << lvt.ts;
    }
    else
    {
        /*Do nothing*/
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_process_init_start( void )
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    if( muk->params.trace.flatfile )
    {
        /*Do nothing*/
    }
    else
    {
        out << "<INIT "
            << "PRO= " << PID().loc_id << " "
            << "FED= " << PID().fed_id << " "
            << ">"
            << endl;
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_process_init_end( void )
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    if( muk->params.trace.flatfile )
    {
        /*Do nothing*/
    }
    else
    {
        out << "</INIT>" << endl;
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_event_execute_start( const SimEvent *ev )
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    TIMER_NOW((trace_t1));
    if( muk->params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(muk);
        out << "\tactivate\t" << PID().loc_id << endl;
        TRACE_PROCESS_PREFIX(this);
        out << "\texec\t" << *ev->name() << endl;
    }
    else
    {
        out << "<EVENT "
            << "PRO= " << PID().loc_id << " "
            << "FED= " << PID().fed_id << " "
            << "SPRO= " << ev->source().loc_id << " "
            << "SFED= " << ev->source().fed_id << " "
            << "TS= " << ev->T().ts << " "
            << "TIE= " << ev->T().tie << " "
            << "NAME= \"" << *ev->name() << "\" "
            << ">"
            << endl;
        out << "\t<MODEL>" << endl << "\t\t";
        ev->trace( out );
        out << endl << "\t</MODEL>" << endl;
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_event_execute_end( const SimEvent *ev )
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    TIMER_NOW((trace_t2));
    double nsecs = TIMER_DIFF(trace_t2, trace_t1);
    if( muk->params.trace.flatfile )
    {
        TRACE_SIM_PREFIX(muk);
        out << "\tdeactivate\t" << PID().loc_id << endl;
    }
    else
    {
        out << "\t" << "<WC UNIT=\"us\"> " << nsecs*1e6 << " </WC>" << endl;
        out << "</EVENT>" << endl;
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_event_dispatch(const SimEvent *ev,
    const SimTime &send_ts, const SimTime &dt)
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    if( muk->params.trace.flatfile )
    {
        TRACE_PROCESS_PREFIX(this);
        out << "\tsend\t" << ev->destination().fed_id
            << "\t" << ev->destination().loc_id
            << "\t" << dt.ts
            << "\t" << *ev->name() << endl;
    }
    else
    {
        out << "\t" << "<SEND "
            << "PRO= " << ev->destination().loc_id << " "
            << "FED= " << ev->destination().fed_id << " "
            << "SENDTS= " << send_ts.ts << " "
            << "DT= " << dt.ts << " "
            << "TS= " << ev->T().ts << " "
            << "TIE= " << ev->T().tie << " "
            << "NAME= \"" << *ev->name() << "\" "
            << "></SEND>" << endl;
    }
}
/*---------------------------------------------------------------------------*/
void SimProcessBase::trace_event_rollback(const SimEventBase *rbev)
{
    MicroKernel *muk = MicroKernel::muk();
    if( !muk->params.trace.generate ) return;
    ostream &out = muk->params.trace.tstream;
    if( muk->params.trace.flatfile )
    {
        TRACE_PROCESS_PREFIX(this);
        out << "\trollback\t" << *rbev->name()
            << "\t" << rbev->T().ts << endl;
    }
    else
    {
        out << "\t" << "<ROLLBACK "
            << "PRO= " << rbev->data._dest.loc_id << " "
            << "FED= " << rbev->data._dest.fed_id << " "
            << "RBTS= " << rbev->T().ts << " "
            << "NAME= \"" << *rbev->name() << "\" "
            << "></ROLLBACK>" << endl;
    }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
class Reflector
{
    public: typedef string SimEventName;
    public: typedef vector<SimProcess *> ProcessVector;
    public: typedef map<SimEventName, ProcessVector> EventSubscriptionMap;

    public: Reflector( const string *n ) : name(*n) {}
    public: virtual ~Reflector() {}

    public: EventSubscriptionMap emap;
    public: string name;
};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
class KernelProcessBase : public MicroProcess
{
    public: KernelProcessBase( void )
    {
        is_kernel = true;
    }

    protected: virtual const SimTime &rlbts(void) const
    {
        return SimTime::MAX_TIME;
    }

    protected: virtual void lbts_advanced( const SimTime &new_lbts)
    {
    }
};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#define LESS(e1,e2) ((e1)->data.retract_ts < (e2)->data.retract_ts)
DEFINE_PQ(SimEventBase, retract_ts, epqi)
typedef PQ_CLASSNAME(HeapPQ, SimEventBase, retract_ts, epqi) LRTSEventHeapPQ;
typedef LRTSEventHeapPQ UnsentEventHeap;
#undef LESS

/*---------------------------------------------------------------------------*/
typedef ProcessedEventListBase SentEventList; //Ordered by e->lrts()
typedef FutureEventList SentEventHeap; //Ordered by e->T()

/*---------------------------------------------------------------------------*/
class RemoteFederateProcess : public KernelProcessBase
{
    public: RemoteFederateProcess( const SimFedID &f ) :
        fed_id(f), event_counter(0)
    {
        MUSDBG( 2, "RemoteFederateProcess "<<f<<" created." );
        SimFedID nrfp = Simulator::sim()->get_nfedperrfp();
        incoming_events = new EventMap[nrfp];
    }
    public: virtual ~RemoteFederateProcess() {}

    //------------------------------------------------------------------------
    protected: virtual const SimTime &ects( void ) const
            { if(ntimes_being_dirtied>0) return old_ects;
              SimTime &ts =(const_cast<RemoteFederateProcess*>(this))->ects_var;
              ts = ueh_top_ts();
if(0)ts.reduce_to(seh_top_ts());else//XXX Just for debugging the perf difference
              ts.reduce_to( sel_head_ts() );
              return ts; }
    protected: virtual const SimTime &epts( void ) const
            { if(ntimes_being_dirtied>0) return old_epts;
              return ueh_top_ts(); }
    protected: virtual const SimTime &pspan( void ) const
            { return SimTime::MAX_TIME; }
    protected: virtual const SimTime &eets( void ) const
            { if(ntimes_being_dirtied>0) return old_eets;
              SimTime &ts =(const_cast<RemoteFederateProcess*>(this))->eets_var;
              ts = ueh_top_ts();//XXX Returns lrts() instead of T(), but it's OK
              ts.reduce_to( seh_top_ts() );
              ts.increase_to( prev_lbts );//XXX In case an lrts() is in the past
             //XXX if(sel_head_ts().ts > 0) ts.reduce_to(sel_head_ts() - 1e-30);
              return ts;
            }
    private: virtual void new_lbts( const SimTime &ts )
            {
                prev_lbts.increase_to( ts );
            }

    protected: virtual long advance_optimistically( const SimTime &lbts,
                                                 const SimTime &limit_ts,
                                                 const int &limit_nevents )
            {
                new_lbts( lbts ); //Make note of implicit notification
                do_commits( lbts );
                int nevents = 0;
                while( ueh_top_ts() <= limit_ts && nevents < limit_nevents )
                {
                    SimEventBase *ev = ueh.pop();
                    if( !ev || !try_to_send( ev, true ) ) break;
                    nevents++;
                }
                do_commits( lbts );
                return nevents;
            }

    protected: virtual const SimTime &enqueue( SimEventBase *e,
                                               const SimTime &lbts );
    protected: virtual void dequeue( SimEventBase *e );

    //------------------------------------------------------------------------
    private: virtual const SimTime &ueh_top_ts( void ) const
            { const SimEventBase *e = ueh.peek();
              return e ? e->lrts() : SimTime::MAX_TIME; }
    private: virtual const SimTime &sel_head_ts( void ) const
            { const SimEventBase *e = sel.peek_head();
              return e ? e->lrts() : SimTime::MAX_TIME; }
    private: virtual const SimTime &seh_top_ts( void ) const
            { const SimEventBase *e = seh.peek();
              return e ? e->T() : SimTime::MAX_TIME; }
    private: virtual void do_commits( const SimTime &committable_ts )
            {
                while( sel_head_ts() <= committable_ts )
                {
                    SimEventBase *ev = sel.del_head();
                    ENSURE( 2, ev || seh.num() == 0, "" );
                    if( !ev ) break;
                    seh.del( ev );
                    ENSURE( 2, sel.num() == seh.num(), "" );
                    ev->detach_from_clist_of_parent();
                    ENSURE( 1, !ev->has_aux() ||
                               !ev->aux()->dep.clist.peek_tail(), "" );
                    MUSDBG( 2, "RFP committing "<<*ev );
                    delete ev;
                }
            }

    //------------------------------------------------------------------------
    protected: virtual SimTime incoming( SimEventBase *e );
    protected: virtual void incoming( const SimEventBase::EventIDType &eid,
                                      const SimTime &ts, SimFedID src_fed );

    private: virtual bool try_to_send( SimEventBase *e, bool immediately );

    protected: int fed_id;
    protected: SimEventBase::EventIDType event_counter;

    private: UnsentEventHeap ueh; //(unsent & uncommitted, ordered by e->lrts())
    private: SentEventList sel;   //(sent but uncommitted, ordered by e->lrts())
    private: SentEventHeap seh;   //(sent but uncommitted, ordered by e->T())

    protected: typedef map<SimEventBase::EventIDType,SimEventBase *> EventMap;
    protected: EventMap *incoming_events;
    protected: SimTime prev_lbts;

    private: friend class MicroKernel;
};

/*---------------------------------------------------------------------------*/
bool RemoteFederateProcess::try_to_send( SimEventBase *e, bool immediately )
{
    SimFedID nrfp = Simulator::sim()->get_nfedperrfp();
    ENSURE(1, 0<=e->data._dest.fed_id-fed_id && e->data._dest.fed_id-fed_id<nrfp,
           fed_id<<" "<<e->data._dest.fed_id<<" "<<nrfp );

    //Send it out on the wire, if so requested
    bool sent = immediately ? Synk_FMSendEvent( e->data._dest.fed_id, e ) : false;

    if( !sent ) //Add back to unsent event heap
    {
        ueh.add( e );
    }
    else //Add to sent event list
    {
        //XXX Caveat: Linear search!
        //Search backward in SEL for right LRTS-order position
        const SimEventBase *procd_event = sel.peek_tail();
        for(; procd_event && procd_event->lrts() > e->lrts();
             procd_event = sel.peek_prev(procd_event) )
        {
        }

        //Insert it in its LRTS position
        sel.add_after( const_cast<SimEventBase*>(procd_event), e );
        seh.add( e );
        ENSURE( 2, sel.num() == seh.num(), "" );

        do_commits( prev_lbts ); //In case the one we sent is not retractable
    }

    return sent;
}

/*---------------------------------------------------------------------------*/
const SimTime &RemoteFederateProcess::enqueue( SimEventBase *e,
                                               const SimTime &lbts )
{
  before_dirtied();

    new_lbts( lbts );

    enqts_var = e->lrts(); //XXX Or should it be e->T()?

    e->set_eid( ++event_counter );

    bool immediately = true; //XXX CUSTOMIZE!
    try_to_send( e, immediately );

  after_dirtied();
  
    return enqts_var;
}

/*---------------------------------------------------------------------------*/
void RemoteFederateProcess::dequeue( SimEventBase *e )
{
  before_dirtied();

    SimFedID nrfp = Simulator::sim()->get_nfedperrfp();

    ENSURE(1, 0<=e->data._dest.fed_id-fed_id && e->data._dest.fed_id-fed_id<nrfp,
           fed_id<<" "<<e->data._dest.fed_id<<" "<<nrfp );

    //Event is in SEL if it has a prev or next list item, or is single-item list
    bool in_sel = ( e->pprev || e->pnext || e==sel.peek_head() );

    if( in_sel ) //It is in sent event list
    {
        //Send a retraction notice to other end
        Synk_FMSendRetract( e->data._dest.fed_id, e->EID(), e->T() );

        sel.del_elem( e );
        seh.del( e );
    }
    else //It is in fel heap
    {
        ueh.del( e );
    }

    ENSURE( 2, sel.num() == seh.num(), "" );

    e->detach_from_clist_of_parent();

  after_dirtied();
}

/*---------------------------------------------------------------------------*/
SimTime RemoteFederateProcess::incoming( SimEventBase *e )
{
    SimFedID src_fed = e->data._src.fed_id;
    SimFedID my_fed = Simulator::sim()->fed_id();
    ENSURE( 2, src_fed != my_fed, "" );
    e->reset_dep();
    SimTime in_ts = SimTime::MAX_TIME;

    SimFedID nrfp = Simulator::sim()->get_nfedperrfp();
    int fi = src_fed-fed_id;

    if( incoming_events[fi].find(e->EID()) != incoming_events[fi].end() )
    {
        in_ts = e->T();
        delete e;
    }
    else
    {
        in_ts = add_to_dest( e->data._dest, e );
        incoming_events[fi][e->EID()] = e;
    }

    //XXX Need to flush the hashmap when LBTS goes past the timestamp
    bool flush_hashmap = false;
    if( flush_hashmap && ( incoming_events[fi].size() > 1000 ) )
    {
        for( EventMap::iterator it = incoming_events[fi].begin();
             it != incoming_events[fi].end(); )
        {
            if( (it->second)->T() >= prev_lbts )
            {
                it++;
            }
            else
            {
                EventMap::iterator delit = it;
                it++;
                incoming_events[fi].erase(delit);
            }
        }
    }

    return in_ts;
}

/*---------------------------------------------------------------------------*/
void RemoteFederateProcess::incoming( const SimEventBase::EventIDType &eid,
    const SimTime &ts, SimFedID src_fed )
{
    SimFedID nrfp = Simulator::sim()->get_nfedperrfp();
    int fi = src_fed-fed_id;
    SimEventBase *event = incoming_events[fi][eid];
    if( event )
    {
        ENSURE( 2, event, eid<<" "<<ts );
        ENSURE( 2, event->T() == ts, *event<<" "<<ts );
        remove_from_dest( event );
        delete event;
    }
    incoming_events[fi][eid] = 0; //XXX Need to flush sometime
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
class ReflectorProcess : public KernelProcessBase
{
    public: ReflectorProcess( const string &name ) : refname( name )
    {
        FAIL( "TBC" );
    }
    public: virtual ~ReflectorProcess( void ) {}
    protected: string refname;
};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
RemoteFederateProcess *MicroKernel::find_remote_federate_proc( int src_fed )
{
    SimLocalPID lpid = FID2RFPLID( src_fed ); //XXX
    SimPID pid( lpid, fed_id() );
    MicroProcess *proc = ID2MP( pid );
    RemoteFederateProcess *rfp = (RemoteFederateProcess *)proc;
    return rfp;
}

/*---------------------------------------------------------------------------*/
SimTime MicroKernel::incoming_remote_event( SimEventBase *e, int src_fed )
{
    MUSDBG( 2, "incoming_remote_event "<<e->T()<<" from " << src_fed );
    RemoteFederateProcess *rfp = find_remote_federate_proc( src_fed );
    ENSURE( 1, rfp, src_fed );
    return rfp->incoming( e );
}

/*---------------------------------------------------------------------------*/
void MicroKernel::incoming_remote_retract(
    const SimEventBase::EventIDType &eid, const SimTime &ts, int src_fed )
{
    MUSDBG( 2, "incoming_remote_retract "<<ts<<" from " << src_fed );
    RemoteFederateProcess *rfp = find_remote_federate_proc( src_fed );
    ENSURE( 1, rfp, src_fed );
    rfp->incoming( eid, ts, src_fed );
}

/*---------------------------------------------------------------------------*/
void MicroKernel::introspect( const SimPID &pid, const char *str ) const
{
    int intensity = _enslvl, badi = -1;
    ENSURE(0, (badi=cts_pq.introspect(intensity))<0,
           "cts "<<badi<<" "<<pid<<" "<<str<<*this);
    ENSURE(0, (badi=pts_pq.introspect(intensity))<0,
           "pts "<<badi<<" "<<pid<<" "<<str<<*this);
    ENSURE(0, (badi=ets_pq.introspect(intensity))<0,
           "ets "<<badi<<" "<<pid<<" "<<str<<*this);
    ENSURE(0, eets().ts>=glbts.ts, eets()<<" "<<glbts<<" "<<*this);
    ENSURE(0, (badi=rlbts_pq.introspect(intensity))<0,
           "rlbts "<<badi<<" "<<pid<<" "<<str<<*this);
}

/*---------------------------------------------------------------------------*/
const SimTime MicroKernel::eets( void ) const
{
    const MicroProcess *spb = ets_pq.peek();
    SimTime ts = spb ? spb->eets() : SimTime::MAX_TIME;

    MUSDBG(5,"eets() pid= "<<(spb?spb->PID():SimPID::INVALID_PID)<<" ts= "<<ts);

    return ts;
}

/*---------------------------------------------------------------------------*/
ostream &MicroKernel::operator>>( ostream &out ) const
{
    return out << endl<<"--------------"<<endl<<
                  "GLBTS="<<glbts<<" TOTEV="<<get_estats().executed<<endl<<
                  "CTS_PQ={"<<cts_pq<<"}"<<endl<<
                  "PTS_PQ={"<<pts_pq<<"}"<<endl<<
                  "ETS_PQ={"<<ets_pq<<"}"<<endl<<
                  "RLBTS_PQ={"<<rlbts_pq<<"}"<<endl<<
                  "--------------"
                  ;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
SimReflectorID Simulator::find_reflector( const string &refname )
{
    SimReflectorID rid = INVALID_RID;
    ReflectorNameMap::iterator loc = refmap.find( refname );
    if( loc != refmap.end() ) rid = (loc->second)->PID().loc_id;
    return rid;
}

/*---------------------------------------------------------------------------*/
SimReflectorID Simulator::create_reflector( const string &refname,
                                            SimTime lookahead )
{
    SimReflectorID rid = sim()->find_reflector( refname );
    if( rid == INVALID_RID )
    {
        ReflectorProcess *rp = 0; //XXX TBC: new ReflectorProcess( refname );
        sim()->add_mp( rp );
        rid = rp->PID().loc_id;
        refmap.insert( ReflectorNameMap::value_type( refname, rp ) );
    }

    return rid;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
class KernelEvent : public SimEvent
{
    DEFINE_BASE_EVENT(Kernel, KernelEvent, SimEvent)
    protected: KernelEvent( void ) { is_kernel = true; }
    protected: KernelEvent( const KernelEvent &ke ) : SimEvent(ke) {}
};

/*---------------------------------------------------------------------------*/
class KEvent_Init : public KernelEvent
{
    DEFINE_LEAF_EVENT(Init, KEvent_Init, KernelEvent)
    public: KEvent_Init( void ){}
    public: KEvent_Init( const KEvent_Init &e ) : KernelEvent(e) {}
    public: virtual void kernel_execute( SimProcessBase *p )
            {
                TRACE_PROCESS_INIT_START(p);
                ((SimProcess *)p)->init();
                TRACE_PROCESS_INIT_END(p);
                MUSDBG( 2, "Initialized process " << p << " ID=" << p->PID() );
            }
};

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
Simulator::Simulator( void )
{
    set_debug_levels();
}

/*---------------------------------------------------------------------------*/
Simulator::~Simulator( void )
{
}

/*---------------------------------------------------------------------------*/
void Simulator::set_debug_levels( void )
{
    bool pcfg = _printcfg;
    long pdbgmaxid = _printdbgmaxfedid;
    char *simcfgstr = getenv("PRINTCONFIG");
    char *simdbgmaxstr = getenv("PRINTMAXFEDID");
    if( simcfgstr ) { pcfg = !strcmp(simcfgstr,"TRUE"); }
    if( simdbgmaxstr ) { pdbgmaxid = atol(simdbgmaxstr); }
    _printcfg = _printlscfg = pcfg;
    _printdbgmaxfedid = _printlsdbgmaxfmid = pdbgmaxid;
    
    char *simdbgstr = getenv("DEBUG");
    if( simdbgstr ) { _simdbg = atoi( simdbgstr ); }
    MUSDBG( 2, "Debug level= " << _simdbg );
    char *ensurestr = getenv("ENSURE");
    if( ensurestr ) { _enslvl = atoi( ensurestr ); }
    MUSDBG( 2, "Ensure level= " << _enslvl );
}

/*---------------------------------------------------------------------------*/
void MicroKernel::pre_init( int *pac, char ***pav )
{
    Synk_PreInit( pac, pav );
}

/*---------------------------------------------------------------------------*/
void MicroKernel::init( const char *stdout_fname )
{
    ENSURE( 0, status == CONSTRUCTED, status );
    status = INITIALIZING;

    Synk_Init( stdout_fname );

    if( fed_id() >= _printdbgmaxfedid )
    {
        _printcfg = false;
        _printlscfg = false;
        _simdbg = -1;
    }

    TRACE_SIM_INIT_START(this);

    SimFedID k = (  num_feds() <     4 ?    1 :
                   (num_feds() <  1024 ?   10 :
                   (num_feds() < 32768 ?  100 :
                                         1000 ) ) ); //XXX
    if(getenv("NFEDPERRFP")) k = atoi(getenv("NFEDPERRFP"));
    SIMCFG( "NFEDPERRFP", k, "#federates per remote fed process" );
    set_nfedperrfp( k );
    SimFedID num_rfps = (num_feds()-1)/get_nfedperrfp() + 1;
    MUSDBG( 2, fed_id()<<": Creating "<<k<<" fed/rfp ("<<num_rfps<<" RFPs)" );
    for( SimFedID fed = 0; fed < num_rfps; fed++ )
    {
        RemoteFederateProcess *fed_proc = new RemoteFederateProcess( fed*k );
        add_mp( fed_proc );
    }
    MUSDBG( 2, fed_id()<<": Created "<<k<<" fed/rfp ("<<num_rfps<<" RFPs)" );

    status = INITIALIZED;
}

/*---------------------------------------------------------------------------*/
const SimPID &MicroKernel::add_mp( MicroProcess *p )
{
    ENSURE( 0, p, "Process should exist!" );
    idmap.add( p, fed_id() );
    cts_pq.add( p );
    ets_pq.add( p );
    pts_pq.add( p );
    rlbts_pq.add( p );
    MUSDBG( 2, "Added process " << p << " ID=" << p->PID() );

    TRACE_SIM_ADDMP( this, p );

    return p->PID();
}

/*---------------------------------------------------------------------------*/
void MicroKernel::del_mp( MicroProcess *p )
{
    ENSURE( 0, p, "Process should exist!" );
    MUSDBG( 2, "Deleting process " << p << " ID=" << p->PID() );
    ENSURE( 1, idmap.get(p->PID().loc_id) == p, "["<<p->PID()<<"] mismatch" );

    rlbts_pq.del( p );
    pts_pq.del( p );
    ets_pq.del( p );
    cts_pq.del( p );
    idmap.del(p);
    delete p;
}

/*---------------------------------------------------------------------------*/
const SimPID &Simulator::add( SimProcess *p )
{
    add_mp( p );

    //Send self an event to initialize
    {
      p->before_dirtied();
        KEvent_Init *kevent = new KEvent_Init();
        p->send( p->PID(), kevent, SimTime(0) );
      p->after_dirtied();
    }

    return p->PID();
}

/*---------------------------------------------------------------------------*/
void Simulator::del( SimProcess *p )
{
    p->wrapup();
    del_mp(p);
}

/*---------------------------------------------------------------------------*/
void Simulator::report_status( const SimTime &dt, const SimTime &endt )
{
    report.dt = dt;
    report.next = glbts;
    report.end = endt;
}

/*---------------------------------------------------------------------------*/
MicroKernel::Parameters::Parameters( void )
{
    char *estr = 0;

    estr = getenv("BATCHSZ");
    batch_sz = !estr ? 1 : atoi(estr);

    estr = getenv("TRACKTIMING");
    timing.track = !estr ? false : (atoi(estr)!=0);
    timing.advance_wt = 0;

    estr = getenv("TRACEEVENTS");
    trace.generate = estr && !strcmp(estr, "true");
    estr = getenv("TRACEFLATFILE");
    trace.flatfile = estr && !strcmp(estr, "true");
    #if NOTRACING
#if !NODEBUG
        ENSURE( 0, !trace.generate, "Can't generate trace: compiled out." );
#endif
        trace.generate = false;
    #endif
    trace.initialized = false;
    estr = getenv("TRACEFILENAME");
    trace.filename_prefix = strdup( !estr ? "trace" : estr );
}

/*---------------------------------------------------------------------------*/
void MicroKernel::start( void )
{
    TRACE_SIM_INIT_END(this);

    SIMCFG( "DEBUG", _simdbg,
            "intensity of debug output" );
    SIMCFG( "ENSURE", _enslvl,
            "intensity of assertions" );
    SIMCFG( "BATCHSZ", params.batch_sz,
            "#events per sim loop iteration" );
    SIMCFG( "TRACKTIMING", (params.timing.track ? "true" : "false"),
            "time each event execution?" );
    SIMCFG( "TRACEEVENTS", (params.trace.generate ? "true" : "false"),
            "generate event trace?" );
    SIMCFG( "TRACEFLATFILE", (params.trace.flatfile ? "true" : "false"),
            "plain text for event trace?" );
    SIMCFG( "TRACEFILENAME", params.trace.filename_prefix,
            "tracefile prefix" );

    ENSURE( 0, status == INITIALIZED, status );
    status = STARTING;

    Synk_Start();
    if(fed_id()==0)MUSDBG( 0, "Simulator started." );

    TIMER_NOW( start_time_with_init );
    start_time = start_time_with_init;
    start_timer_started = false;

    status = STARTED;
}

/*---------------------------------------------------------------------------*/
long MicroKernel::advance_process( MicroProcess *spb, bool really_advance,
                                   const SimTime &limit_ts,
                                   bool optimistically,
                                   const SimTime &opt_limit_ts )
{
    long nevents = 0;
#define READJUST 1
#if !READJUST
    bool ctoadd=false, ptoadd=false, etoadd=false, rtoadd=false;
#endif /*!READJUST*/

    if( !spb ) return 0;

    TIMER_TYPE t1, t2;
    if( params.timing.track ) { TIMER_NOW(t1); }

    if(_simdbg>=1){introspect( spb->PID(), "before advance&del" );}

#if READJUST
    spb->before_dirtied();
#else /*READJUST*/
    if(spb->ects_pqi!=PQ_TAG_INVALID) {cts_pq.del( spb );   ctoadd=true;}
    if(spb->epts_pqi!=PQ_TAG_INVALID) {pts_pq.del( spb );   ptoadd=true;}
    if(spb->eets_pqi!=PQ_TAG_INVALID) {ets_pq.del( spb );   etoadd=true;}
    if(spb->rlbts_pqi!=PQ_TAG_INVALID){rlbts_pq.del( spb ); rtoadd=true;}
#endif /*READJUST*/

    if(_simdbg>=1){introspect( spb->PID(), "before advance" );}

    if( !really_advance ) //Only notify LBTS
    {
        spb->lbts_advanced( glbts );
    }
    else
    {
        nevents = optimistically ?
                  spb->advance_optimistically( limit_ts, opt_limit_ts, 1000 ) :
                  spb->advance( limit_ts );
    }

    if(_simdbg>=1){introspect( spb->PID(), "after advance" );}

#if READJUST
    spb->after_dirtied();
#else /*READJUST*/
    if(ctoadd)cts_pq.add( spb );
    if(ptoadd)pts_pq.add( spb );
    if(etoadd)ets_pq.add( spb );
    if(rtoadd)rlbts_pq.add( spb );
#endif /*READJUST*/

    if(_simdbg>=1){introspect( spb->PID(), "after advance&add" );}

    if( params.timing.track )
    {
        TIMER_NOW(t2);
        double wt = TIMER_DIFF( t2, t1 );
        params.timing.advance_wt += wt;
    }

    return nevents;
}

/*---------------------------------------------------------------------------*/
void MicroKernel::make_lbts_callbacks( void )
{
    ENSURE( 1, temp_mpvec.empty(), "" );
    int i = 0, n = 0;

    /*Determine which processes are eligible for lbts notification*/
    MicroProcess *lpb = 0;
    while( (lpb=rlbts_pq.top()) && lpb->rlbts()<=glbts )
    {
        temp_mpvec.push_back( lpb );
        rlbts_pq.del( lpb );
    }

    if( temp_mpvec.size() <=0 ) return;

    /*Remove the processes from all the priority queues*/
    for( i = 0, n = temp_mpvec.size(); i < n; i++ )
    {
        lpb = temp_mpvec[i];
        ENSURE( 2, lpb, "" );

        cts_pq.del( lpb );
        pts_pq.del( lpb );
        ets_pq.del( lpb );
    }

    /*Advance the LBTS for each of the removed processes*/
    for( i = 0, n = temp_mpvec.size(); i < n; i++ )
    {
        lpb = temp_mpvec[i];
        advance_process( lpb, false, glbts );
    }

    /*Add the processes back to all the priority queues*/
    for( i = 0, n = temp_mpvec.size(); i < n; i++ )
    {
        lpb = temp_mpvec[i];

        cts_pq.add( lpb );
        pts_pq.add( lpb );
        ets_pq.add( lpb );
        rlbts_pq.add( lpb );
    }

    /*Clear the temporary array*/
    temp_mpvec.clear();
    ENSURE( 1, temp_mpvec.empty(), "" );
}

/*---------------------------------------------------------------------------*/
const SimTime MicroKernel::run( const SimTime &max_t, unsigned long max_nevents)
{
    MUSDBG( 10, "Simulator run( " << max_t << ", " << max_nevents << " )" );

    ENSURE( 0, status == STARTED || status == RUNNING, status );
    status = RUNNING;

    for( unsigned long i = 0; i < max_nevents; )
    {
        long nevents = 0;

        MUSDBG( 5, "glbts " << glbts );
if(0)/*XXX*/if( synk_lbts.nactive <= 0 && i%1000==0 ) {Synk_Tick();} //Read in network buffers in case they get full in large runs...

        MicroProcess *cpb = cts_pq.top();
        SimTime min_commit_ts = cpb ? cpb->ects() : SimTime::MAX_TIME;
        SimTime min_emitable_ts = eets();

        MUSDBG( 5, "min_emitable_ts= " << min_emitable_ts );
        MUSDBG( 5, "min_commit_ts= " << min_commit_ts );
        if( glbts >= max_t && min_commit_ts >= glbts ) break;

        SimTime limit_ts( glbts );
        limit_ts.reduce_to( min_emitable_ts );

        if( min_commit_ts.ts > 0 && !start_timer_started )
        {
            TIMER_NOW( start_time );
            start_timer_started = true;
        }

        if( min_commit_ts <= glbts )
        {
            MUSDBG( 5, "conservative advance from " << min_commit_ts
                        << " to " << limit_ts );
            nevents += advance_process( cpb, true, limit_ts );
        }
        else
        {
            bool do_lbts = true, do_optimistic = true;

            MUSDBG( 5, "ETS_PQ={"<<ets_pq<<"}" );

            if( do_lbts ) { Synk_InitiateLBTS( min_emitable_ts ); }

            if( synk_lbts.nactive > 0 ) { Synk_Tick(); }

            if( synk_lbts.nactive <= 0 ) //Just got a new advance
            {
                ts_synk2musik( glbts, synk_lbts.ts );
                if( report.next < SimTime::MAX_TIME &&
                    glbts >= report.next )
                {
                    if( fed_id() < _printdbgmaxfedid )
                    {
                        print_stats( cout );
                    }
                    report.next += report.dt;
                }

                {
                    bool do_postlbts_expdelay = (num_feds() > 1);
                    if(do_postlbts_expdelay)
                    {
                        static double expdel = 0, maxexpdel = 0;
                        static const char *expdelstr = 0, *maxexpdelstr = 0;
                        if( !expdelstr )
                        {
                            expdelstr = getenv("MK_POSTLBTS_EXPDELAYSEC");
                            maxexpdelstr = getenv("MK_POSTLBTS_MAXEXPDELAYSEC");
                            if( !expdelstr )
                            {
                                expdelstr = "";
                                maxexpdelstr = "";
                            }
                            else
                            {
                                expdel = atof(expdelstr);
                                maxexpdel = maxexpdelstr ?
                                            atof(maxexpdelstr) : 10*expdel;
                                srand48(12345);
                            }
                            SIMCFG( "MK_POSTLBTS_EXPDELAYSEC", expdel,
                            "Exponentially distributed delay after each LBTS" );
                            SIMCFG( "MK_POSTLBTS_MAXEXPDELAYSEC", maxexpdel,
                            "Maximum delay after each LBTS" );
                        }
                        if( expdel > 0 )
                        {
                            double runif = drand48();
                            double randdelay = -expdel * log( runif );
                            if( randdelay > maxexpdel ) randdelay = maxexpdel;
                            useconds_t microsec = randdelay * 1e6;
cout<<"Sleep "<<microsec<<" microseconds"<<endl;
                            usleep( microsec );
                        }
                    }
                }

                make_lbts_callbacks();
                do_optimistic = (num_feds() <= 1);
            }

            if( do_optimistic )
            {
                for( short b = 0; b < params.batch_sz; b++ )
                {
MUSDBG(3,"b="<<b);//XXX
                    MicroProcess *ppb = pts_pq.top();
                    const SimTime &min_processable_ts =
                            ppb ? ppb->epts() : SimTime::MAX_TIME;
                    const SimTime &pspan =
                            ppb ? ppb->pspan() : SimTime::MAX_TIME;
MUSDBG(4,"optimistic mpts="<<min_processable_ts<<" ppb="<<*ppb);//XXX
                    if( min_processable_ts >= max_t ||
                        min_processable_ts >= SimTime::MAX_TIME ||
                        min_processable_ts > glbts+pspan )
                    {
MUSDBG(3,"Breaking optimistic batch");//XXX
                        break;
                    }
                    else
                    {
                        MicroProcess *ppb2 = pts_pq.top2();
                        SimTime min_processable_ts2 =
                                ppb2 ? ppb2->epts() : SimTime::MAX_TIME;
                        SimTime opt_limit( min_processable_ts*10.0 );
                        opt_limit.reduce_to( glbts+pspan );
                        if( min_processable_ts2 > opt_limit )
                        {
                            MUSDBG( 4, "Limiting optimism from "<<
                                    min_processable_ts2<<" to "<<opt_limit );
                            min_processable_ts2 = opt_limit;
                        }

                        MUSDBG( 3,"optimistic advance from "<<min_processable_ts
                                  << " to " << min_processable_ts2 );

                        nevents+=advance_process(ppb, true, limit_ts,
                                                 true,min_processable_ts2);
                    }
                }
            }
        }

        i += nevents;
    }

    MicroProcess *cpb = cts_pq.top();
    SimTime min_commit_ts = cpb ? cpb->ects() : SimTime::MAX_TIME;
    min_commit_ts.reduce_to( glbts );

    MUSDBG( 5, "Simulator::run() returning " << min_commit_ts );

    return min_commit_ts;
}

/*---------------------------------------------------------------------------*/
void MicroKernel::print_stats( ostream &out )
{
    TIMER_TYPE curr_time;
    TIMER_NOW( curr_time );
    double nsecs = TIMER_DIFF( curr_time, start_time );
    double memmb = (get_memusage()/1e6);
    if( report.peak_memmb < memmb ) report.peak_memmb = memmb;

    MUSDBGNNL(0,fed_id() << ": ");
    MUSDBGNNL(0, "$$:" << " LBTS= " << glbts.ts);
    if( report.dt < SimTime::MAX_TIME )
        { MUSDBGNNL(0, " %= " << (glbts.ts/report.end.ts)*100.0); }
    MUSDBGNNL(0, " #LBTS= " << synk_lbts.tot_lbts);
    MUSDBGNNL(0, " #Ev= " << get_estats().executed);
    MUSDBGNNL(0, " WClk= " << nsecs);
if(0)MUSDBGNNL(0, " Ev/sec= " << get_estats().executed/nsecs);
    MUSDBGNNL(0, " us/Ev= " << 1e6*nsecs/get_estats().executed);
    if( memmb > 0 ) MUSDBGNNL(0, " TotMB= " << memmb);
    MUSDBG(0, "");
}

/*---------------------------------------------------------------------------*/
void MicroKernel::stop( void )
{
    TRACE_SIM_WRAPUP_START(this);

    SimTime last_lbts = glbts;
    TIMER_NOW( stop_time );
    SimTime stop_lbts; ts_synk2musik(stop_lbts, synk_lbts.ts);

    ENSURE( 0, status == RUNNING, status );
    status = STOPPING;

    if(fed_id()==0)MUSDBG( 1, "Simulator::stop() GLBTS="<<glbts );

    Synk_Stop();
    synk_lbts.tot_lbts -= synk_lbts.tot_stopping_lbts;//Exclude artifact LBTS

    int high_pid = idmap.highest_pid();

    for( int pid = idmap.least_pid(), last_pid = high_pid;
         pid <= last_pid; pid++ )
    {
        MicroProcess *mp = idmap.get( pid );
        if( !mp ) continue;

        if( pid < 0 ) del_mp( mp );
        else del( (SimProcess*)mp ); //XXX
    }

    if( fed_id() < _printdbgmaxfedid )
    {
        print_stats( cout );
    }

    double nsecs = TIMER_DIFF( stop_time, start_time );
    if( fed_id() < _printdbgmaxfedid )
    {
#if !NODEBUG
    MUSDBG(0, fed_id() << ": -------------------------------------" );
    MUSDBG(0, fed_id() << ":    Sizeof SimProcess = " << sizeof(SimProcess));
    MUSDBG(0, fed_id() << ":      Sizeof SimEvent = " << sizeof(SimEvent) );
#endif
    MUSDBG(0, fed_id() << ": -------------------------------------" );
    MUSDBG(0, fed_id() << ":      Federate number = " << fed_id() );
    MUSDBG(0, fed_id() << ":      Total federates = " << num_feds() );
    MUSDBG(0, fed_id() << ":           Batch size = " << params.batch_sz );
    MUSDBG(0, fed_id() << ":        Max local PID = " << high_pid );
    MUSDBG(0, fed_id() << ": Num application LBTS = " << synk_lbts.tot_lbts);
    MUSDBG(0, fed_id() << ":    Num stopping LBTS = " << synk_lbts.tot_stopping_lbts );
    MUSDBG(0, fed_id() << ":   Last computed LBTS = " << last_lbts.ts );
    MUSDBG(0, fed_id() << ":         Total events = "<<get_estats().executed);
    MUSDBG(0, fed_id() << ":      Events per LBTS = "<<get_estats().executed*1.0/synk_lbts.tot_lbts);
    MUSDBG(0, fed_id() << ":      Elapsed seconds = " << nsecs );
    MUSDBG(0, fed_id() << ":        Events/second = " << get_estats().executed/nsecs);
    MUSDBG(0, fed_id() << ":   Microseconds/event = " << 1e6*nsecs/get_estats().executed );
    MUSDBGNNL(0, fed_id() << ":     Peak memory (MB) = " );
            if( report.peak_memmb <= 0 ) MUSDBG(0, "Unknown" );
            else MUSDBG(0, report.peak_memmb );
    MUSDBG(0, fed_id() << ": -------------------------------------" );

    unsigned long exed = get_estats().executed, cmed = get_estats().committed;
    unsigned long  rbed = get_estats().rolledback, sent = get_estats().sent;
    printf("%ld:     Executed events = %10lu\n", fed_id(), exed);
    printf("%ld:    Committed events = %10lu [ %5.2lf %% ]\n", fed_id(), cmed, (exed<=0?0:(cmed*100.0/exed)));
    printf("%ld:   Rolledback events = %10lu [ %5.2lf %% ]\n", fed_id(), rbed, (exed<=0?0:(rbed*100.0/exed)));
    cout << fed_id() << ": -------------------------------------" << endl;
    }

    #if MPI_AVAILABLE
      struct { long min, max, sum; } totex, totcm, totrb;
      #define RGET( _1, _2, _3 ) do{ \
          long inx = 0, outx = 0; \
          inx = _1; \
          int retcode = \
              MPI_Allreduce( &inx, &outx, 1, MPI_LONG, _2, MPI_COMM_WORLD ); \
          ENSURE( 0, retcode == MPI_SUCCESS, retcode ); \
          _3 = outx; \
          if( fed_id() < _printdbgmaxfedid ) \
          { \
              printf( " %12ld", outx ); \
          } \
        } while(0)
      #define MGET( _1, _2 ) do{ \
          if( fed_id() < _printdbgmaxfedid ) \
          { \
          printf( "Stats-%-12s", #_1 ); \
          } \
          RGET( get_estats()._1, MPI_MIN, _2.min ); \
          RGET( get_estats()._1, MPI_MAX, _2.max ); \
          RGET( get_estats()._1, MPI_SUM, _2.sum ); \
          double davg = _2.sum*1.0/num_feds(); \
          if( fed_id() < _printdbgmaxfedid ) \
          { \
          printf( " %12.2lf", davg ); \
          printf( " %+9.2lf %%", davg<=0?0:((_2.max-davg)/davg*100.0) ); \
          printf( " %+9.2lf %%", davg<=0?0:((_2.min-davg)/davg*100.0) ); \
          printf( "\n" ); \
          } \
        } while(0)
      /*------*/
      {
          if( fed_id() < _printdbgmaxfedid )
          {
          printf( "------------------\n" );
          printf( "Stats-%-12s %12s %12s %12s %12s %9s %9s\n", "x", "Min", "Max", "Sum", "Avg", "Avg+%", "Avg-%" );
          }
          MGET( executed, totex );
          MGET( committed, totcm );
          MGET( rolledback, totrb );
          if( fed_id() < _printdbgmaxfedid )
          {
          printf( "------------------\n" );
          }
          double cmpcnt = 0, rbpcnt = 0, exevrate = 0, cmevrate = 0;
          if( totex.sum > 0 )
          {
              exevrate = totex.sum / nsecs;
              cmevrate = totcm.sum / nsecs;
              cmpcnt = totcm.sum * 100.0 / totex.sum;
              rbpcnt = totrb.sum * 100.0 / totex.sum;
          }
          if( fed_id() < _printdbgmaxfedid )
          {
          printf( "Overall-Committed     %5.2lf %%\n", cmpcnt);
          printf( "Overall-Rolledback    %5.2lf %%\n", rbpcnt);
          printf( "Overall-ExecutedRate  %9.2lf MilEv/s\n", exevrate/1e6 );
          printf( "Overall-CommittedRate %9.2lf MilEv/s\n", cmevrate/1e6 );
          printf( "------------------\n" );
          }
      }
      /*------*/
      #undef MGET
      #undef RGET
    #endif /*MPI_AVAILABLE*/

    if( ( params.timing.track && get_estats().executed>0 ) &&
          ( fed_id() < _printdbgmaxfedid ) )
    {
    MUSDBG(0, fed_id() << ": Event processing seconds     = "
         << params.timing.advance_wt );
    MUSDBG(0, fed_id() << ": Non-event processing seconds = "
         << (nsecs-params.timing.advance_wt));
    MUSDBG(0, fed_id() << ": Actual microseconds/event    = "
         << (params.timing.advance_wt)*1e6/get_estats().executed );
    MUSDBG(0, fed_id() << ": Non-event microseconds/event = "
         << (nsecs-params.timing.advance_wt)*1e6/get_estats().executed );
    MUSDBG(0, fed_id() << ": -------------------------------------" );
    }

    status = STOPPED;

    TRACE_SIM_WRAPUP_END(this);

    #if MPI_AVAILABLE
    if(1)
    {
        MUSDBG(0,fed_id() << ": --- Final Barrier Started ----------");
        MPI_Barrier(MPI_COMM_WORLD);
        MUSDBG(0,fed_id() << ": --- Final Barrier Done ----------");
        MPI_Finalize();//BGL
        MUSDBG(0,fed_id() << ": --- MPI Finalized ----------");
    }
    #endif /*MPI_AVAILABLE*/
}

/*---------------------------------------------------------------------------*/
int MicroKernel::num_feds( void ) const { return Synk_numnodes; }
SimFedID MicroKernel::fed_id( void ) const { return Synk_nodeid; }

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
SimProcess::SimProcess( void )
{
}

/*---------------------------------------------------------------------------*/
SimProcess::~SimProcess( void )
{
}

/*---------------------------------------------------------------------------*/
void SimProcess::init( void ) {}

/*---------------------------------------------------------------------------*/
void SimProcess::wrapup( void ) {}

/*---------------------------------------------------------------------------*/
void SimProcess::enable_undo( bool mode, const SimTime &ra, const SimTime &rs )
{
  bool added_to_sim = (PID().loc_id >= 0);
  if(added_to_sim)before_dirtied();
    can_undo = mode;
    if( can_undo )
    {
        runahead = ra;
        resilience = rs;
    }
  if(added_to_sim)after_dirtied();
}

/*---------------------------------------------------------------------------*/
class KEvent_AddDest : public KernelEvent
{
    DEFINE_LEAF_EVENT(AddDest, KEvent_AddDest, KernelEvent)
    public: KEvent_AddDest( SimPID p, SimTime t ) : dest(p), la(t) {}
    public: KEvent_AddDest( const KEvent_AddDest &e ) :
                KernelEvent(e), dest(e.dest), la(e.la) {}
    public: virtual void kernel_execute( SimProcessBase *p )
                { ((SimProcess *)p)->add_dest( dest, la, SimTime(0) ); }
    private: SimPID dest;
    private: SimTime la;
};

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::add_dest( const SimPID &dest,
                const SimTime lookahead, const SimTime dt )
{
    SimEventID eid = 0;
    if( dt > 0 )
    {
        KEvent_AddDest *kevent = new KEvent_AddDest( dest, lookahead );
        eid = send( PID(), kevent, dt );
    }
    else
    {
        if(1)/*XXX*/
        {
            /*Skipping Synk_Topo schemes*/
        }
        else
        {
        if( dest.fed_id != sim()->fed_id() )
        {
            if( dest.fed_id != SimPID::ANY_FED_ID )
            {
                Synk_TopoAddDest( dest.fed_id, lookahead );
            }
            else
            {
                for( int i = 0, n = sim()->num_feds(); i < n; i++ )
                {
                    if( i != sim()->fed_id() )
                        Synk_TopoAddDest( i, lookahead );
                }
            }
        }
        }
        MUSDBG( 2, "Updating LA to min of " << min_la << " and " << lookahead );
        update_min_la( lookahead );
    }
    return eid;
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::del_dest( const SimPID &dest, const SimTime dt )
{
    SimEventID eid = 0;
    MUSDBG( 1,"Not yet implemented" ); //XXX
    return eid;
}

/*---------------------------------------------------------------------------*/
class KEvent_AddSrc : public KernelEvent
{
    DEFINE_LEAF_EVENT(AddSrc, KEvent_AddSrc, KernelEvent)
    public: KEvent_AddSrc( SimPID p ) : src(p) {}
    public: KEvent_AddSrc( const KEvent_AddSrc &e ) :
                KernelEvent(e), src(e.src) {}
    public: virtual void kernel_execute( SimProcessBase *p )
                { ((SimProcess *)p)->add_src( src, SimTime(0) ); }
    private: SimPID src;
};

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::add_src( const SimPID &src, const SimTime dt )
{
    SimEventID eid = 0;
    if( dt > 0 )
    {
        KEvent_AddSrc *kevent = new KEvent_AddSrc( src );
        eid = send( PID(), kevent, dt );
    }
    else
    {
        if( src.fed_id != sim()->fed_id() )
        {
            if( src.fed_id != SimPID::ANY_FED_ID )
            {
                Synk_TopoAddSrc( src.fed_id );
            }
            else
            {
                for( int i = 0, n = sim()->num_feds(); i < n; i++ )
                {
                    if( i != sim()->fed_id() )
                        Synk_TopoAddSrc( i );
                }
            }
        }
    }
    return eid;
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::del_src( const SimPID &src, const SimTime dt )
{
    SimEventID eid = 0;
    MUSDBG( 1,"Not yet implemented" ); //XXX
    return eid;
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::publish( const SimReflectorID &rid,
                       const SimTime lookahead, const SimTime dt )
{
    SimEventID eid = 0;
    FAIL( "To be completed" ); //XXX
    return eid;
    //Synk_MPublish( r->name.c_str(), rid );
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::unpublish( const SimReflectorID &rid, const SimTime dt )
{
    SimEventID eid = 0;
    FAIL( "To be completed" ); //XXX
    return eid;
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::subscribe( const SimReflectorID &rid, const SimTime dt )
{
    SimEventID eid = 0;
    FAIL( "To be completed" ); //XXX
    return eid;
    //Synk_MSubscribe( r->name.c_str(), rid );
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::unsubscribe( const SimReflectorID &rid, const SimTime dt)
{
    SimEventID eid = 0;
    FAIL( "To be completed" ); //XXX
    return eid;
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::send( const SimPID &to, SimEvent *e,
                             const SimTime &dt, const SimTime &rdt )
{
    return dispatch( to, e, dt, rdt );
}

/*---------------------------------------------------------------------------*/
SimEventID SimProcess::post( const SimReflectorID &rid, SimEvent *e,
                             const SimTime dt )
{
    SimEventID eid = 0;
    FAIL( "To be completed" ); //XXX
    return eid;
}

/*---------------------------------------------------------------------------*/
class KEvent_Timer : public KernelEvent
{
    DEFINE_LEAF_EVENT(Timer, KEvent_Timer, KernelEvent)
        public: KEvent_Timer( void *c ) : closure(c) {}
    public: KEvent_Timer( const KEvent_Timer &e ) : KernelEvent(e),
            closure(e.closure) {}
    public: virtual void kernel_execute( SimProcessBase *p )
                {
                    p->timedout( this, closure );
                }
    protected: void *closure;
};

/*---------------------------------------------------------------------------*/
class KEvent_RepeatingTimer : public KEvent_Timer
{
    DEFINE_LEAF_EVENT(RepeatingTimer, KEvent_RepeatingTimer, KEvent_Timer)
        public: KEvent_RepeatingTimer(void *c, const SimTime &dt) :
                KEvent_Timer(c), period(dt) {}
    public: KEvent_RepeatingTimer( const KEvent_RepeatingTimer &e ) :
                KEvent_Timer(e), period(e.period) {}
    public: virtual void kernel_execute( SimProcessBase *p )
                {
                    KEvent_Timer::kernel_execute( p );
                    p->set_timer( period, closure, true );
                }
    protected: SimTime period;
};

/*---------------------------------------------------------------------------*/
SimTimerID SimProcess::set_timer( const SimTime &dt,
    void *closure, bool repeating )
{
    KEvent_Timer *te = repeating ? new KEvent_RepeatingTimer(closure,dt) :
                                       new KEvent_Timer(closure);
    SimTimerID tid = send( PID(), te, dt );
    return tid;
}

/*---------------------------------------------------------------------------*/
SimEvent *SimProcess::retract( SimEventID eid )
{
    SimEvent *event = (SimEvent *)eid;
    undispatch( event );
    return event;
}

/*---------------------------------------------------------------------------*/
void SimProcess::retract_timer( SimTimerID tid )
{
    SimEventID eid = tid;
    SimEvent *event = retract( eid );
    delete event;
}

/*---------------------------------------------------------------------------*/
void SimProcess::timedout( SimTimerID timer_id, void *closure ) {}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
void CopyState::pack_to( void *buf ) { memcpy( buf, this, num_bytes() ); }
void CopyState::unpack_from(const void *buf){memcpy( this, buf, num_bytes() );}
void CopyState::freeing( const void *buf ) {}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
PeriodicSimProcess::PeriodicSimProcess( const SimTime &p ) { period = p; }

/*---------------------------------------------------------------------------*/
PeriodicSimProcess::~PeriodicSimProcess() {}

/*---------------------------------------------------------------------------*/
void PeriodicSimProcess::init( void ) { tid = set_timer( period ); }

/*---------------------------------------------------------------------------*/
void PeriodicSimProcess::wrapup( void ) { retract_timer( tid ); }

/*---------------------------------------------------------------------------*/
void PeriodicSimProcess::timedout( SimTimerID t, void *closure )
    { if( t == tid ) { tick(); tid = set_timer( period ); } }

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#if 0 /*XT3*/
       #define pthread_create( ptid, attr, start_routine, arg ) (int)0
       #define pthread_mutex_lock(_x) (void)0
       #define pthread_mutex_unlock(_x) (void)0
       #define pthread_cond_wait(_x,_y) (void)0
       #define pthread_cond_broadcast(_x) (void)0
       #define pthread_mutex_init(_x,_y) (void)0
       #define pthread_cond_init(_x,_y) (void)0
       typedef int pthread_t;
       typedef int pthread_cond_t;
       typedef int pthread_mutex_t;
       typedef int pthread_attr_t;
#else
#include <pthread.h>
#endif

/*---------------------------------------------------------------------------*/
#define WAIT_FOR_CONDITION( _mtx, _cvar, _pred )                             \
    do                                                                       \
    {                                                                        \
       pthread_mutex_lock( &(_mtx) );                                        \
       while( !( _pred ) )                                                   \
       {                                                                     \
           pthread_cond_wait( &(_cvar), &(_mtx) );                           \
       }                                                                     \
       pthread_mutex_unlock( &(_mtx) );                                      \
    }while(0)                                                                \

/*---------------------------------------------------------------------------*/
#define SIGNAL_CONDITION( _mtx, _cvar, _stmt )                               \
    do                                                                       \
    {                                                                        \
       pthread_mutex_lock( &(_mtx) );                                        \
       _stmt ;                                                               \
       pthread_cond_broadcast( &(_cvar) );                                   \
       pthread_mutex_unlock( &(_mtx) );                                      \
    }while(0)                                                                \

/*---------------------------------------------------------------------------*/
typedef enum
{
    SIM_THR_READY,
    SIM_THR_RUN,
    SIM_THR_RECEIVE,
    SIM_THR_TIMEDOUT,
    SIM_THR_STOP,
    SIM_THR_EXIT
} SimThreadFlag;

/*---------------------------------------------------------------------------*/
struct SimThreadData
{
    SimThreadData( void )
    {
        vacant = true;
        flag = SIM_THR_READY;
        process = 0;
        event = 0;
        const_event = 0;

        pthread_cond_init( &pcond, NULL );
        pthread_mutex_init( &pmtx, NULL );
        pthread_cond_init( &kcond, NULL );
        pthread_mutex_init( &kmtx, NULL );
    }

    bool vacant; //true if no simulation process is being run on this thread now
    SimThreadFlag flag;
    ThreadedSimProcess *process;
    const SimEvent *const_event;
    SimEvent *event;
    jmp_buf env;

    pthread_t tid;
    pthread_cond_t pcond;
    pthread_mutex_t pmtx;
    pthread_cond_t kcond;
    pthread_mutex_t kmtx;
};

/*---------------------------------------------------------------------------*/
static void *pmain( void *arg )
{
    bool done = false;
    SimThreadData *d = (SimThreadData *)arg;

    MUSDBG( 2, "pmain() started" );

    while( !done )
    {
    MUSDBG( 2, "pmain("<<d->tid<<") waiting for command flag" );

        WAIT_FOR_CONDITION( d->pmtx, d->pcond, d->flag != SIM_THR_READY );

    MUSDBG(2, "pmain("<<d->tid<<") executing command flag " << ((int)d->flag) );

        if( d->flag != SIM_THR_RUN ) { done = true; }
        else
        {
    MUSDBG( 2, "pmain("<<d->tid<<") executing RUN");

            ENSURE( 0, d->process, "Process must be specified" );
            switch( setjmp( d->env ) )
            {
                case 0: //Normal
                {
    MUSDBG( 3, "pmain("<<d->tid<<") calling run()");
                    d->process->run();
                    break;
                }
                default: //From longjmp()
                {
    MUSDBG( 2, "pmain("<<d->tid<<") returned from longjmp()");
                    break;
                }
            }

            { d->vacant = true; d->process->_data = 0; d->process = 0; }
        }

    MUSDBG( 2, "pmain("<<d->tid<<") ack'ing completion to kernel" );

        SIGNAL_CONDITION( d->kmtx, d->kcond, d->flag = SIM_THR_READY );
    }

    MUSDBG( 2, "pmain("<<d->tid<<") exiting" );
    return 0;
}

/*---------------------------------------------------------------------------*/
static SimThreadData *get_a_thread( void )
{
    #define MAXTHRS 10000
    static SimThreadData *thrs[MAXTHRS];
    static int nthrs = 0;
    int i = 0;

    MUSDBG( 2, "get_a_thread() finding a thread" );
    //Try to find an idle/vacant thread
    for( i = nthrs-1; i >= 0; --i )
    {
        ENSURE( 0, thrs[i], "There should be a thread here" );
        if( thrs[i]->vacant )
        {
            ENSURE(0,!thrs[i]->process, "Vacant thread should have no process");
            break;
        }
    }

    if( i >= 0 )
    {
        MUSDBG( 2, "get_a_thread() found an idle thread ID " << thrs[i]->tid );
        thrs[i]->vacant = false;
    }
    else //No vacant thread; create a new thread
    {
        ENSURE( 0, nthrs < MAXTHRS, "Increase maximum #threads" );
        MUSDBG( 2, "get_a_thread() creating new thread " );

        i = nthrs++;
        thrs[i] = new SimThreadData();

        pthread_t *ptid = &thrs[i]->tid;
        pthread_attr_t *attr = NULL;
        void *(*start_routine)(void *) = &pmain;
        void *arg = thrs[i];

        thrs[i]->vacant = false;

        int flag = pthread_create( ptid, attr, start_routine, arg );

        MUSDBG(2, "get_a_thread() created new thread with ID " << thrs[i]->tid);
        ENSURE(0,flag == 0, "Thread creation failed.";perror("pthread_create"));
    }

    return thrs[i];
}

/*---------------------------------------------------------------------------*/
ThreadedSimProcess::ThreadedSimProcess( void )
{
}

/*---------------------------------------------------------------------------*/
ThreadedSimProcess::~ThreadedSimProcess( void )
{
}

/*---------------------------------------------------------------------------*/
void ThreadedSimProcess::init( void )
{
    MUSDBG( 2, "Process #" << PID() << " init()" );

    set_soe(true);
    SimProcess::init();

    SimThreadData *d = get_a_thread();
    _data = d;

    MUSDBG( 2, "New process #" << PID() << " pthread TID = " << d->tid );

/*    if(0)sleep(1); //Give time for new thread to come up and suspend*/

    {
        MUSDBG(2,"ThreadedSimProcess::init("<<d->tid<<") signaling RUN flag");

        SIGNAL_CONDITION( d->pmtx, d->pcond,
                          d->flag = SIM_THR_RUN; d->process = this );

        MUSDBG(2,"ThreadedSimProcess::init("<<d->tid<<") awaiting READY flag");

        WAIT_FOR_CONDITION( d->kmtx, d->kcond, d->flag == SIM_THR_READY );
        ENSURE(0, !d->vacant, "Process shouldn't exit during init!" );
    }
}

/*---------------------------------------------------------------------------*/
void ThreadedSimProcess::execute( SimEvent *e )
{
    SimThreadData *d = (SimThreadData *)_data;
    if( d )
    {
        ENSURE(0,!d->event && !d->const_event, "No obsolete events must exist");

        d->event = e;

        SIGNAL_CONDITION( d->pmtx, d->pcond, d->flag = SIM_THR_RECEIVE );
        WAIT_FOR_CONDITION( d->kmtx, d->kcond, d->flag == SIM_THR_READY );

        if( d->event ) { d->event = 0; }

        ENSURE(0,!d->event && !d->const_event, "Event should be consumed" );
    }
}

/*---------------------------------------------------------------------------*/
void ThreadedSimProcess::timedout( SimTimerID timer_id, void *closure )
{
    SimThreadData *d = (SimThreadData *)_data;
    if( d )
    {
        ENSURE(0,!d->event && !d->const_event, "No obsolete events must exist");

        SIGNAL_CONDITION( d->pmtx, d->pcond, d->flag = SIM_THR_TIMEDOUT );
        WAIT_FOR_CONDITION( d->kmtx, d->kcond, d->flag == SIM_THR_READY );

        ENSURE(0,!d->event && !d->const_event, "Event should be consumed" );
    }
}

/*---------------------------------------------------------------------------*/
bool ThreadedSimProcess::filter( WaitContext *wc, bool wcdone )
{
    return wcdone; /* Accept whatever the wc wants to do */
}

/*---------------------------------------------------------------------------*/
void ThreadedSimProcess::wait( const SimTime &dt, WaitContext *wc )
{
    bool done = false;
    SimTimerID timer_id = 0;
    SimThreadData *d = (SimThreadData *)_data;

    ENSURE(0, dt >= 0 || wc, "Timeout and/or wait context required" );

    if( dt >= 0 ) timer_id = set_timer( dt );

    SIGNAL_CONDITION( d->kmtx, d->kcond, d->flag = SIM_THR_READY );
    do
    {
        WAIT_FOR_CONDITION( d->pmtx, d->pcond, d->flag != SIM_THR_READY );
        switch( d->flag )
        {
            case SIM_THR_RECEIVE:
            {
                MUSDBG(4, "P::wait("<<d->tid<<") got RECEIVE flag; continuing");

                ENSURE(0,d->event || d->const_event, "At least one required" );
                ENSURE(0,!(d->event && d->const_event), "At most one expected");

                if( !wc )
                {
                }
                else
                {
                    bool wcdone = wc->handle( d->const_event ? d->const_event : d->event,
                                              d->event );
                    done = filter( wc, wcdone );
                }
                d->const_event = d->event = 0;

                if( done )
                {
                    if( dt >= 0 ) { retract_timer( timer_id ); timer_id = 0; }
                }
                else
                {
                    /*Need to continue waiting*/
                    if( wc ) wc->reset();
                    SIGNAL_CONDITION( d->kmtx, d->kcond, d->flag=SIM_THR_READY);
                }
                break;
            }
            case SIM_THR_TIMEDOUT:
            {
                MUSDBG(4,"P::wait("<<d->tid<<") got TIMEDOUT flag; continuing");
                done = true;
                break;
            }
            case SIM_THR_STOP:
            {
                MUSDBG( 4, "P::wait("<<d->tid<<") got STOP/WRAPUP flag;" <<
                           "unrolling stack to finish run()" );
                longjmp( d->env, 1 );
                FAIL( "Stack unrolling makes this unreachable point" );
                break;
            }
            default:
            {
                FAIL( "Unexpected command flag " << ((int)d->flag) );
                break;
            }
        }
    }while(!done);
}

/*---------------------------------------------------------------------------*/
void ThreadedSimProcess::wrapup( void )
{
    SimThreadData *d = (SimThreadData *)_data;
    if( d )
    {
        SIGNAL_CONDITION( d->pmtx, d->pcond, d->flag = SIM_THR_STOP );
        WAIT_FOR_CONDITION( d->kmtx, d->kcond, d->flag == SIM_THR_READY );
        ENSURE(0,d->vacant && !d->process, "Thread should have been detached" );
    }

    _data = 0;
}

/*---------------------------------------------------------------------------*/
