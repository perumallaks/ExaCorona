/*---------------------------------------------------------------------------*/
/*! \file musik.h
 *  \brief Main interface to the simulator.
 *
 *  This is the only guaranteed interface to the simulator.  Please do not
 *  rely on any other declarations and/or definitions in other files.
 *  The main types are ::SimTime, ::SimPID, ::SimEvent,
 *  ::SimProcess and ::Simulator.
 *
 *  Author: Kalyan S. Perumalla
 */
/*---------------------------------------------------------------------------*/
#ifndef __MUSIK_H
#define __MUSIK_H

#include <stdlib.h>
#include <iostream>
using namespace std;

/*---------------------------------------------------------------------------*/
/*! \brief Global variable to control amount of runtime assertion checking.
 *
 * Set this variable to integral values from 0 to inf, to control the amount
 * of checking.  Higher values enable more checks.
 *
 *! \sa ENSURE()
 */
extern int _enslvl;
/*---------------------------------------------------------------------------*/
/*! \brief A useful assertion macro.
 *
 * If the specified condition does not hold, then executes the specified
 * action and exits.
 */
#define ALWAYS( _lvl, _condition, _action )                                  \
    if( ((_lvl) <= _enslvl) && !(_condition))                                \
    {                                                                        \
        cerr << __FILE__ << ":" << __LINE__ << ": Failed guarantee:" << endl;\
        cerr << #_condition << endl; cerr << _action; cerr << endl; exit(1); \
    } else /*Terminated by semicolon in the macro invocation*/
#if NODEBUG
#define ENSURE( _lvl, _condition, _action ) /*Do nothing*/((void)0)
#else
#define ENSURE( _lvl, _condition, _action ) ALWAYS( _lvl, _condition, _action )
#endif

/*---------------------------------------------------------------------------*/
/*! \brief A useful abort macro.
 *
 * Executes the specified action and exits.
 */
#define FAIL( _action ) ENSURE( 0, false, _action )

/*---------------------------------------------------------------------------*/
/*! \brief Global variable to control amount of debug output generated.
 *
 * Set this variable to integral values from 0 to inf, to generate
 * debug/status output.  Higher values generate more output.
 *
 *! \sa SIMDBG()
 */
extern int _simdbg; extern ostream *_simdbgstrm, *_simdbgstrmdefault, *_musdbgstrm;
/*---------------------------------------------------------------------------*/
/*! \brief A useful debugging macro.
 *
 * The specified output is generated only if the specified level is within
 * the current debug level.
 */
#if NODEBUG
#define SIMDBGSTRM(_strm) /*Do nothing*/((void)0)
#define SSIMDBG(level, etc) /*Do nothing*/((void)0)
#define SIMDBG(level, etc) /*Do nothing*/((void)0)
#define MUSDBG(level, etc) /*Do nothing*/((void)0)
#define MUSDBGNNL(level, etc) /*Do nothing*/((void)0)
#else
#define SIMDBGSTRM(_strm) do{_simdbgstrm = (_strm);}while(0)
#define SSIMDBG(nnl, level, etc)                                             \
    if( _simdbg >= (level) )                                                 \
    {                                                                        \
            if(_simdbg >= 4) (*_simdbgstrm)<<__FILE__<<":"<<__LINE__<<": ";  \
            (*_simdbgstrm) << etc;                                           \
            if(!(nnl)) (*_simdbgstrm) << endl;                               \
            SIMDBGSTRM(_simdbgstrmdefault);                                  \
    } else /*Terminated by semicolon in the macro invocation*/
#define SIMDBG(level, etc) do{SIMDBGSTRM(&cout);SSIMDBG(false,level,etc);}while(0)
#define SIMDBGNNL(level, etc) do{SIMDBGSTRM(&cout);SSIMDBG(true,level,etc);}while(0)
#define MUSDBG(level, etc) do{SIMDBGSTRM(_musdbgstrm);SSIMDBG(false,level,etc);}while(0)
#define MUSDBGNNL(level, etc) do{SIMDBGSTRM(_musdbgstrm);SSIMDBG(true,level,etc);}while(0)
#endif

/*---------------------------------------------------------------------------*/
class SimEventBase;
class SimProcessBase;
class MicroKernel;
class SimEvent;
class SimProcess;
class Simulator;

/*---------------------------------------------------------------------------*/
/*! \brief Event identifier.
 */
typedef SimEventBase *SimEventID;
/*! \brief Timer identifier.
 */
typedef SimEventID SimTimerID;
/*! \brief Locally unique identifier of a simulation process.
 */
