/*---------------------------------------------------------------------------*/
/*! \file musikpriv.h
 *  \brief Internal implementation of the simulator.
 *
 *  The contents of this file represent a snapshot of a specific implementation
 *  of the simulator interface.
 *  \warning
 *  Do NOT rely, in any way whatsoever, on the contents of this file.  They
 *  can (and will) change across future versions, sometimes very dramatically.
 *
 *  Author: Kalyan S. Perumalla
 */
/*---------------------------------------------------------------------------*/
#ifndef __MUSIKPRIV_H
#define __MUSIKPRIV_H

#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <deque>
using namespace std;

/*---------------------------------------------------------------------------*/
//XXX #define _SIMDBG(level, etc) SIMDBG(level,etc)
extern long _printdbgmaxfedid;
extern bool _printcfg;
#define SIMCFG(_cfgstr,_val,_notes) \
  if(_printcfg){ \
    cout<<"CONFIG-PARAMETER: "<<_cfgstr<<" "<<_val<< \
        " #!Musik: \""<<_notes<<"\""<<endl; \
  }else

/*---------------------------------------------------------------------------*/
#if NOTRACING
  #define TRACE_SIM_PREFIX(s) ((void)0)
  #define TRACE_SIM_INIT_START(s) ((void)0)
  #define TRACE_SIM_ADDMP(s,lp) ((void)0)
  #define TRACE_SIM_INIT_END(s) ((void)0)
  #define TRACE_SIM_WRAPUP_START(s) ((void)0)
  #define TRACE_SIM_WRAPUP_END(s) ((void)0)
  #define TRACE_PROCESS_PREFIX(p) ((void)0)
  #define TRACE_PROCESS_INIT_START(p) ((void)0)
  #define TRACE_PROCESS_INIT_END(p) ((void)0)
  #define TRACE_EVENT_EXECUTE_START(e) ((void)0)
  #define TRACE_EVENT_EXECUTE_END(e) ((void)0)
  #define TRACE_EVENT_DISPATCH(e,_sts,_dt) ((void)0)
  #define TRACE_EVENT_ROLLBACK(e) ((void)0)
#else /*NOTRACING*/
  #define TRACE_SIM_PREFIX(s) (s)->trace_sim_prefix()
  #define TRACE_SIM_INIT_START(s) (s)->trace_sim_init_start()
  #define TRACE_SIM_ADDMP(s,lp) (s)->trace_sim_addmp(lp)
  #define TRACE_SIM_INIT_END(s) (s)->trace_sim_init_end()
  #define TRACE_SIM_WRAPUP_START(s) (s)->trace_sim_wrapup_start()
  #define TRACE_SIM_WRAPUP_END(s) (s)->trace_sim_wrapup_end()
  #define TRACE_PROCESS_PREFIX(p) (p)->trace_process_prefix()
  #define TRACE_PROCESS_INIT_START(p) (p)->trace_process_init_start()
  #define TRACE_PROCESS_INIT_END(p) (p)->trace_process_init_end()
  #define TRACE_EVENT_EXECUTE_START(e) trace_event_execute_start(e)
  #define TRACE_EVENT_EXECUTE_END(e) trace_event_execute_end(e)
  #define TRACE_EVENT_DISPATCH(e,_sts,_dt) trace_event_dispatch(e,_sts,_dt)
  #define TRACE_EVENT_ROLLBACK(e) trace_event_rollback(e)
#endif /*NOTRACING*/

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef long PQTagType;
const long PQ_TAG_INVALID = -1;
#define PQ_CLASSNAME( _prefix, _elem_type, _key_var, _index_var )             \
    _prefix ## _ ## _elem_type ## _ ## _key_var ## _ ## _index_var

/*---------------------------------------------------------------------------*/
#define PQ_FORWARD_DECLS( _elem_type, _key_var, _index_var )                  \
    class PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var );               \
    class PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )

/*---------------------------------------------------------------------------*/
#define DEFINE_PQ( _elem_type, _key_var, _index_var )                         \
                                                                              \
class PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var )                    \
{                                                                             \
    public: PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var)( void ){}     \
    public: virtual ~PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var)(){}  \
    public: virtual const _elem_type *peek( void ) const = 0;                 \
    public: virtual _elem_type *top( void ) = 0;                              \
    public: virtual _elem_type *top2( void ) = 0;                             \
    public: virtual _elem_type *pop( void ) = 0;                              \
    public: virtual _elem_type *elem( long i ) = 0;                           \
    public: virtual void add( _elem_type * ) = 0;                             \
    public: virtual void del( _elem_type * ) = 0;                             \
    public: virtual void readjust( _elem_type *, bool ) = 0;                  \
    public: virtual long num( void ) const = 0;                               \
    public: virtual void set_params( long init_maxsz,                         \
                                     double growth_fac, double shrink_fac )=0;\
    public: virtual ostream &operator>>( ostream & ) const = 0;               \
    public: virtual long introspect( int intensity ) const = 0;               \
};                                                                            \
inline ostream &operator<<( ostream &out, const PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var ) &pq ) { return pq >> out; } \
                                                                              \
