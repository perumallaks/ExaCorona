/*---------------------------------------------------------------------------*/
/* GMFM test program.                                                        */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mycompat.h"
#include "fmgm.h"

/*---------------------------------------------------------------------------*/
static int dbg = 1;

/*---------------------------------------------------------------------------*/
static int FM_nodeid = 123;
static int FM_handler = 456;

/*---------------------------------------------------------------------------*/
static void send_one_msg( int dest )
{
    char *hello = "Hello Kalyan!";
    GM_stream *stream = 0;
#if !NODEBUG
if(dbg>=2){printf("Sending msg to %d\n",dest);fflush(stdout);}
#endif
    stream = GM_begin_message( dest, GMMAXDATALEN, FM_handler, FM_nodeid, FM_nodeid+1 );
    GM_send_piece( stream, hello, strlen(hello)+1 );
    GM_end_message( stream );
#if !NODEBUG
if(dbg>=2){printf("Sent msg to %d\n",dest);fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
static void recv_one_msg( void )
{
#if !NODEBUG
if(dbg>=2){printf("recv_one_msg() start\n");fflush(stdout);}
#endif
    while( GM_extract(~0) <= 0 )
    {
    }
#if !NODEBUG
if(dbg>=2){printf("recv_one_msg() done\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
static int gm_callback( int handler, GM_stream *gm_stream, int gm_sender,
    int src_fm_id, int dest_fm_id )
{
    int retval = -1;
#if !NODEBUG
if(dbg>=2){printf("gm_callback(handler=%d,gm_sender=%d,src_fm_id=%d,dest_fm_id=%d\n",handler,gm_sender,src_fm_id,dest_fm_id);fflush(stdout);}
#endif
    MYASSERT( src_fm_id == FM_nodeid && src_fm_id+1 == dest_fm_id, ("!") );
    return retval;
}

/*---------------------------------------------------------------------------*/
static void test_gm( int ac, char *av[] )
{
    char *hosts[100];
    int i, myid, N;

    MYASSERT( ac >= 4, ("Usage: test <i> <n> <nodenames>") );
    myid = atoi(av[1]);
    N = atoi(av[2]);
#if !NODEBUG
if(dbg>=0){printf("i=%d N=%d\n",myid,N);fflush(stdout);}
#endif
    for( i = 0; i < N; i++ )
    {
        hosts[i] = av[3+i];
#if !NODEBUG
if(dbg>=0){printf("host[%d]=\"%s\"\n",i,hosts[i]);fflush(stdout);}
#endif
    }

    GM_initialize( myid, N, hosts, gm_callback );

    {
        int dest = (GM_nodeid+1)%GM_numnodes;
#if !NODEBUG
if(dbg>=0){printf("Messaging with node %d\n",dest);fflush(stdout);}
#endif
	if( GM_nodeid == 0 ) send_one_msg(dest);
        for( i = 0; i < 1000000; i++ )
	{
            recv_one_msg();
	    send_one_msg(dest);
#if !NODEBUG
if(i%100000==0){printf("%d\n",i);fflush(stdout);}
#endif
	}
    }

#if !NODEBUG
if(dbg>=0){printf("Done messaging.\n");fflush(stdout);}
#endif

    GM_finalize();
}

/*---------------------------------------------------------------------------*/
int main( int ac, char *av[] )
{
    test_gm( ac, av );
    return 0;
}

/*---------------------------------------------------------------------------*/
