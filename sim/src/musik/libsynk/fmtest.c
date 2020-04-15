/*---------------------------------------------------------------------------*/
/* A simple performance test application for FM.                             */
/* N processors pass a single token around in a circular (ring) pattern.     */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mycompat.h"
#include "fm.h"

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
static int dbg = 1;
static int mid = 0, lastmid = 1;
static unsigned int hid = 0;
static void test_sender( int i, int n )
{
    int dest = (i+1)%n, maxlen = 64;
    FM_stream *stream = FM_begin_message( dest, maxlen, hid );
    FM_send_piece( stream, &i, sizeof(i) );
    FM_send_piece( stream, &n, sizeof(n) );
    FM_send_piece( stream, &mid, sizeof(mid) );
    FM_end_message( stream );
    mid++;
}

/*---------------------------------------------------------------------------*/
static int test_handler( FM_stream *stream, unsigned int sender )
{
    int i = -1, n = -1, mid = -1;

#if !NODEBUG
if(dbg>1){printf("test_handler() from %d\n", sender);fflush(stdout);}
#endif

    FM_receive( &i, stream, sizeof(i) );
    FM_receive( &n, stream, sizeof(n) );
    FM_receive( &mid, stream, sizeof(mid) );

#if !NODEBUG
if(dbg>1){printf("i=%d, n=%d, mid=%d, lastmid=%d\n", i, n, mid, lastmid); fflush(stdout);fflush(stdout);}
#endif

    MYASSERT( i==sender, ("sender mismatch %d %d",i,sender) );
    MYASSERT( n==FM_numnodes, ("numnodes mismatch") );

#if !NODEBUG
if(dbg>1 && mid % (lastmid/lastmid)==0) {printf("mid=%d\n",mid);fflush(stdout);}
#endif
    if(mid<lastmid)test_sender( FM_nodeid, FM_numnodes );else mid++;

    return FM_CONTINUE;
}

/*---------------------------------------------------------------------------*/
int main( int ac, char *av[] )
{
    int i = 0, n = 1;
    int nextracts = 0;
    TIMER_TYPE t1, t2;

    FM_pre_init( &ac, &av ); /*XXX*/

    {char *estr = getenv("FMTEST_DEBUG"); dbg = estr ? atoi(estr) : 1;}
    {char *estr = getenv("FMTEST_MAXTRANS"); lastmid=estr ? atoi(estr) :1000;}

    MYASSERT( ac == 1, ("Usage: %s\nSet node config using NODEINFO", av[0]) );
    {
        NodeInfo nodeinfo;

	FM_getenv_nodeinfo( &nodeinfo );
	FM_setenv_nodenames( &nodeinfo );

	i = nodeinfo.my_index;
	n = nodeinfo.nproc;

	FM_freeenv_nodenames( &nodeinfo );
    }

#if !NODEBUG
if(dbg>0){printf("i=%d n=%d trans=%d\n", i, n, lastmid);fflush(stdout);}
#endif

    FML_FMInit("fmtest");
    FML_RegisterHandler( &hid, test_handler );

    if( i==0 ) test_sender( FM_nodeid, FM_numnodes );

    TIMER_NOW(t1);
    for(mid=0;mid<lastmid;)
    {
        FM_extract(10000);
	nextracts++;
/*	if(0&&nextracts%1==0){printf("nx=%d\n",nextracts);fflush(stdout);}*/
    }
    TIMER_NOW(t2);
    FML_Barrier();
if(dbg>0){printf("i=%d n=%d trans=%d\n", i, n, lastmid);fflush(stdout);}

#if !NODEBUG
if(0){TIMER_TYPE t1,t2; double dt; TIMER_NOW(t1); do{FM_extract(~0);TIMER_NOW(t2);dt=TIMER_DIFF(t2,t1);}while(dt<1/*secs*/);}
#endif

    FM_finalize();

    {printf("i=%d done %d rounds %lf s %lf us per round %lf us/round/proc\n",i,lastmid,TIMER_DIFF(t2,t1),TIMER_DIFF(t2,t1)/lastmid*1e6,TIMER_DIFF(t2,t1)/lastmid*1e6/n);fflush(stdout);}

    #if MPI_AVAILABLE
    if(1) { MPI_Barrier(MPI_COMM_WORLD); MPI_Finalize(); }
    #endif /*MPI_AVAILABLE*/

    return 0;
}

/*---------------------------------------------------------------------------*/