/*--------------------------------------------------------------------------*/\
class PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var ) :              \
        public PQ_CLASSNAME( PQ, _elem_type, _key_var, _index_var )           \
{                                                                             \
    public: PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var)( void );  \
    public: virtual ~PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var)  \
                                 ( void );                                    \
    public: virtual const _elem_type *peek( void ) const;                     \
    public: virtual _elem_type *top( void );                                  \
    public: virtual _elem_type *top2( void );                                 \
    public: virtual _elem_type *pop( void );                                  \
    public: virtual _elem_type *elem( long i );                               \
    public: virtual void add( _elem_type * );                                 \
    public: virtual void del( _elem_type * );                                 \
    public: virtual void readjust( _elem_type *, bool );                      \
    public: virtual long num( void ) const;                                   \
    public: virtual void set_params( long init_max,                           \
                                     double growth_fac, double shrink_fac );  \
    public: virtual ostream &operator>>( ostream & ) const;                   \
    public: virtual long introspect( int intensity ) const;                   \
                                                                              \
    private: long n;                                                          \
    private: long max;                                                        \
    private: _elem_type **e;                                                  \
                                                                              \
    private: long initial_max;                                                \
    private: double growth_factor, shrink_factor;                             \
                                                                              \
    private: void exchange( long i, long j );                                 \
    private: void sift_down( long i );                                        \
    private: void percolate_up( long i );                                     \
    private: void del( long i );                                              \
    private: void shrink_if_needed( void );                                   \
};                                                                            \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline                                                                        \
       PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::             \
       PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )( void )       \
{                                                                             \
    n = max = 0;                                                              \
    e = 0;                                                                    \
                                                                              \
    initial_max = 100;                                                        \
    growth_factor = 2.0;                                                      \
    shrink_factor = 1.0;                                                      \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline                                                                        \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
       ~PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )( void )      \
{                                                                             \
    if( e ) delete [] e;                                                      \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        exchange( long i, long j )                                            \
{                                                                             \
    if( i == j ) return;                                                      \
                                                                              \
    _elem_type *ev = e[i];                                                    \
    e[i] = e[j];                                                              \
    e[j] = ev;                                                                \
                                                                              \
    e[i]-> _index_var = i;                                                    \
    e[j]-> _index_var = j;                                                    \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        sift_down( long i )                                                   \
{                                                                             \
    if( n <= 1 ) return;                                                      \
    long k = i, j, c1, c2;                                                    \
    do                                                                        \
    {                                                                         \
        j = k;                                                                \
        c1 = c2 = 2*k+1;                                                      \
        c2++;                                                                 \
        if( c1 < n && LESS( e[c1], e[k] ) ) k = c1;                           \
        if( c2 < n && LESS( e[c2], e[k] ) ) k = c2;                           \
        exchange( j, k );                                                     \
    }while( j != k );                                                         \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        percolate_up( long i )                                                \
{                                                                             \
    if( n <= 1 ) return;                                                      \
    long k = i, j, p;                                                         \
    do                                                                        \
    {                                                                         \
        j = k;                                                                \
        if( (p = (k+1)/2) )                                                   \
        {                                                                     \
            --p;                                                              \
            if( LESS( e[k], e[p] ) ) k = p;                                   \
        }                                                                     \
        exchange( j, k );                                                     \
    } while( j != k );                                                        \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        readjust( _elem_type *elem, bool value_decreased )                    \
{                                                                             \
    long i = elem-> _index_var;                                               \
    ENSURE( 1, i != PQ_TAG_INVALID, "pq-readjust "<<*this );                  \
    value_decreased ? percolate_up( i ) : sift_down( i );                     \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        shrink_if_needed( void )                                              \
{                                                                             \
    /*Shrink array if it has become too large*/                               \
    if( max > initial_max && shrink_factor>1.0 && n < long(max/shrink_factor))\
    {                                                                         \
        long old_max = max;                                                   \
        max = long(max/shrink_factor);                                        \
        if( max <= initial_max ) max = initial_max;                           \
        e = (_elem_type **) realloc(e, max*sizeof(_elem_type *));             \
        MUSDBG( 3, "Shrunk PQ from "<<old_max<<" to "<<max<<" elems." );      \
    }                                                                         \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        del( long i )                                                         \
{                                                                             \
    ENSURE( 1, n > 0, "" );                                                   \
    _elem_type *victim = e[i];                                                \
    if( false ) /*Do percolate/sift method*/                                  \
    {                                                                         \
        --n;                                                                  \
        exchange( i, n );                                                     \
        if( n > 0 )                                                           \
        {                                                                     \
            LESS(e[i], e[n]) ? percolate_up(i) : sift_down(i);                \
        }                                                                     \
    }                                                                         \
    else /*Do retiring method*/                                               \
    {                                                                         \
        long c1, c2, k = i;                                                   \
        do {                                                                  \
            c1 = c2 = 2*k+1;                                                  \
            c2++;                                                             \
            if( c1 >= n && c2 >= n ) break; /*No child exists: k is leaf*/    \
            if( c2 >= n ) /*Only c1 exists*/                                  \
            {                                                                 \
                exchange( k, c1 );                                            \
                k = c1;                                                       \
            } else if( c1 >= n ) /*Only c2 exists*/                           \
            {                                                                 \
                FAIL( "Impossible" );                                         \
            } else /*Both c1 and c2 exist*/                                   \
            {                                                                 \
                long j = (LESS(e[c1],e[c2]) ? c1 : c2);                       \
                exchange( k, j );                                             \
                k = j;                                                        \
            }                                                                 \
        } while(1);                                                           \
        i = k;                                                                \
        --n;                                                                  \
        exchange( i, n );                                                     \
        if( n > 0 )                                                           \
        {                                                                     \
            if( i == 0 )                                                      \
            {                                                                 \
                FAIL( "Impossible" );                                         \
            }                                                                 \
            else if( i == n )                                                 \
            {                                                                 \
                /*Do nothing*/                                                \
            }                                                                 \
            else                                                              \
            {                                                                 \
                percolate_up( i );                                            \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    ENSURE( 1, e[n] == victim, "" );                                          \
    e[n]-> _index_var = PQ_TAG_INVALID;                                       \
    shrink_if_needed();                                                       \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline const _elem_type *                                                     \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        peek( void ) const                                                    \
{                                                                             \
    return n <= 0 ? 0 : e[0];                                                 \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline _elem_type *                                                           \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        top( void )                                                           \
{                                                                             \
    return n <= 0 ? 0 : e[0];                                                 \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline _elem_type *                                                           \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        top2( void )                                                          \
{                                                                             \
    return (n<=1) ? 0 : ((n<=2) ? e[1] : (LESS(e[1],e[2]) ? e[1]: e[2]));     \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline _elem_type *                                                           \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        pop( void )                                                           \
{                                                                             \
    _elem_type *ev = 0;                                                       \
    if( n > 0 )                                                               \
    {                                                                         \
        ev = e[0];                                                            \
        del( long(0) );                                                       \
    }                                                                         \
    return ev;                                                                \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline _elem_type *                                                           \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        elem( long i )                                                        \
{                                                                             \
    _elem_type *ev = 0;                                                       \
    if( n > i )                                                               \
    {                                                                         \
        ev = e[i];                                                            \
    }                                                                         \
    return ev;                                                                \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        add( _elem_type *ev )                                                 \
{                                                                             \
    if( n >= max )                                                            \
    {                                                                         \
        void *p = e;                                                          \
        long old_max = max;                                                   \
        long a = (max = (max<=0 ? initial_max : long(growth_factor*max))) *   \
                         sizeof(_elem_type *);                                \
        e = (_elem_type **)(p ? realloc(p,a) : malloc(a));                    \
        MUSDBG( 3, "Expanded PQ from "<<old_max<<" to "<<max<<" elems." );    \
    }                                                                         \
    long i = n++;                                                             \
    e[i] = ev; ev-> _index_var = i;                                           \
    percolate_up( i );                                                        \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        del( _elem_type *ev )                                                 \
{                                                                             \
    long j = ev-> _index_var;                                                 \
    ENSURE(2, 0 <= j && j < n && e[j] == ev, "Bad index " << j << ", n " <<n);\
    del( j );                                                                 \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline long                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        num( void ) const                                                     \
{                                                                             \
    return n;                                                                 \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline void                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        set_params( long init_max, double growth_fac, double shrink_fac )     \
{                                                                             \
    initial_max = init_max;                                                   \
    growth_factor = growth_fac;                                               \
    shrink_factor = shrink_fac;                                               \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline ostream &                                                              \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        operator>>( ostream &out ) const                                      \
{                                                                             \
    out << endl;                                                              \
    for( long i = 0; i < n; i++ )                                             \
    {                                                                         \
        out << "    (" << i << "= " << *e[i] << ")" << endl;                  \
    }                                                                         \
    return out;                                                               \
}                                                                             \
                                                                              \
/*--------------------------------------------------------------------------*/\
inline long                                                                   \
        PQ_CLASSNAME( HeapPQ, _elem_type, _key_var, _index_var )::            \
        introspect( int intensity ) const                                     \
{                                                                             \
    long badi = -1;                                                           \
    if( badi<0 && intensity>=2 )                                              \
    {                                                                         \
        for( long i = 0; i < n; i++ )                                         \
        {                                                                     \
            if( !e[i] || e[i]-> _index_var != i )                             \
            {                                                                 \
                badi = i;                                                     \
                break;                                                        \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    if( badi<0 && intensity>=3 )                                              \
    {                                                                         \
        long c1 = 0, c2 = 0;                                                  \
        for( long i=0; badi<0 && i<n; i++ )                                   \
        {                                                                     \
            c1 = c2 = 2*i+1;                                                  \
            c2++;                                                             \
            if( c1 < n && LESS( e[c1], e[i] ) ) badi = c1;                    \
            else if( c2 < n && LESS( e[c2], e[i] ) ) badi = c2;               \
        }                                                                     \
    }                                                                         \
    return badi;                                                              \
}                                                                             \
                                                                              \
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
#define LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )      \
        _elem_type ## _unique_name           
#define LIST_FORWARD_DECLS( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )  \
        class LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var );
#define LIST_DECLARE( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )        \
class LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )        \
{                                                                             \
    public: LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )  \
                          ( void ) : head(0), tail(0), nv(0) {}               \
    public: virtual ~LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )() {} \
    public: virtual void add_as_head( _elem_type *e );                        \
    public: virtual void add_as_tail( _elem_type *e );                        \
    public: virtual void add_before( _elem_type *existing, _elem_type *toadd );\
    public: virtual void add_after( _elem_type *existing, _elem_type *toadd );\
    public: virtual _elem_type *del_head( void );                             \
    public: virtual _elem_type *del_tail( void );                             \
    public: virtual void del_elem( _elem_type *e );                           \
    public: virtual const _elem_type *peek_head( void ) const { return head; }\
    public: virtual const _elem_type *peek_tail( void ) const { return tail; }\
    public: virtual const _elem_type *peek_next( const _elem_type *e ) const; \
    public: virtual const _elem_type *peek_prev( const _elem_type *e ) const; \
    public: virtual bool is_empty( void ) const { return nv <= 0; }           \
    public: virtual long num( void ) const { return nv; }                     \
    public: virtual ostream &operator>>( ostream & ) const;                   \
    public: virtual const _elem_type *introspect( int intensity ) const;      \
    private: _elem_type *head, *tail;                                         \
    private: long nv;                                                         \
};                                                                            \
inline ostream &operator<<( ostream &out, const LIST_CLASSNAME( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var ) &lst ) { return lst >> out; }                \

#define LIST_DEFINE( _elem_type, _unique_name, _next_var, _prev_var, _cnext_var, _cprev_var )         \
    inline void LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         add_as_head( _elem_type *e )                                         \
                { e-> _prev_var = 0; e-> _next_var = head;                    \
                  if( head ) head-> _prev_var = e;                            \
                  if( !tail ) tail = e;                                       \
                  head = e;                                                   \
                  nv++;                                                       \
                }                                                             \
    inline void LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         add_as_tail( _elem_type *e )                                         \
                { e-> _next_var = 0; e-> _prev_var = tail;                    \
                  if( tail ) tail-> _next_var = e;                            \
                  if( !head ) head = e;                                       \
                  tail = e;                                                   \
                  nv++;                                                       \
                }                                                             \
    inline void LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         add_before( _elem_type *existing, _elem_type *toadd )                \
                { if( !head || existing == head ) { add_as_head( toadd ); }   \
                  else if( !existing ) { add_as_tail( toadd ); }              \
                  else {                                                      \
                      toadd-> _next_var = existing;                           \
                      toadd-> _prev_var = existing-> _prev_var;               \
                      existing-> _prev_var -> _next_var = toadd;              \
                      existing-> _prev_var = toadd;                           \
                      nv++;                                                   \
                  }                                                           \
                }                                                             \
    inline void LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         add_after( _elem_type *existing, _elem_type *toadd )                 \
                { if( !tail || existing == tail ) { add_as_tail( toadd ); }   \
                  else if( !existing ) { add_as_head( toadd ); }              \
                  else {                                                      \
                      toadd-> _prev_var = existing;                           \
                      toadd-> _next_var = existing-> _next_var;               \
                      existing-> _next_var -> _prev_var = toadd;              \
                      existing-> _next_var = toadd;                           \
                      nv++;                                                   \
                  }                                                           \
                }                                                             \
    inline _elem_type *                                                       \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         del_head( void )                                                     \
                { if( !head ) return 0;                                       \
                  _elem_type *e = head;                                       \
                  head = head-> _next_var;                                    \
                  if( !head ) tail = 0; else head-> _prev_var = 0;            \
                  e-> _next_var = e-> _prev_var = 0;                          \
                  nv--;                                                       \
                  return e;                                                   \
                }                                                             \
    inline _elem_type *                                                       \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         del_tail( void )                                                     \
                { if( !tail ) return 0;                                       \
                  _elem_type *e = tail;                                       \
                  tail = tail-> _prev_var;                                    \
                  if( !tail ) head = 0; else tail-> _next_var = 0;            \
                  e-> _next_var = e-> _prev_var = 0;                          \
                  nv--;                                                       \
                  return e;                                                   \
                }                                                             \
    inline void LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         del_elem( _elem_type *e )                                            \
                { if( e == head ) del_head();                                 \
                  else if( e == tail ) del_tail();                            \
                  else                                                        \
                  {                                                           \
                      e-> _prev_var-> _next_var = e-> _next_var;              \
                      e-> _next_var-> _prev_var = e-> _prev_var;              \
                      e-> _prev_var = e-> _next_var = 0;                      \
                      nv--;                                                   \
                  }                                                           \
                }                                                             \
    inline const _elem_type *                                                 \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         peek_next( const _elem_type *e ) const                               \
                { return !e ? 0 : e-> _cnext_var; }                           \
    inline const _elem_type *                                                 \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         peek_prev( const _elem_type *e ) const                               \
                { return !e ? 0 : e-> _cprev_var; }                           \
    inline const _elem_type *                                                 \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         introspect( int intensity ) const                                    \
                {                                                             \
                    if( intensity<=1 ) return 0;                              \
                                                                              \
                    if( head && head-> _cnext_var ) return head;              \
                    if( tail && head-> _cnext_var ) return tail;              \
                    if( head && !head-> _cnext_var && head != tail)return head;\
                    if( tail && !tail-> _cprev_var && tail != head)return tail;\
                    if( head && !head-> _cnext_var && num() != 1 ) return head;\
                    if( tail && !tail-> _cprev_var && num() != 1 ) return tail;\
                    if( num() == 0 && (head || tail) ) return head;           \
                                                                              \
                    if( intensity<=2 ) return 0;                              \
                                                                              \
                    for( const _elem_type *elem = peek_head(); elem;          \
                         elem = peek_next(elem) )                             \
                    {                                                         \
                        if( elem-> _cnext_var &&                              \
                            elem-> _cnext_var -> _cprev_var != elem )         \
                            return elem;                                      \
                        if( elem-> _cprev_var &&                              \
                            elem-> _cprev_var -> _cnext_var != elem )         \
                            return elem;                                      \
                    }                                                         \
                    return 0;                                                 \
                }                                                             \
    inline ostream &                                                          \
                LIST_CLASSNAME(_elem_type,_unique_name,_next_var, _prev_var, _cnext_var, _cprev_var)::\
         operator>>( ostream &out ) const                                     \
                {                                                             \
                    out << "[" << num() << "]={" << endl;                     \
                    for( const _elem_type *elem = peek_head(); elem;          \
                         elem = peek_next(elem) )                             \
                    {                                                         \
                        out << "        |" << *elem <<                        \
                            (elem-> _cnext_var?"|==>":"|")<<endl;             \
                    }                                                         \
                    out << "        }" << endl;                               \
                    return out;                                               \
                }                                                             \
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
struct FreeList
{
    FreeList( void ) : free_list(), nbuffers_to_alloc(0) {}
    deque<void *> free_list;
    unsigned long nbuffers_to_alloc;
};
typedef map<int, FreeList> FreeListMap; /*Indexed by roof(blocksize/10)*/

/*---------------------------------------------------------------------------*/
class KernelEvent;
PQ_FORWARD_DECLS(SimEventBase, recv_ts, epqi);

/*---------------------------------------------------------------------------*/
LIST_DECLARE(SimEventBase, Cause, aux()->dep.cnext, aux()->dep.cprev,
                                  caux()->dep.cnext, caux()->dep.cprev)
LIST_DECLARE(SimEventBase, Processed, pnext, pprev, pnext, pprev)
typedef LIST_CLASSNAME(SimEventBase, Cause, aux()->dep.cnext, aux()->dep.cprev,
                                           caux()->dep.cnext, caux()->dep.cprev)
        CausalEventList;

/*---------------------------------------------------------------------------*/
class KernelProcessBase;
class RemoteFederateProcess;

/*---------------------------------------------------------------------------*/
/*! \brief Dependency list for events.
 *
 * <B>Not intended as a supported API.  Subject to change.<B>
 */

/*---------------------------------------------------------------------------*/
/*! \brief Underlying support class for simulation events.
 *
 * <B>Not intended as a supported API.  Subject to change.<B>
 */
class SimEventBase
{
    public: typedef long EventIDType;
    public: struct Data
            {
                SimPID _src, _dest;
                SimTime recv_ts; /*Time at which destination must process this*/
                SimTime retract_ts; /*Latest time this event can be retracted*/
                EventIDType _eid;
            } data;
    public: SimEventBase( void );
    public: SimEventBase( const SimEventBase &e );
    public: virtual ~SimEventBase( void );

    public: virtual const SimTime &T( void ) const { return data.recv_ts; }
    public: virtual const SimTime &lrts( void ) const { return data.retract_ts; }

    public: virtual void set_src_dest( const SimPID &s, const SimPID &d )
                { data._src = s; data._dest = d; }
    public: virtual void set_time( const SimTime &t, const SimTime &rt )
                { data.recv_ts = t; data.retract_ts = rt; }
    public: virtual ostream &trace( ostream &out ) const { return out; }

    public: virtual const string *name( void ) const;
    protected: virtual bool isa( const string &n ) const;

    public: void *operator new( size_t sz );
    public: void *operator new( size_t sz, void *ptr );
    public: void operator delete( void *d, size_t sz );
    public: static void *get_buffer( size_t totsz );
    public: static unsigned long bytes_alloced_sofar;

    private: PQTagType epqi; /*For use in receiver's future event list*/

    private: bool is_kernel; /*Is this a kernel implementation event?*/
    public: bool is_kernel_event( void ) const { return is_kernel; }
    private: virtual void kernel_execute( SimProcessBase *p ) {}

    private: class Dependencies
    {
        public: Dependencies( void ) :
                cparent(0), cprev(0), cnext(0) {}
        public: void reset( void )
                { cparent = 0; cprev = cnext = 0; }

        public: CausalEventList clist; /*To track events gen'd by this event*/
        public: SimEventBase *cparent; /*Who generated me?*/
        public: SimEventBase *cprev, *cnext; /*To chain in clist of cparent*/
    };

    private: class AuxiliaryData
    {
        public: AuxiliaryData( void ) : dep(), hook1(0), hook2(0) {}

        public: Dependencies dep;
        public: void *hook1, *hook2; //Scratch "registers"
    };

    private: void set_eid(const EventIDType &id) { data._eid = id; }
    public: const EventIDType &EID(void) const { return data._eid; }

    private: AuxiliaryData *_aux;
    #define DYNAMIC_AUX 1 /*XXX*/
    #if !DYNAMIC_AUX
        AuxiliaryData _aux_static;
    #endif /*DYNAMIC_AUX*/
    private: bool has_aux( void ) const
    {
        return _aux!=0;
    }
    public: void erase_aux( void ) { _aux = 0; } //XXX
    private: void del_aux( void )
    {
    #if DYNAMIC_AUX
        if( _aux ) delete _aux;
    #endif /*DYNAMIC_AUX*/
        _aux = 0;
    }
    private: AuxiliaryData *aux( void )
    {
        if( !_aux ) _aux =
    #if DYNAMIC_AUX
            new AuxiliaryData();
        MUSDBG( 10, "Allocated aux" );
    #else /*DYNAMIC_AUX*/
            &_aux_static;
    #endif /*DYNAMIC_AUX*/
        ENSURE( 0, _aux, "" );
        return _aux;
    }
    private: const AuxiliaryData *caux( void ) const
    {
        return const_cast<SimEventBase*>(this)->aux();
    }

    private: SimEventBase *pprev, *pnext;/*To chain in _dest's procd list*/

    private: void reset_dep( void ) { if( has_aux() ) aux()->dep.reset(); }
    private: void detach_from_clist_of_parent( void );

    private: static FreeListMap free_map;

    private: void coerce_ts( const SimTime &ts, const SimTime &rts );

    private: friend class SimEvent;
    private: friend class KernelEvent;
    private: friend class MicroKernel;
    private: friend class Simulator;
    private: friend class KernelProcessBase;
    private: friend class RemoteFederateProcess;
    private: friend class MicroProcess;
    private: friend class SimProcessBase;
    private: friend class SimProcess;
    private: friend class PQ_CLASSNAME(HeapPQ, SimEventBase, recv_ts, epqi);
    private: friend class PQ_CLASSNAME(HeapPQ, SimEventBase, retract_ts, epqi);
    private: friend class LIST_CLASSNAME(SimEventBase, Cause,
                                   aux()->dep.cnext, aux()->dep.cprev,
                                   caux()->dep.cnext, caux()->dep.cprev);
    private: friend class LIST_CLASSNAME(SimEventBase, Processed,
                                   pnext, pprev, pnext, pprev);

    public: virtual ostream &operator>>( ostream &out ) const;
};
/*---------------------------------------------------------------------------*/
inline ostream &operator<<( ostream &out, const SimEventBase &ev )
    { return ev >> out; }

/*---------------------------------------------------------------------------*/
inline SimEventBase::SimEventBase( void ) :
    epqi(PQ_TAG_INVALID),
    is_kernel(false),
    pprev(0), pnext(0),
    _aux(0)
{
    data.recv_ts = 0;
    data.retract_ts = 0;
    data._eid = -1;
}

/*---------------------------------------------------------------------------*/
inline SimEventBase::SimEventBase( const SimEventBase &other ) :
    epqi(PQ_TAG_INVALID),
    is_kernel(false),
    pprev(0), pnext(0),
    _aux(0)
{
    data = other.data;
}

/*---------------------------------------------------------------------------*/
inline SimEventBase::~SimEventBase( void )
{
    del_aux();
}

/*---------------------------------------------------------------------------*/
inline void SimEventBase::detach_from_clist_of_parent( void )
{
    if( has_aux() && aux()->dep.cparent ) //Remove from parent's causal list
    {
        ENSURE( 2, aux()->dep.cparent->data._dest == data._src, *this<< " -- "<<
                  *aux()->dep.cparent<<": parent's dest must be child's src");
        long npeers = aux()->dep.cparent->aux()->dep.clist.num()-1;
        aux()->dep.cparent->aux()->dep.clist.del_elem( this );
        ENSURE( 2, aux()->dep.cparent->aux()->dep.clist.num() == npeers,
                   npeers<<" "<<aux()->dep.cparent->aux()->dep.clist.num()<<
                   " "<<*this<<" "<<*aux()->dep.cparent );
        aux()->dep.cparent = 0;
    }
}
inline void SimEventBase::coerce_ts( const SimTime &ts, const SimTime &rts )
{
    MUSDBG( 1, *this<<": Coercing TS "<<T()<<" to "<<ts );
    set_time( ts, rts );
}

/*---------------------------------------------------------------------------*/
#define _EVENT_NAME( _n, _p ) \
    public: virtual const string *name(void) const { static string *s = 0; \
          return s ? s : (s = new string((*_p::name())+#_n+":")); }
#define _EVENT_DUP( _e ) \
    public: virtual SimEvent *duplicate(void) const { return new _e( *this ); }
/*---------------------------------------------------------------------------*/
/* Use: class E : P { DEFINE_BASE_EVENT(N,E,P) ... }                         */
/* Use: class E : P { DEFINE_LEAF_EVENT(N,E,P) ... }                         */
/* E is in tree rooted at SimEvent.  N is E's "name".  P is E's parent.      */
/*---------------------------------------------------------------------------*/
#define DEFINE_BASE_EVENT( _n, _e, _p ) _EVENT_NAME( _n, _p )
#define DEFINE_LEAF_EVENT( _n, _e, _p ) _EVENT_NAME( _n, _p ) _EVENT_DUP( _e )
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
#define LESS(e1,e2) ((e1)->data.recv_ts < (e2)->data.recv_ts)
DEFINE_PQ(SimEventBase, recv_ts, epqi)
typedef PQ_CLASSNAME( HeapPQ, SimEventBase, recv_ts, epqi ) EventHeapPQ;
typedef EventHeapPQ FutureEventList;
#undef LESS
/*---------------------------------------------------------------------------*/
LIST_DEFINE(SimEventBase, Cause, aux()->dep.cnext, aux()->dep.cprev,
                                 caux()->dep.cnext, caux()->dep.cprev)
LIST_DEFINE(SimEventBase, Processed, pnext, pprev, pnext, pprev)
/*---------------------------------------------------------------------------*/
typedef LIST_CLASSNAME(SimEventBase, Processed, pnext, pprev,
                       pnext, pprev) ProcessedEventListBase;
class ProcessedEventList : public ProcessedEventListBase
{
    public: virtual const SimEventBase *introspect( int intensity ) const
    {
        const SimEventBase *badev = 0;
        badev = ProcessedEventListBase::introspect(intensity);
        if( !badev && intensity>=2 )
        {
            const SimEventBase *pev1 = peek_head(), *pev2 = peek_next(pev1);
            while( !badev && pev1 && pev2 )
            {
                if( pev1->T() <= pev2->T() )
                {
                    pev1 = pev2;
                    pev2 = peek_next(pev2);
                }
                else
                {
                    badev = pev1;
                }
            }
        }
        return badev;
    }
};
inline ostream &operator<<( ostream &out, const ProcessedEventList &lst )
    { return lst >> out; }

/*---------------------------------------------------------------------------*/
class PIDMap;
class MicroProcess;
PQ_FORWARD_DECLS(MicroProcess, ects, ects_pqi);
PQ_FORWARD_DECLS(MicroProcess, epts, epts_pqi);
PQ_FORWARD_DECLS(MicroProcess, eets, eets_pqi);
PQ_FORWARD_DECLS(MicroProcess, rlbts, rlbts_pqi);

/*---------------------------------------------------------------------------*/
class MicroProcess
{
    public: MicroProcess( void ) :
                _ID(),
                is_kernel(false),
                ntimes_being_dirtied(0),
                ects_pqi(PQ_TAG_INVALID),
                epts_pqi(PQ_TAG_INVALID),
                eets_pqi(PQ_TAG_INVALID),
                rlbts_pqi(PQ_TAG_INVALID)
                {}
    public: virtual ~MicroProcess()
                {MUSDBG(5,"Deleting MicroProcess");}

    public: virtual const SimPID &PID( void ) const { return _ID; }
    private: virtual void set_PID( const SimPID &id ) { _ID = id; }
    protected: virtual MicroProcess *ID2MP( const SimPID &pid );

    /*These will be called by the priority queues of micro kernel*/
    protected: virtual const SimTime &ects(void) const=0;//Earliest Committable
    protected: virtual const SimTime &epts(void) const=0;//Earliest Processable
    protected: virtual const SimTime &eets(void) const=0;//Earliest Emittable
    protected: virtual const SimTime &rlbts(void)const=0;//Requested LBTS notif
    protected: virtual const SimTime &pspan(void)const=0;//Do epts iff<=lbts+pspan

    /*These will be called by the micro kernel*/
    protected: virtual const SimTime &enqueue( SimEventBase *e,
                                               const SimTime &lbts ) = 0;
    protected: virtual void dequeue( SimEventBase *e ) = 0;
    protected: virtual void lbts_advanced( const SimTime &new_lbts)=0;
    protected: virtual long advance_optimistically( const SimTime &lbts,
                                                 const SimTime &limit_ts,
                                                 const int &limit_nevents ) = 0;
    protected: virtual long advance( const SimTime &lbts )
               { return advance_optimistically( lbts, lbts, 0xfffff ); }

    /*These are services for use by subclasses*/
    protected: virtual void before_dirtied( void );
    protected: virtual void after_dirtied( void );
    protected: virtual const SimTime &add_to_dest( const SimPID &to,
                                                   SimEventBase *event);
    protected: virtual void remove_from_dest( SimEventBase *event );

    public: virtual ostream &operator>>( ostream &out ) const;
    public: virtual void introspect( const char *str ) const {}

    private: SimPID _ID;
    private: bool is_kernel; /*!<Is this a kernel implementation process?*/

    protected: int ntimes_being_dirtied;
    protected: SimTime old_ects,old_epts,old_eets,old_rlbts;//Frozen snapshots
    private: SimTime new_ects,new_epts,new_eets,new_rlbts;//Temp variables
    protected: SimTime ects_var,epts_var,eets_var,rlbts_var; //Scratch variables
    protected: SimTime enqts_var; //Scratch variable

    private: PQTagType ects_pqi;
    private: PQTagType epts_pqi;
    private: PQTagType eets_pqi;
    private: PQTagType rlbts_pqi;

    private: friend class PIDMap;
    private: friend class Simulator;
    private: friend class MicroKernel;
    private: friend class KernelProcessBase;
    private: friend class PQ_CLASSNAME(HeapPQ, MicroProcess, ects, ects_pqi);
    private: friend class PQ_CLASSNAME(HeapPQ, MicroProcess, epts, epts_pqi);
    private: friend class PQ_CLASSNAME(HeapPQ, MicroProcess, eets, eets_pqi);
    private: friend class PQ_CLASSNAME(HeapPQ, MicroProcess, rlbts, rlbts_pqi);
};
inline ostream &operator<<( ostream &out, const MicroProcess &mp )
    { return mp>>out; }

/*---------------------------------------------------------------------------*/
#define LESS(p1,p2) ((p1)->ects() < (p2)->ects())
DEFINE_PQ(MicroProcess, ects, ects_pqi)
typedef PQ_CLASSNAME( HeapPQ, MicroProcess, ects, ects_pqi ) CommitablePQ;
#undef LESS
#define LESS(p1,p2) ((p1)->epts() < (p2)->epts())
DEFINE_PQ(MicroProcess, epts, epts_pqi)
typedef PQ_CLASSNAME( HeapPQ, MicroProcess, epts, epts_pqi ) ProcessablePQ;
#undef LESS
#define LESS(p1,p2) ((p1)->eets() < (p2)->eets())
DEFINE_PQ(MicroProcess, eets, eets_pqi)
typedef PQ_CLASSNAME( HeapPQ, MicroProcess, eets, eets_pqi ) EmitablePQ;
#undef LESS
#define LESS(p1,p2) ((p1)->rlbts() < (p2)->rlbts())
DEFINE_PQ(MicroProcess, rlbts, rlbts_pqi)
typedef PQ_CLASSNAME( HeapPQ, MicroProcess, rlbts, rlbts_pqi ) RequestedLBTSPQ;
#undef LESS

/*---------------------------------------------------------------------------*/
struct EventStats
{
    unsigned long executed;
    unsigned long committed;
    unsigned long rolledback;
    unsigned long leftover;
    unsigned long sent;
    EventStats( void ) :
        executed(0), committed(0), rolledback(0), leftover(0), sent(0) {}
};

/*---------------------------------------------------------------------------*/
/*! \brief Underlying support class for simulation processes.
 *
 * <B>Not intended as a supported API.  Subject to change.<B>
 */
class SimProcessBase : public MicroProcess
{
    public: SimProcessBase( void ) : MicroProcess(),
                fel(),
                pel(),
                lvt(0),
                lct(0),
                execute_context(),
                can_undo(false),
                min_la(0),
                explicit_min_la(false),
                runahead(SimTime::MAX_TIME),
                resilience(0),
        copy_state(0)
                {
                    fel.set_params( 1, 2.0, 10.0); /*CUSTOMIZE*/
                }
    public: virtual ~SimProcessBase( void )
                {
                    free_all_fel();
                    free_all_pel();
                }

    protected: virtual void free_all_fel( void )
                {
                    while( SimEventBase *e = fel.pop() )
                    {
                        free_event( e );
                    }
                }
    protected: virtual void free_all_pel( void )
                {
                    while( SimEventBase *e = pel.del_head() )
                    {
                        free_event( e );
                    }
                }

    protected: EventStats &acc_estats( void );
    public: virtual SimTimerID set_timer( const SimTime &dt,
                                      void *closure=0,bool repeating=false)=0;
    public: virtual void timedout( SimTimerID timer_id, void *closure=0 ) = 0;

    public: virtual void execute( SimEvent *e ) = 0;
    protected: virtual void undo_event( SimEventBase *event )
            {
                if( !copy_state || !event->has_aux() ) return;
                ENSURE( 1, event->aux()->hook1, "" );
                void *buf = event->aux()->hook1;
                copy_state->unpack_from( buf );
            }

    protected: virtual void save_state( SimEventBase *event )
            {
                if( !copy_state || !event->has_aux() ) return;
                ENSURE( 1, !event->aux()->hook1, "" );
                int max_bytes = copy_state->max_bytes();
                void *buf = new char[max_bytes]; //XXX To be optimized
                event->aux()->hook1 = buf;
                copy_state->pack_to( buf );
            }

    protected: virtual void free_state( SimEventBase *event )
            {
                if( !copy_state || !event->has_aux() ) return;
                if( event->aux()->hook1 )
                {
                    char *buf = (char *)event->aux()->hook1;
                    copy_state->freeing( buf );
                    delete [] buf; //XXX To be optimized - avoid runtime alloc/free
                }
                event->aux()->hook1 = 0;
            }
    protected: virtual void free_event( SimEventBase *e ) { delete e; }
    protected: virtual void commit_event( SimEventBase *e, bool is_kernel )
            {
                ENSURE( 1, lct <= e->T(), lct << " <= " << e->T() );
                lct = e->T();

                free_state( e );
                free_event( e );
            }

    protected: virtual const SimTime &enqueue( SimEventBase *e,
                                               const SimTime &lbts )
            {
                enqts_var = e->T();

              before_dirtied();
                remember_new_lbts( lbts );
                fel.add( e );
              after_dirtied();

                return enqts_var;
            }
    protected: virtual void detach_all_children_from_clist( SimEventBase *ev,
                             bool cancel )
            {
                if( ev->has_aux() )
                {
                while(const SimEventBase *const_child =
                      ev->aux()->dep.clist.peek_tail())
                {
                    int nchildren = ev->aux()->dep.clist.num();
                    SimEventBase *child =const_cast<SimEventBase*>(const_child);
                    child->detach_from_clist_of_parent();
                    ENSURE( 1, ev->aux()->dep.clist.num() == nchildren-1,
                            PID()<<" Clist of "<<*ev<<" must delete "<<*child);
                    if( cancel )
                    {
                        undispatch( child );
                        free_event( child );
                    }
                }
                ENSURE( 1, ev->aux()->dep.clist.is_empty(), "" );
                }
            }
    protected: virtual void dequeue( SimEventBase *e )
            {
              before_dirtied();
                if( e->epqi == PQ_TAG_INVALID ) //It is in processed list
                {
                    rollback_to( e );
                }
                else //It is in fel heap
                {
                    fel.del( e );
                }
                e->detach_from_clist_of_parent();
              after_dirtied();
            }
    private: virtual void remember_new_lbts( const SimTime &ts )
            {
                MUSDBG( 2, PID()<<" w/ LBTS "<<execute_context.lbts
                                <<" recd new LBTS of "<<ts );
                execute_context.lbts.increase_to( ts );
            }
    protected: virtual void lbts_advanced( const SimTime &new_lbts)
            {
                MUSDBG( (PID().loc_id==0?1:2), PID()<<" LBTS advanced from "
                           <<execute_context.lbts<<" to "<<new_lbts );
                remember_new_lbts( new_lbts );
            }
    protected: virtual void do_one_commit( SimEventBase *ev )
            {
                if( !ev ) return;
                MUSDBG( 10, PID()<<" do_one_commit( " << *ev << " )" );
                SimTime old_lvt = lvt;
                MUSDBG(3, PID() << " do_one_commit committing event " << *ev);
                ev->detach_from_clist_of_parent();
                MUSDBG(3, PID() << " do_one_commit detach children " << *ev);
                detach_all_children_from_clist( ev, false );

                acc_estats().committed++;

                MUSDBG(3, PID() << " do_one_commit commit_event " << *ev);
                commit_event( ev, ev->is_kernel );
                lvt = old_lvt;
                MUSDBG( 10, PID()<<" do_one_commit done" );
            }
    protected: virtual long do_commits( const SimTime &committable_ts )
            {
                MUSDBG( 10, PID()<<" do_commits( " << committable_ts << " )" );
                long nevents = 0;
                while( pel_head_ts() <= committable_ts )
                {
                    MUSDBG( 10, PID()<<" do_commits( ) iter" );
                    SimEventBase *ev = pel.del_head();
                    if( !ev ) break;
                    do_one_commit( ev );
                    nevents++;
                    MUSDBG( 10, PID()<<" do_commits( ) iter done" );
                }
                MUSDBG( 10, PID()<<" do_commits( ) done " << nevents );
                return nevents;
            }
    protected: virtual void rollback( SimEventBase *ev )
            {
                MUSDBG(3, PID() << " rollback ev= "<<ev<<" "<<*ev );
                if( ev )
                {
                    acc_estats().rolledback++;
                    TRACE_EVENT_ROLLBACK( ev );

                    undo_event( ev );
                    free_state( ev );
                    detach_all_children_from_clist( ev, true );
                    ENSURE( 1, !ev->has_aux() ||
                                ev->aux()->dep.clist.is_empty(), "" );
                }
            }
    private: virtual SimEventBase *rollback_pel_tail( void )
            {
                MUSDBG(3, PID() << " rollback_pel_tail" );
                SimEventBase *ev = pel.del_tail();
                if( ev )
                {
                    rollback( ev );
                    lvt = ( pel.peek_tail() ? pel.peek_tail()->T() : lct );
                }
                return ev;
            }
    private: virtual void rollback_to( const SimEventBase *rbev )
            {
                MUSDBG(3, PID() << " rollback_to "<<rbev<<" "<<*rbev );
                while( SimEventBase *ev = rollback_pel_tail() )
                {
                    if( ev == rbev ) break;
                    fel.add( ev );
                }
            }
    private: virtual void rollback_to( const SimTime &rbts )
            {
                MUSDBG(3, PID() << " rollback_to "<<rbts );
                while( pel_tail_ts() > rbts )
                {
                    SimEventBase *ev = rollback_pel_tail();
                    if( !ev ) break;
                    fel.add( ev );
                }
            }
    protected: virtual void trace_process_prefix( void );
    protected: virtual void trace_process_init_start( void );
    protected: virtual void trace_process_init_end( void );
    protected: virtual void trace_event_execute_start( const SimEvent *ev );
    protected: virtual void trace_event_execute_end( const SimEvent *ev );
    protected: virtual void trace_event_dispatch( const SimEvent *ev,
                                                  const SimTime &send_ts,
                                                  const SimTime &dt );
    protected: virtual void trace_event_rollback( const SimEventBase *rbev );

    //-------------------------------
    //Send Outside Execute()
    protected: struct SOEData{
                  SOEData(void) : send_outside(false), events(), dests() {}
                  bool send_outside;
                  vector<SimEventBase *> events;
                  vector<SimPID> dests;
               } soe;
    protected: virtual void set_soe( bool _b ) { soe.send_outside=_b; }
    protected: virtual bool get_soe( void ) { return soe.send_outside; }
    protected: virtual int num_soe( void ) { return soe.events.size(); }
    protected: virtual void add_soe( const SimPID &to, SimEventBase *ev )
                       {
                           soe.events.push_back(ev);
                           soe.dests.push_back(to);
                       }
    protected: virtual void send_soe( void )
                       { for( int i = 0; i < num_soe(); i++ )
                           { add_to_dest( soe.dests[i], soe.events[i] );}
                           soe.events.clear();
                           soe.dests.clear();
                       }
    //-------------------------------

    protected: virtual long advance_optimistically( const SimTime &lbts,
                                                 const SimTime &orig_limit_ts,
                                                 const int &limit_nevents )
            {
                SimTime limit_ts( orig_limit_ts );
                SimTime committable_ts( lbts );

                MUSDBG(3, PID() << " advance_optimistically lbts= "<<lbts<<
                           " limit_ts= "<<limit_ts );
                ENSURE( 2, can_undo || limit_ts <= lbts ||
                        limit_ts == SimTime::MAX_TIME,
                        can_undo<<" "<<limit_ts<<" "<<lbts );

                limit_ts.reduce_to( lbts+runahead );
                committable_ts.increase_to( lbts+resilience );
                committable_ts.reduce_to( fel_top_ts() ); /*XXX 11Jan09*/

//int _x=0;{cout << "ADVOPT: "<<&lbts.ts<<" "<<&orig_limit_ts.ts<<" "<<&limit_ts.ts<<" "<<&SimTime::MAX_TIME.ts<<endl;}

                int nevents = 0;
                nevents += do_commits( committable_ts );
                remember_new_lbts( lbts ); //Make note of implicit notification
                execute_context.ests = SimTime::MAX_TIME;
                while( fel_top_ts() <= limit_ts &&
                       fel_top_ts() <= execute_context.ests &&
                       nevents < limit_nevents )
                {
                    const SimEventBase *cev = fel.peek();
                    if( !cev ) break;
                    SimTime rbts = cev->T()+resilience;
                    rollback_to( rbts );
                    ENSURE( 1, lvt <= rbts || lvt <= lbts,
                            lbts << " " << lvt << "<=" << rbts );

                    SimEventBase *ev = fel.pop();
                    if( !ev ) break;
                    ENSURE( 2, !ev->has_aux() ||ev->aux()->dep.clist.is_empty(),
                            "LBTS= "<<lbts<<" "<<PID()<<" "<<*ev );

                    const SimTime &tts = pel_tail_ts();
                    const SimTime &mts = (tts < SimTime::MAX_TIME ? tts : lct);
                    ENSURE( 1, lct <= mts, lct << " < " << mts );
                    if( ev->T() < mts )
                    {
                        ENSURE( 1,
                                mts-ev->T() <= resilience.ts,
                                *this<<endl<<
                                "MTS=["<<mts<<"] EVENT=["<<*ev<<"]"<<endl<<
                                "RES=["<<resilience<<"] TTS=["<<tts<<"]"<<endl<<
                                "LCT=["<<lct<<"] LVT=["<<lvt<<"]"<<endl<<
                                "RBTS=["<<rbts<<"]");
                        ev->coerce_ts( mts, mts-min_la );
                    }

                    lvt = ev->T();
                    execute_context.event = ev;
                    if( ev->T() > lbts ) save_state( ev );
                    {
                        MUSDBG(3, PID()<<" executing event ev="<<ev<<" "<<*ev);
                        ENSURE( 0, get_soe() || num_soe()==0, "" );

                        acc_estats().executed++;

                        if( ev->is_kernel )
                        {
                            ev->kernel_execute( this );
                        }
                        else
                        {
                            SimEvent *sev = (SimEvent *)ev;
                            TRACE_EVENT_EXECUTE_START( sev );
                            execute( sev );
                            TRACE_EVENT_EXECUTE_END( sev );
                        }
                        send_soe();
                        ENSURE( 0, num_soe()==0, "" );
                    }
                    execute_context.event = 0;

                    if( ev->T() > committable_ts )
		    {
                        pel.add_as_tail( ev );
		    }
		    else
                    {
                        do_one_commit( ev ); nevents++;
                        nevents += do_commits( committable_ts );
                    }
//if(_x++==0){cout << "ADVOPT LOOP: "<<&cev->T().ts<<" "<<&tts.ts<<" "<<&limit_ts.ts<<" "<<&SimTime::MAX_TIME.ts<<endl;}
                }

                MUSDBG(3, PID() <<" advance_optimistically nevents="<<nevents);

                return nevents;
            }
    protected: virtual SimEventID dispatch( const SimPID &to,
                                          SimEventBase *new_event,
                                          const SimTime &dt,
                                          const SimTime &rdt )
            {
                SimTime recv_ts( lvt+dt );
                SimTime retract_ts( recv_ts-min_la );
                retract_ts.tie += tie_counter;
                recv_ts.tie += tie_counter++; //XXX Force tie breaks

                if( rdt > SimTime::ZERO_TIME ) //An earlier retract dt is given
                {
                    retract_ts = lvt+rdt;
                    if(rdt.tie==0) retract_ts.tie=recv_ts.tie;
                }
                else if( rdt < SimTime::ZERO_TIME )//An earlier retract ts given
                {
                    /*For now, only MIN_TIME is allowed as absolute ts*/
                    ENSURE( 0, rdt == SimTime::MIN_TIME, "" );
                    retract_ts = SimTime::MIN_TIME;
                }
                else
                {
                    /*The common case (no retract dt/ts specified)*/
                    ENSURE( 1, PID()==to || retract_ts == recv_ts-min_la, "" );
                }

                ENSURE( 1, new_event->is_kernel ||
                           PID() == to ||
                           dt >= min_la,
                           *this<<endl<<
                           "from "<<PID()<<" To "<<to<<endl<<
                           "now "<<lvt<<" "<<dt<<" >= "<<min_la );
                ENSURE( 2, dt >= 0, "Positive dt required " << dt );
                new_event->set_src_dest( PID(), to );
                new_event->set_time( recv_ts, retract_ts );
                ENSURE( 1, can_undo || lvt <= execute_context.lbts, "" );
                if( lvt > execute_context.lbts )
                {
                    SimEventBase *generating_event = execute_context.event;
                    ENSURE( 2, generating_event || lvt == 0,
                            "An event should be executing at T>0 " << lvt );
                    if( generating_event )
                    {
                        ENSURE( 2, generating_event->data._dest == PID(),
                                PID()<<" must match dest "<<generating_event );
                        generating_event->aux()->dep.clist.add_as_tail(
                               new_event );
MUSDBG( 5, PID() << " dispatch() aux lvt "<<lvt<<" lbts "<<execute_context.lbts<<" "<<*new_event );
                        new_event->aux()->dep.cparent = generating_event;
                    }
                }
                TRACE_EVENT_DISPATCH( (SimEvent *)new_event, lvt, dt );
                if( to == PID() )
                {
MUSDBG( 3, PID() << " dispatch() adding event to self "<<*new_event );
                    fel.add( new_event );
                }
                else
                {
                    if(get_soe())
                    {
MUSDBG( 3, PID() << " dispatch() keeping SOE event to "<<to<<" "<<*new_event );
                        add_soe( to, new_event );
                    }
                    else
                    {
MUSDBG( 3, PID() << " dispatch() adding event to "<<to<<" "<<*new_event );
                        add_to_dest( to, new_event );
                    }
                    execute_context.ests.reduce_to( new_event->T() );
                }
                return new_event;
            }
    private: virtual void undispatch( SimEventBase *event )
            {
                ENSURE( 1, event->T() > lct,
                        *event<<" > "<<lct<<" "<<execute_context.lbts );
                ENSURE( 2, event->T() > lvt,
                        *event<<" > "<<lvt<<" "<<execute_context.lbts );
                remove_from_dest( event );
            }

    //Earliest commitable time
    protected: virtual const SimTime &ects( void ) const
            { if(ntimes_being_dirtied>0) return old_ects;
              SimTime &ts = (const_cast<SimProcessBase*>(this))->ects_var;
              ts = fel_top_ts();
              if( can_undo ) ts.reduce_to( pel_head_ts() );
              return ts; }

    //Earliest processable time
    protected: virtual const SimTime &epts( void ) const
            { if(ntimes_being_dirtied>0) return old_epts;
              if( !can_undo ) return SimTime::MAX_TIME;
              SimTime &ts = (const_cast<SimProcessBase*>(this))->epts_var;
              ts = fel_top_ts();
              //XXX if(ts > execute_context.lbts+runahead) ts=SimTime::MAX_TIME;
              return ts; }

    //Span of optimistic processing: execute event with epts() iff <= lbts+pspan
    protected: virtual const SimTime &pspan( void ) const
            { return can_undo ? runahead : SimTime::MAX_TIME; }

    //Earliest emitable time
    protected: virtual const SimTime &eets( void ) const
            { if(ntimes_being_dirtied>0) return old_eets;
              SimTime &ts = (const_cast<SimProcessBase*>(this))->eets_var;
              ts = fel_top_ts() + min_la;
#if !NODEBUG
if( false ) //XXXXXXXX Upon deep re-thinking, I think the following isn't reqd
              if( can_undo )
              {
                  SimTime pts(pel_head_ts() + min_la - 1e-30); //XXX
                  pts.increase_to( execute_context.lbts+resilience );
                  if( pts >= 0 ) ts.reduce_to(pts);
                  ts.increase_to( lct + min_la ); //XXX
              }
#endif
              return ts; }

    //Requested LBTS notification time
    protected: virtual const SimTime &rlbts( void ) const
            { if(ntimes_being_dirtied>0) return old_rlbts;
              SimTime &ts = (const_cast<SimProcessBase*>(this))->rlbts_var;
              ts = SimTime::MAX_TIME;
              //XXX if( can_undo ) ts = execute_context.lbts+runahead;
              return ts; }

    protected: FutureEventList fel;    //(unprocessed & uncommitted)
    protected: ProcessedEventList pel; //(processed but uncommitted)

    private: virtual const SimTime &fel_top_ts( void ) const
            { const SimEventBase *e = fel.peek();
              return e ? e->T() : SimTime::MAX_TIME; }
    private: virtual const SimTime &pel_head_ts( void ) const
            { const SimEventBase *e = pel.peek_head();
              return e ? e->T() : SimTime::MAX_TIME; }
    private: virtual const SimTime &pel_tail_ts( void ) const
            { const SimEventBase *e = pel.peek_tail();
              return e ? e->T() : SimTime::MAX_TIME; }
    private: virtual void update_min_la( const SimTime &lookahead )
            {
                if( !explicit_min_la || lookahead < min_la )
                {
                    min_la = lookahead;
                }
                explicit_min_la = true;
            }

    protected: SimTime lvt;//Timestamp of last processed(or being proc'd) event
    protected: SimTime lct;//Timestamp of latest committed event
    protected: struct EventExecutionContext
            {
                SimTime lbts;
                SimEventBase *event;
                SimTime ests; //Min ts of non-local events sent by this event
                EventExecutionContext() :
                    lbts(0), event(0), ests(SimTime::MAX_TIME) {}
            } execute_context;

    private: bool can_undo;  /*!<Does the subclass implement the undo method?*/
    private: SimTime min_la;/*!<Min lookahead from this PID to any other PID*/
    private: bool explicit_min_la;/*!<User explicitly specified lookahead?*/
    private: SimTime runahead;/*!<How far can optimistic execution exceed lbts*/
    private: SimTime resilience;/*!<Min diff in timestamps to cause rollback*/
    private: CopyState *copy_state;/*!<Automatically checkpointed state*/
    private: static unsigned long tie_counter; /*For entire local federate*/

    private: friend class KernelProcessBase;
    private: friend class KEvent_Init;
    private: friend class RemoteFederateProcess;
    private: friend class SimProcess;

    public: virtual ostream &operator>>( ostream &out ) const;
    public: void introspect( const char *str ) const;
};
inline ostream &operator<<( ostream &out, const SimProcessBase &pb )
    { return pb>>out; }

/*---------------------------------------------------------------------------*/
typedef map<string, KernelProcessBase *> ReflectorNameMap;

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
typedef vector<MicroProcess *> ProcessVector;
class PIDMap
{
    public: void add( MicroProcess *proc, SimFedID fed_id )
    {
        SimPID pid;
        ENSURE( 1, proc->PID() == SimPID::INVALID_PID,
                proc->PID() << " already added?" );
        if( proc->is_kernel )
        {
            kernel_procs.push_back(proc);
            pid.loc_id = SimLocalPID(-1*kernel_procs.size());
        }
        else
        {
            pid.loc_id = user_procs.size();
            user_procs.push_back(proc);
        }
        pid.fed_id = fed_id;
        proc->set_PID( pid );
    }
    public: void del( MicroProcess *proc )
    {
        del( proc->PID().loc_id );
    }
    public: void del( const SimLocalPID &loc_id )
    {
        MicroProcess *proc = get(loc_id);
        ENSURE( 1, proc, loc_id << " not added before?" );
        (loc_id < 0 ?  kernel_procs[(-loc_id)-1] : user_procs[loc_id]) = 0;
        proc->set_PID( SimPID::INVALID_PID );
    }
    public: MicroProcess *get( const SimLocalPID &loc_id )
    {
        return (loc_id < 0) ?  kernel_procs[(-loc_id)-1] : user_procs[loc_id];
    }
    public: int least_pid( void ) const
    {
        return -kernel_procs.size();
    }
    public: int highest_pid( void ) const
    {
        return user_procs.size() - 1;
    }
    protected: ProcessVector kernel_procs, user_procs;
};

/*---------------------------------------------------------------------------*/
/*! \brief Underlying support class for simulator class.
 *
 * <B>Not intended as a supported API.  Subject to change.<B>
 */
class MicroKernel
{
    public: MicroKernel( void ) :
                glbts(0), estats(), report(), status(CONSTRUCTED)
                { ENSURE( 0, !instance, "" ); instance = this; }
    public: virtual ~MicroKernel( void ) { instance = 0; }

    public: virtual int num_feds( void ) const;
    public: virtual SimFedID fed_id( void ) const;

    public: virtual void pre_init( int *pac, char ***pav );//Needed for MPI
    public: virtual void init( const char *fn );
    public: virtual void start( void );
    public: virtual const SimTime run( const SimTime &max_t=SimTime::MAX_TIME,
                                       unsigned long max_nevents=0xefffffff );
    public: virtual void stop( void );

    protected: virtual void del( SimProcess * ) = 0;
    private: virtual void print_stats( ostream &out );
    private: virtual void make_lbts_callbacks( void );

    protected: SimFedID nfedperrfp;
    public: virtual void set_nfedperrfp( const SimFedID &k ) { nfedperrfp = k; }
    public: virtual SimFedID get_nfedperrfp( void ) const { return nfedperrfp; }
    public: virtual SimLocalPID FID2RFPLID( const SimFedID &fid ) const
            {
                return -1-(fid/get_nfedperrfp());
            }
    public: virtual MicroProcess *ID2MP( const SimPID &pid )
            {
                MicroProcess *mp = 0;
                if( pid.fed_id == fed_id() ) { mp = idmap.get(pid.loc_id); }
                else { mp = idmap.get(FID2RFPLID(pid.fed_id)); } //XXX
                return mp;
            }
    public: virtual const SimPID &add_mp( MicroProcess *p );
    public: virtual void del_mp( MicroProcess *p );

    public: virtual const SimTime &forward( const SimPID &to,
                                            SimEventBase *event )
            {
                MicroProcess *to_mp = ID2MP( to );
                ENSURE(2, to_mp, "Destination "<<to<<" should exist");
                return to_mp->enqueue( event, glbts );
            }
    public: virtual void pullback( SimEventBase *event,
                                   const MicroProcess *from_mp )
            {
                MicroProcess *to_mp = ID2MP( event->data._dest );
                ENSURE( 1, to_mp, "" );
                ENSURE( 0, from_mp==to_mp || event->T() > glbts,
                           *event<<" should overstep "<<glbts );
                to_mp->dequeue( event );
            }

    public: virtual const SimTime eets( void ) const;
    public: static const SimTime local_min( void )
            {
                return instance->status<=RUNNING ?
                            instance->eets() : SimTime::MAX_TIME;
            }

    private: virtual long advance_process(MicroProcess *spb,bool really_advance,
                               const SimTime &limit_ts,
                               bool optimistically=false,
                               const SimTime &opt_limit_ts=0 );

    public: SimTime incoming_remote_event( SimEventBase *e, int src_fed);
    public: void incoming_remote_retract( const SimEventBase::EventIDType &eid,
                                          const SimTime &ts, int src_fed );
    protected: RemoteFederateProcess *find_remote_federate_proc( int src_fed );

    private: CommitablePQ cts_pq;
    private: ProcessablePQ pts_pq;
    private: EmitablePQ ets_pq;
    private: RequestedLBTSPQ rlbts_pq;

    private: SimTime glbts; //Global lbts
    private: PIDMap idmap;
    private: ReflectorNameMap refmap;

    private: vector<MicroProcess*> temp_mpvec; //Scratch variable

    protected: EventStats estats;
    protected: struct Reporting
               {
                   Reporting() :
                       dt(SimTime::MAX_TIME),
                       next(SimTime::MAX_TIME),
                       end(SimTime::MAX_TIME),
                       peak_memmb(0) {}
                   SimTime dt, next, end;
                   double peak_memmb; /*Peak memory (MB) used by sim*/
               } report;
    protected: struct Parameters
               {
                   Parameters(void);
                   long batch_sz;
                   struct
                   {
                       bool track;//Gather stats on events/communication time
                       double advance_wt;//Total wc time spent in all advance()
                   } timing;
                   struct
                   {
                       bool generate;//Generate event trace?
                       bool flatfile;//Generate flat-file format or XML-like?
                       bool initialized;//Trace file been initialized/created?
                       ofstream tstream;//Trace file stream
                       char *filename_prefix;
                   } trace;
               } params;

    protected: enum Status
               {
                   INVALID, CONSTRUCTED,
                   INITIALIZING, INITIALIZED,
                   STARTING, STARTED,
                   RUNNING,
                   STOPPING, STOPPED,
                   DONE
               } status;

    protected: virtual const EventStats &get_estats( void )const{return estats;}
    protected: virtual EventStats &acc_estats( void ) { return estats; }

    protected: virtual void trace_sim_prefix( void );
    protected: virtual void trace_sim_init_start( void );
    protected: virtual void trace_sim_addmp( MicroProcess *mp );
    protected: virtual void trace_sim_init_end( void );
    protected: virtual void trace_sim_wrapup_start( void );
    protected: virtual void trace_sim_wrapup_end( void );

    private: static MicroKernel *instance;
    public: static MicroKernel *muk(void) { return instance; }

    private: friend class MicroProcess;
    private: friend class SimProcessBase;
    private: friend class Simulator;
    public: virtual ostream &operator>>( ostream &out ) const;
    public: void introspect( const SimPID &pid, const char *str ) const;
};
inline ostream &operator<<( ostream &out, const MicroKernel &sb )
    {return sb>>out;}

/*---------------------------------------------------------------------------*/
inline void MicroProcess::before_dirtied( void )
{
    if( ntimes_being_dirtied <= 0 )
    {
        ENSURE( 3, ntimes_being_dirtied == 0, ntimes_being_dirtied );
        { //Take Snapshot
            old_ects = ects();
            old_epts = epts();
            old_eets = eets();
            old_rlbts = rlbts();
        } //End Snapshot
    }

    if( ntimes_being_dirtied++ > 0 ) return;

    MUSDBG( 3, PID() << "before_dirtied: ects= "<<old_ects<<
               " epts= "<<old_epts<<" eets= "<<old_eets<<" rlbts= "<<old_rlbts);

    if(_simdbg>=1){MicroKernel::instance->introspect(PID(),"before_dirtied");}
}

/*---------------------------------------------------------------------------*/
inline void MicroProcess::after_dirtied( void )
{
    if(_simdbg>=1){MicroKernel::instance->introspect(PID(),"after_dirtied1");}

    if( --ntimes_being_dirtied > 0 ) return;

    ENSURE( 3, ntimes_being_dirtied == 0, ntimes_being_dirtied );

    MUSDBG( 3, PID() << " after_dirtied: ects= "<<ects()<<
               " epts= "<<epts()<<" eets= "<<eets()<<" rlbts="<<rlbts() );

    new_ects = ects(); new_epts = epts(); new_eets = eets(); new_rlbts= rlbts();

    if( ects_pqi != PQ_TAG_INVALID && old_ects != new_ects )
        MicroKernel::instance->cts_pq.readjust( this, new_ects < old_ects );
    if( epts_pqi != PQ_TAG_INVALID && old_epts != new_epts )
        MicroKernel::instance->pts_pq.readjust( this, new_epts < old_epts );
    if( eets_pqi != PQ_TAG_INVALID && old_eets != new_eets )
        MicroKernel::instance->ets_pq.readjust( this, new_eets < old_eets );
    if( rlbts_pqi != PQ_TAG_INVALID && old_rlbts != new_rlbts )
        MicroKernel::instance->rlbts_pq.readjust( this, new_rlbts < old_rlbts );

    if(_simdbg>=1){MicroKernel::instance->introspect(PID(),"after_dirtied2");}
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
inline MicroProcess *MicroProcess::ID2MP( const SimPID &pid )
    { return MicroKernel::instance->ID2MP( pid ); }
inline const SimTime &MicroProcess::add_to_dest( const SimPID &to, SimEventBase *event )
    { return MicroKernel::instance->forward( to, event ); }
inline void MicroProcess::remove_from_dest( SimEventBase *event )
    { MicroKernel::instance->pullback( event, this ); }

/*---------------------------------------------------------------------------*/
extern "C" {
#define COMPOSITETIME 1
#include "tm.h"
}
#define _SHDRSZ sizeof(Synk_Hdr)
#define SHDRSZ (_SHDRSZ<8?8:((((_SHDRSZ-1)/8)+1)*8))
typedef int Synk_MsgSizeType;
typedef struct
{
    Synk_MsgSizeType totsz;
    TM_TagType tag;
} Synk_Hdr;
/*---------------------------------------------------------------------------*/
inline ostream &operator<<( ostream &o, const TM_Time &ts )
    { SimTime sts( ts.ts, ts.tie ); return o << "*" << sts; }

/*---------------------------------------------------------------------------*/
inline void *SimEventBase::get_buffer( size_t totsz )
{
#define BLOCK_SIZE(_sz) (((int(((_sz==0)?8:_sz)-1)/8)+1)*8)

    ENSURE( 2, totsz >= SHDRSZ+sizeof(SimEventBase),
            totsz<<" "<<SHDRSZ<<" "<<sizeof(SimEventBase) );

    int bsz = BLOCK_SIZE(totsz);

    MUSDBG( 10, "SimEventBase::get_buffer(" << totsz << ") bsz=" << bsz );

    FreeList &fl = free_map[bsz];
    if( fl.free_list.size() == 0 )
    {
        unsigned long ram_size = 2UL*1000UL*1000UL*1000UL; /*CUSTOMIZE*/
        if( fl.nbuffers_to_alloc<=0 ) fl.nbuffers_to_alloc = 1;/*CUSTOMIZE*/
        unsigned long rounded_bsz = ((bsz-1)/32+1)*32; /*CUSTOMIZE 32/64/128*/
        unsigned long bigbufsz = rounded_bsz*fl.nbuffers_to_alloc;
        void *bigbuf = malloc( bigbufsz );
        bytes_alloced_sofar += bigbufsz;
        if( bytes_alloced_sofar >= ram_size )
          {MUSDBG( 0, bytes_alloced_sofar<<" reaching RAM limit "<<ram_size);}
        ENSURE( 0, bigbuf, "Can't malloc "<<bigbufsz<<" bytes" );
        MUSDBG( 2, "SimEventBase::get_buffer(" << totsz << ") allocated " <<
                    fl.nbuffers_to_alloc<<" bufs * "<<rounded_bsz<<
                    " bytes/buf = "<<bigbufsz<< "bytes ptr="<< ((int*)bigbuf) );
        for( unsigned long i = 0; i < fl.nbuffers_to_alloc; i++ )
        {
            char *ep = ((char*)bigbuf) + i*rounded_bsz;
            Synk_Hdr *hdr = (Synk_Hdr *)ep;
            hdr->totsz = rounded_bsz;
            fl.free_list.push_back( ep );
        }
        if( bigbufsz <= 50UL*1000UL*1000UL ) fl.nbuffers_to_alloc *= 2;
    }
    ENSURE( 1, fl.free_list.size() > 0, "List must not be empty" );
    void *p = fl.free_list.front();
    ENSURE( 1, p, "A buffer must exist" );
    fl.free_list.pop_front();
    Synk_Hdr *hdr = (Synk_Hdr *)p;
    ENSURE( 0, hdr->totsz >= totsz, "" );
    hdr->totsz = totsz;
    MUSDBG( 10, "SimEventBase::get_buffer(" << totsz << ") returning " << hdr);
    return hdr;
}

/*---------------------------------------------------------------------------*/
inline void *SimEventBase::operator new( size_t sz, void *ptr )
{
    MUSDBG( 10, "SimEventBase::new2(" << sz << ", " << ptr << " )" );
    void *retptr = ptr;
    MUSDBG( 10, "SimEventBase::new2(" << sz << ") returning " << retptr );
    return retptr;
}

/*---------------------------------------------------------------------------*/
inline void *SimEventBase::operator new( size_t sz )
{
    MUSDBG( 10, "SimEventBase::new(" << sz << ") allocating " << sz+SHDRSZ );
    void *buf = get_buffer( sz + SHDRSZ );
    MUSDBG( 10, "SimEventBase::new(" << sz << ") got " << buf );
    Synk_Hdr *hdr = (Synk_Hdr *)buf;
    MUSDBG( 10, "SimEventBase::new(" << sz << ") returning " << hdr+1 );
    return hdr+1;
}

/*---------------------------------------------------------------------------*/
inline void SimEventBase::operator delete( void *d, size_t sz )
{
    sz += SHDRSZ;
    int bsz = BLOCK_SIZE(sz);

    MUSDBG( 10, "SimEventbase::delete(" << d << ", " << sz << ")" );

    Synk_Hdr *hdr = ((Synk_Hdr *)d)-1;
    FreeList &fl = free_map[bsz];
    fl.free_list.push_front( hdr );
    ENSURE( 1, fl.free_list.size() > 0, "List must not be empty" );
#undef BLOCK_SIZE
}

/*---------------------------------------------------------------------------*/
inline EventStats &SimProcessBase::acc_estats( void )
  { return MicroKernel::muk()->acc_estats(); }

/*---------------------------------------------------------------------------*/
#endif /*__MUSIKPRIV_H*/