typedef long SimLocalPID;
/*! \brief Federate identifier.  Federate is synonymous with simulator.
 */
typedef long SimFedID;
/*! \brief Reflector identifier.
 */
typedef SimLocalPID SimReflectorID;
/*! \brief An invalid reflector identifier.
 */
extern const SimReflectorID INVALID_RID;

/*---------------------------------------------------------------------------*/
/*! \brief Simulation time representation.
 *
 * Time is represented as a pair: ts and tie.  The latter is useful to break
 * ties among <EM>simulataneous</EM> events (events with the same time stamp).
 */
class SimTime
{
    public: SimTime( void ) : ts(0), tie(0) {}
    public: SimTime( double t ) : ts(t), tie(0) {}
    public: SimTime( double t, double e ) : ts(t), tie(e) {}
    public: SimTime( const SimTime &s ) : ts(s.ts), tie(s.tie) {}

    public: SimTime &operator=( const SimTime &s )
            { ts = s.ts; tie = s.tie; return *this; }
    public: SimTime &operator=( double t )
            { ts = t; tie = 0; return *this; }

    public: bool operator==( const SimTime &t2 ) const
            { return ts == t2.ts && tie == t2.tie; }
    public: bool operator!=( const SimTime &t2 ) const
            { return ts != t2.ts || tie != t2.tie; }
    public: bool operator<( const SimTime &t2 ) const
            { return ts < t2.ts || (ts == t2.ts && tie < t2.tie); }
    public: bool operator<=( const SimTime &t2 ) const
            { return ts <= t2.ts && (ts != t2.ts || tie <= t2.tie); }
    public: bool operator>( const SimTime &t2 ) const
            { return ts > t2.ts || (ts == t2.ts && tie > t2.tie); }
    public: bool operator>=( const SimTime &t2 ) const
            { return ts >= t2.ts && (ts != t2.ts || tie >= t2.tie); }
    public: SimTime operator+( const double dt ) const
            { return *this==SimTime::MAX_TIME ? *this : SimTime( ts+dt, tie ); }
    public: SimTime operator-( const SimTime &t2 ) const
            { return *this==SimTime::MAX_TIME || t2==SimTime::MAX_TIME ?
                      SimTime::MAX_TIME : SimTime( ts-t2.ts, tie-t2.tie ); }
    public: SimTime operator+( const SimTime &t2 ) const
            { return *this==SimTime::MAX_TIME || t2==SimTime::MAX_TIME ?
                      SimTime::MAX_TIME : SimTime( ts+t2.ts, tie+t2.tie ); }
    public: SimTime operator*( const double f ) const
            { return *this==SimTime::MAX_TIME ? *this : SimTime( ts*f, tie*f );}
    public: SimTime operator/( const double f ) const
            { return *this==SimTime::MAX_TIME ? *this : SimTime( ts/f, tie/f );}
    public: SimTime &operator+=( const SimTime &t )
            { return *this = *this + t; }
    public: SimTime &operator+=( const double f )
            { return *this = *this + f; }

    public: SimTime &reduce_to( const SimTime &t2 )
            { if( t2 < *this ) *this = t2; return *this; }
    public: SimTime &increase_to( const SimTime &t2 )
            { if( t2 > *this ) *this = t2; return *this; }

    public: static const SimTime &min( const SimTime &t1, const SimTime &t2 )
            { return t1 < t2 ? t1 : t2; }
    public: static const SimTime &max( const SimTime &t1, const SimTime &t2 )
            { return t1 > t2 ? t1 : t2; }

    public: double ts;
    public: double tie;

    public: static const double MAX_TS;
    public: static const double MAX_TIE;

    public: static const SimTime MAX_TIME;
    public: static const SimTime MIN_TIME;
    public: static const SimTime ZERO_TIME;
};
/*---------------------------------------------------------------------------*/
inline SimTime operator*( double d, const SimTime &t ) { return t*d; }
inline SimTime operator+( double d, const SimTime &t ) { return t+d; }
inline SimTime operator-( double d, const SimTime &t ) { return -1*(t-d); }
/*---------------------------------------------------------------------------*/
ostream &operator<<( ostream &out, const SimTime &t );

/*---------------------------------------------------------------------------*/
/*! \brief Simulation process identifier.
 *
 * Process identifier is a pair (a,b) where
 * b is the identifier of the federate (simulator) housing the process,
 * and a is the unique identifier of that process within that simulator.
 */
class SimPID
{
    public: SimLocalPID loc_id;
    public: SimFedID fed_id;

    public: SimPID( void ) :
                loc_id(INVALID_LOC_ID), fed_id(INVALID_FED_ID) {}
    public: SimPID( SimLocalPID lid, SimFedID fid ) :
                loc_id(lid), fed_id(fid) {}

    public: bool operator==( const SimPID &p ) const
            { return loc_id == p.loc_id && fed_id == p.fed_id; }
    public: bool operator!=( const SimPID &p ) const
            { return !( *this == p ); }

    public: static const SimLocalPID INVALID_LOC_ID;
    public: static const SimLocalPID ANY_LOC_ID;

    public: static const SimFedID INVALID_FED_ID;
    public: static const SimFedID LOCAL_FED_ID;
    public: static const SimFedID ANY_FED_ID;

    public: static const SimPID INVALID_PID;
    public: static const SimPID ANY_PID;
};
/*---------------------------------------------------------------------------*/
ostream &operator<<( ostream &out, const SimPID &pid );

/*---------------------------------------------------------------------------*/
/*! \brief Root class for automatically saved state of an optimistic process.
 *
 * To be documented.
 */
class CopyState
{
    public: CopyState( void ) {}
    public: virtual ~CopyState() {}
    public: virtual int num_bytes( void ) = 0;
    public: virtual int max_bytes( void ) = 0;
    public: virtual void pack_to( void *buf );
    public: virtual void unpack_from( const void *buf );
    public: virtual void freeing( const void *buf );
};

#include "musikpriv.h"

/*---------------------------------------------------------------------------*/
/*! \brief Simulation event exchanged by simulation processes.
 *
 * All exchanges between simulation processes is via sending/receiving SimEvent
 * objects.
 * \remarks
 *     The new and delete operators are overridden and specialized for
 *     SimEvent, to optimize runtime performance.
 */
class SimEvent : public SimEventBase
{
    public: SimEvent( void ) : SimEventBase() {}
    public: SimEvent( const SimEvent &e ) : SimEventBase( e ) {}
    public: virtual ~SimEvent( void ) {}

    public: virtual const SimTime &T( void ) const;
    public: virtual const SimPID &source( void ) const;
    public: virtual const SimPID &destination( void ) const;

    public: virtual SimEvent *duplicate( void ) const = 0;
    public: virtual ostream &trace( ostream &out ) const;
    public: virtual ostream &operator>>( ostream &out ) const;
};

/*---------------------------------------------------------------------------*/
/*! \brief Simulation process.
 */
class SimProcess : public SimProcessBase
{
    public: SimProcess( void );
    public: virtual ~SimProcess( void );

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by the kernel
    //They can be refined (augmented) by subclasses
    //-------------------------------------------------------------------------
    public: virtual void init( void );
    public: virtual void wrapup( void );

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by subclasses
    //It is best NOT to redefine/override in subclasses
    //-------------------------------------------------------------------------
    public: virtual Simulator *sim( void ) const; //!< Get simulator singleton
    public: virtual const SimPID &PID( void ) const; //!< Get my PID
    public: virtual const SimTime &now( void ) const; //!< Get current time

    //-------------------------------------------------------------------------
    /*! \brief Enables/disables optimistic execution
     *
     * Set mode to true to enable optimistic execution, false to disable.
     * The additional/optional arguments can be specified when mode=true.
     */
    public: virtual void enable_undo( bool mode,
                                    const SimTime &runahead = SimTime::MAX_TIME,
                                    const SimTime &resilience = 0 );

    //-------------------------------------------------------------------------
    public: virtual void set_state( CopyState *cst ) { copy_state = cst; }
    public: virtual const CopyState *get_state(void) const {return copy_state;}

    //-------------------------------------------------------------------------
    public: virtual SimEventID add_dest( const SimPID &dest,
                                         const SimTime lookahead=0,
                                         const SimTime dt=0 );
    public: virtual SimEventID del_dest( const SimPID &dest,
                                         const SimTime dt=0 );
    public: virtual SimEventID add_src( const SimPID &src,
                                        const SimTime dt=0 );
    public: virtual SimEventID del_src( const SimPID &src,
                                        const SimTime dt=0 );

    //-------------------------------------------------------------------------
    public: virtual SimEventID publish( const SimReflectorID &rid,
                                        const SimTime lookahead=0,
                                        const SimTime dt=0 );
    public: virtual SimEventID unpublish( const SimReflectorID &rid,
                                          const SimTime dt=0 );
    public: virtual SimEventID subscribe( const SimReflectorID &rid,
                                          const SimTime dt=0 );
    public: virtual SimEventID unsubscribe( const SimReflectorID &rid,
                                            const SimTime dt=0 );

    //-------------------------------------------------------------------------
    public: virtual SimEventID send( const SimPID &to, SimEvent *e,
                                     const SimTime &dt=SimTime::ZERO_TIME,
                                     const SimTime &rdt=SimTime::ZERO_TIME );
    public: virtual SimEventID post( const SimReflectorID &rid,
                                     SimEvent *e, const SimTime dt=0 );

    //-------------------------------------------------------------------------
    public: virtual SimEvent *retract( SimEventID eid );

    //-------------------------------------------------------------------------
    public: virtual SimTimerID set_timer( const SimTime &dt,
                                          void *closure=0,bool repeating=false);
    public: virtual void retract_timer( SimTimerID tid );

    //-------------------------------------------------------------------------
    //The following services are intended to be defined/overridden by subclasses
    //The base class/kernel invokes these at runtime to notify the subclasses
    //-------------------------------------------------------------------------
    public: virtual void execute( SimEvent *e ) = 0;
    public: virtual void timedout( SimTimerID timer_id, void *closure=0 );
};

/*---------------------------------------------------------------------------*/
/*! \brief An instantiatable simulation process.
 *
 * Applications should inherit from this class rather than from SimProcess.
 */
class NormalSimProcess : public SimProcess
{
    public: virtual void init( void ) {}
    public: virtual void execute( SimEvent *e ) {}
    public: virtual void wrapup( void ) {}
};

/*---------------------------------------------------------------------------*/
/*! \brief Simulation process with a periodic action.
 *
 * This is intended as a convenience class to model a periodic activity.
 * Applications should override the tick() method to code the periodic action.
 * The tick() method is automatically invoked every period time units.
 */
class PeriodicSimProcess : public SimProcess
{
    public: PeriodicSimProcess( const SimTime &period );
    public: virtual ~PeriodicSimProcess( void );

    //-------------------------------------------------------------------------
    //The following services are intended to be defined by subclasses
    //-------------------------------------------------------------------------
    public: virtual void tick( void ) = 0;

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by the kernel
    //If overridden, the subclasses must invoke these from the overrides
    //-------------------------------------------------------------------------
    public: virtual void init( void );
    public: virtual void wrapup( void );
    private: virtual void timedout( SimTimerID timer_id, void *closure=0 );
    private: virtual void execute( SimEvent *e ) {}

    //-------------------------------------------------------------------------
    //Internal implementation data
    //-------------------------------------------------------------------------
    private: SimTimerID tid;
    private: SimTime period;
};

/*---------------------------------------------------------------------------*/
/*! \brief Simulation process that maintains full stack context across events.
 *
 * A special process that is capable of maintaining full stack context even
 * across multiple event executions.  This is also known in the literature as a
 * <EM>process-oriented</EM> process.  The user can override the run() method
 * to specify the threaded activity for this process.  The wait() methods
 * can be used to wait to receive specific events or to wait for specific
 * periods of simulation time.
 */
class ThreadedSimProcess : public SimProcess
{
    public: ThreadedSimProcess( void );
    public: virtual ~ThreadedSimProcess( void );

    //-------------------------------------------------------------------------
    /*! \brief Context object used inside a ThreadedSimProcess.
     *
     * Used to store context information required during waiting in
     * a ThreadedSimProcess.
     */
    public: struct WaitContext
    {
        int type; const SimEvent *ce; SimEvent *e;
        WaitContext(int t=0) : type(t), ce(0), e(0) {}
        virtual ~WaitContext() {}
        virtual void reset( void ) { ce = e = 0; }
        virtual bool handle( const SimEvent *cx, SimEvent *x )
                         { ce=cx; e=x; return true; }
    };

    //-------------------------------------------------------------------------
    //The following services are intended to be defined/overridden by subclasses
    //-------------------------------------------------------------------------
    public: virtual void run( void ) = 0;
    public: virtual bool filter( WaitContext *context, bool wcdone );
    public: virtual void wait( const SimTime &dt, WaitContext *wc = 0 );
    public: virtual void wait( WaitContext *wc ) { wait( -1, wc ); }
    public: virtual void wait( void ) { WaitContext wc;  wait( &wc ); }

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by the kernel
    //It is best NOT to redefine/override in subclasses
    //-------------------------------------------------------------------------
    public: virtual void init( void );
    public: virtual void wrapup( void );
    protected: virtual void execute( SimEvent *e );
    protected: virtual void timedout( SimTimerID timer_id, void *closure=0 );

    //-------------------------------------------------------------------------
    //Internal implementation; not for public
    //-------------------------------------------------------------------------
    public: void *_data;
};

/*---------------------------------------------------------------------------*/
/*! \brief Simulator object embodying the entire simulation.
 *
 * Used to provide simulation-wide services, such as addition or deletion of
 * simulation processes.
 * \remarks
 * During simulation, exactly one instance of this object (singleton)
 * is required.  Simulation processes can be added/deleted only after the
 * simulator is created.
 */
class Simulator : public MicroKernel
{
    public: Simulator( void );
    public: virtual ~Simulator( void );

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by the application
    //-------------------------------------------------------------------------
    public: virtual void pre_init( int *pac, char ***pav );/*Call if using MPI*/
    public: virtual void init( const char *stdout_fname = 0 );
    public: virtual void start( void );
    public: virtual const SimTime run( const SimTime &max_t=SimTime::MAX_TIME,
                                       unsigned long max_nevents=0xefffffff );
    public: virtual void stop( void );

    //-------------------------------------------------------------------------
    public: static void set_debug_levels(void);/*<!Set ENSURE & SIMDBG levels*/
    public: void report_status(const SimTime &dt, const SimTime &endt);/*<!
                         Print progress every dt assuming endt is the endtime*/

    //-------------------------------------------------------------------------
    //The following services are intended for invocation by app and/or LPs
    //-------------------------------------------------------------------------
    public: virtual const SimPID &add( SimProcess *p );
    public: virtual void del( SimProcess *p );

    public: virtual SimReflectorID create_reflector( const string &ref_name,
                                                     SimTime lookahead=0 );
    public: virtual SimReflectorID find_reflector( const string &ref_name );

    public: virtual int num_feds( void ) const;
    public: virtual SimFedID fed_id( void ) const;

    public: static Simulator *sim(void);
};

/*---------------------------------------------------------------------------*/
int app_event_type( const SimEvent *ev );
int app_event_data_size( int evtype, const SimEvent *ev );
SimEvent *app_event_create( int evtype, char *buf );
void app_event_data_pack( int evtype, const SimEvent *ev, char *buf, int bufsz );
void app_event_data_unpack( int evtype, SimEvent *ev, const char *buf, int bufsz);

/*---------------------------------------------------------------------------*/
/* Inlined implementation.  Do not rely on this specific code.  Could change.*/
/*---------------------------------------------------------------------------*/
inline const SimTime &SimEvent::T( void ) const { return SimEventBase::T(); }
inline const SimPID &SimEvent::source( void ) const { return data._src; }
inline const SimPID &SimEvent::destination( void ) const { return data._dest; }
inline ostream &SimEvent::trace( ostream &out ) const
    { return SimEventBase::trace(out); }
inline ostream &SimEvent::operator>>( ostream &out ) const
    { return SimEventBase::operator>>( out ); }
/*---------------------------------------------------------------------------*/
inline Simulator *Simulator::sim(void)
    { return (Simulator*)MicroKernel::muk(); }
inline void Simulator::pre_init( int *pac, char ***pav )
    { MicroKernel::pre_init( pac, pav ); }
inline void Simulator::init( const char *fn ) { MicroKernel::init(fn); }
inline void Simulator::start( void ) { MicroKernel::start(); }
inline const SimTime Simulator::run( const SimTime &max_t,
                                     unsigned long max_nevents )
    { return MicroKernel::run( max_t, max_nevents ); }
inline void Simulator::stop( void ) { MicroKernel::stop(); }
inline int Simulator::num_feds( void ) const { return MicroKernel::num_feds(); }
inline SimFedID Simulator::fed_id( void ) const { return MicroKernel::fed_id();}
/*---------------------------------------------------------------------------*/
inline Simulator *SimProcess::sim( void ) const { return Simulator::sim(); }
inline const SimPID &SimProcess::PID( void )const{return SimProcessBase::PID();}
inline const SimTime &SimProcess::now( void ) const { return lvt; }
/*---------------------------------------------------------------------------*/
inline ostream &operator<<( ostream &out, const SimTime &t )
    { if(t.ts<SimTime::MAX_TS) out << t.ts; else out << "MAX_TS"; out<<":";
      if(t.tie<SimTime::MAX_TIE) out << t.tie; else out << "MAX_TIE";
      return out; }
/*---------------------------------------------------------------------------*/
inline ostream &operator<<( ostream &out, const SimPID &pid )
    { return out << "[" << pid.loc_id << "," << pid.fed_id << "]"; }

/*---------------------------------------------------------------------------*/
#endif /*__MUSIK_H*/
