/*---------------------------------------------------------------------------*/
/* Implementation of FM-like interface to Myrinet GM 1.2 calls.              */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mycompat.h"
#include "fmgm.h"
#if GM_AVAILABLE
    #include <gm.h>
#else
    #define GM_FAILURE 0
    #define GM_SUCCESS 1
    #define GM_LOW_PRIORITY 1
    #define GM_RECV_EVENT 1
    #define GM_MAX_HOST_NAME_LEN 1000
    typedef int gm_status_t;
    struct gm_port { int dummy; };
    struct gm_recv { int type, length; void *buffer; };
    union gm_recv_event { struct gm_recv recv; };
    #define gm_perror(m,s)          printf("%s status=%d\n",m,s)
    #define gm_init()                         GM_FAILURE
    #define gm_open(a1,a2,a3,a4,a5)           GM_FAILURE
    #define gm_get_host_name(a1,a2)           GM_FAILURE
    #define gm_get_node_id(a1,a2)             GM_FAILURE
    #define gm_max_node_id_inuse(a1,a2)       GM_FAILURE
    #define gm_node_id_to_host_name(a1,a2)    ""
    #define gm_min_size_for_length(a1)        0
    #define gm_set_acceptable_sizes(a1,a2,a3) GM_FAILURE
    #define gm_num_send_tokens(a1)            0
    #define gm_num_receive_tokens(a1)         0
    #define gm_dma_malloc(a1,a2)              0
    #define gm_provide_receive_buffer_with_tag(a1,a2,a3,a4,a5)
    #define gm_close(a1)
    #define gm_finalize()
    #define gm_send_with_callback(a1,a2,a3,a4,a5,a6,a7,a8,a9)  GM_FAILURE
    #define gm_receive_pending(a1)            0
    #define gm_receive(a1)                    0
    #define gm_unknown(a1,a2)                 GM_FAILURE
    #define gm_ntohc(a1)                      a1
    #define gm_ntohl(a1)                      a1
    #define gm_ntohp(a1)                      a1
#endif

/*---------------------------------------------------------------------------*/
static int gmfmdbg = 1;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int length;    /*Actual #bytes in this piece*/
    int offset;    /*Into data byte array; pointer alignment accounted for*/
} GMMsgPieceSpec;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int npieces;                       /*Actual #pieces in this message*/
    GMMsgPieceSpec pspec[GMMAXPIECES]; /*Pieces pointing into data array*/
    char pdata[GMMAXDATALEN];          /*Compacted/linearized data of pieces*/
} GMMsgPieces;

/*---------------------------------------------------------------------------*/
typedef struct _GMMsg
{
    int internal; /*Is this a GMFM-specific control/internal message?*/
    int src_id;
    int dest_id;
    int handler;
    int src_pe;
    int dest_pe;
    struct _GMMsg *next;
    GMMsgPieces pieces; /*This should always be at the end of the message*/
} GMMsg;

/*---------------------------------------------------------------------------*/
typedef char GMHostName[GM_MAX_HOST_NAME_LEN];

/*---------------------------------------------------------------------------*/
typedef struct
{
    unsigned int inodeid;
    int iportnum;
    GMHostName hostname;
} GMPeer;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int unitnum, portnum;
    char *portname;
    unsigned int inodeid, maxinodeid;
    GMHostName localhostname;
    GMHostName ihostname[GMMAXPE];
} GMBoardInfo;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int nrecd;
} GMBarrierInfo;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int nodeid, numnodes;
    struct gm_port *port;
    GMBoardInfo gmb;
    struct{ unsigned int nst, nrt; } orig, curr;
    unsigned int maxmsglen, maxmsgszlog;
    GMPeer peers[GMMAXPE];
    GMMsg *sendbufs;
    GMBarrierInfo bar;
} GMState;

/*---------------------------------------------------------------------------*/
int GM_nodeid;
int GM_numnodes;

/*---------------------------------------------------------------------------*/
static GMState gmstate, *gm = &gmstate;
static GMCallback *fmcb = 0, *fmprivcb = 0;

/*---------------------------------------------------------------------------*/
static void print_gmmsg( GMMsg *msg )
{
    MYASSERT( msg, ("!") );
    printf("internal=%d\n",msg->internal);fflush(stdout);
    printf("src_id=%d\n",msg->src_id);fflush(stdout);
    printf("dest_id=%d\n",msg->dest_id);fflush(stdout);
    printf("handler=%d\n",msg->handler);fflush(stdout);
}

/*---------------------------------------------------------------------------*/
int GM_internal_callback(int handler, GM_stream *stream,
    int src_pe, int src_id, int dest_id)
{
    gm->bar.nrecd++;

    return 1;
}

/*---------------------------------------------------------------------------*/
GM_stream *GM_begin_internal_message( int recipient, int length, int handler,
    int src_id, int dest_id )
{
    GM_stream *stream = 0;

    stream = GM_begin_message( recipient, length, handler, src_id, dest_id );

    {
    GMMsg *msg = 0;
    msg = (GMMsg *)stream;
    msg->internal = 1;
    }

    return stream;
}

/*---------------------------------------------------------------------------*/
void GM_barrier( void )
{
#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d starting barrier\n",GM_nodeid);fflush(stdout);}
#endif
    /*Every one sends to node 0, and node 0 responds back to everyone*/
    if( GM_nodeid == 0 )
    {
	int i = 0;

#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d waiting for %d msgs\n",GM_nodeid, GM_numnodes-1);fflush(stdout);}
#endif

        /*Wait for everyone*/
	while( gm->bar.nrecd < GM_numnodes-1 ) { GM_extract(1); }

#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d recd all %d msgs\n",GM_nodeid, GM_numnodes-1);fflush(stdout);}
#endif

        gm->bar.nrecd -= (GM_numnodes-1);

#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d replying to 1..%d nodes\n",GM_nodeid, GM_numnodes-1);fflush(stdout);}
#endif

	/*Reply to everyone*/
	for( i = 1; i < GM_numnodes; i++ )
	{
	    GM_stream *s = GM_begin_internal_message( i, 0, 0, 0, i );
	    GM_end_message( s );
	}
    }
    else
    {
	GM_stream *s = GM_begin_internal_message( 0, 0, 0, GM_nodeid, 0 );
	GM_end_message( s );

#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d sent msg to node 0\n",GM_nodeid);fflush(stdout);}
#endif

        while( gm->bar.nrecd <= 0 ) { GM_extract(1); }

#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d recd msg from node 0\n",GM_nodeid);fflush(stdout);}
#endif

        gm->bar.nrecd--;
    }
#if !NODEBUG
if(gmfmdbg>=1){printf("GM_nodeid=%d done barrier\n",GM_nodeid);fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void GM_initialize( int nodeid, int numnodes, char *hosts[], GMCallback *cb )
{
    char *progname = "GMFM";
    int i = 0;
    gm_status_t status;
    unsigned int maxportnum = 16 /*XXX: KALYAN 128*/;
    int bound = 0;
    char *estr = getenv("FMGM_DEBUG");

    gmfmdbg = (estr ? atoi(estr) : 0);

    gm->nodeid = GM_nodeid = nodeid;
    gm->numnodes = GM_numnodes = numnodes;
#if !NODEBUG
if(gmfmdbg>=0){printf("GM_nodeid=%d, GM_numnodes=%d\n",GM_nodeid,GM_numnodes);fflush(stdout);}
#endif

    MYASSERT( cb, ("Message callback required") );
    fmcb = cb;
    fmprivcb = GM_internal_callback;

    for( i = 0; i < gm->numnodes; i++ )
    {
	GMPeer *peer = &gm->peers[i];
	peer->inodeid = -1;
	peer->iportnum = -1;
	strcpy( peer->hostname, hosts[i] );
    }

#define ON_ERR(_act) MYASSERT(status == GM_SUCCESS, _act ; gm_perror("",status))

    status = gm_init();
    ON_ERR( ("%d",status) );
#if !NODEBUG
if(gmfmdbg>=1){printf("gm_init() done\n");fflush(stdout);}
#endif

    gm->gmb.unitnum = 0; gm->gmb.portname = progname; gm->gmb.portnum = 2;
    do
    {
#if !NODEBUG
if(gmfmdbg>=0){printf("Binding to port %d...",gm->gmb.portnum);fflush(stdout);}
#endif

        status = gm_open( &gm->port, gm->gmb.unitnum, gm->gmb.portnum,
	                  gm->gmb.portname, GM_API_VERSION_1_2 );
        if( status == GM_SUCCESS )
	{
#if !NODEBUG
if(gmfmdbg>=0){printf("Succeeded\n");fflush(stdout);}
#endif
	    bound = 1;
	}
	else
	{
#if !NODEBUG
if(gmfmdbg>=0){printf("Failed\n");fflush(stdout);}
#endif
            gm_perror("gm_open", status);
	    gm->gmb.portnum++;
	}
    }while(!bound && gm->gmb.portnum < maxportnum);

    ON_ERR( ("Couldn't bind to any port upto port %d",gm->gmb.portnum) );
#if !NODEBUG
if(gmfmdbg>=0){printf("GM: Bound to local port %u\n",gm->gmb.portnum);fflush(stdout);}
#endif

    status = gm_get_host_name( gm->port, gm->gmb.localhostname );
    ON_ERR( ("Couldn't get local host name") );
#if !NODEBUG
if(gmfmdbg>=0){printf("GM: Local host name \"%s\"\n",gm->gmb.localhostname);fflush(stdout);}
#endif

    status = gm_get_node_id( gm->port, &gm->gmb.inodeid );
    ON_ERR( ("Couldn't get local node id") );
#if !NODEBUG
if(gmfmdbg>=0){printf("GM: Local node id %u\n",gm->gmb.inodeid);fflush(stdout);}
#endif

    status = gm_max_node_id_inuse( gm->port, &gm->gmb.maxinodeid );
    ON_ERR( ("Couldn't get max node id in use") );
#if !NODEBUG
if(gmfmdbg>=0){printf("GM: Max node id in use %u\n",gm->gmb.maxinodeid);fflush(stdout);}
#endif

    for( i = 0; i <= gm->gmb.maxinodeid; i++ )
    {
        char *nm = gm_node_id_to_host_name( gm->port, i );
	strcpy( gm->gmb.ihostname[i], nm?nm:"" );
    }

#if !NODEBUG
if(gmfmdbg>=1){printf("Node IDs:\n----------\n");for(i=0;i<=gm->gmb.maxinodeid;i++){printf("[%u] %-12.12s%s",i,gm->gmb.ihostname[i],(i+1)%3==0?"\n":"\t");}printf("\n----------\n");fflush(stdout);}
#endif

    for( i = 0; i < gm->numnodes; i++ )
    {
	GMPeer *peer = &gm->peers[i];
	int j = 0, inodeid = -1;
	for( j = 0; j <= gm->gmb.maxinodeid; j++ )
	{
	    if( !strcmp( peer->hostname, gm->gmb.ihostname[j] ) )
	    {
	        inodeid = j;
		break;
	    }
	}
	MYASSERT( inodeid >= 0, ("Can't find host #%d \"%s\" in GM tables!",
	        i,peer->hostname) );
#if !NODEBUG
if(gmfmdbg>=1){printf("GMInterfaceID=%3d Node[%d]=\"%s\"\n",inodeid,i,peer->hostname);fflush(stdout);}
#endif
	peer->inodeid = inodeid;
	peer->iportnum = 2; /*XXX*/
    }

    gm->maxmsglen = sizeof(GMMsg);
    gm->maxmsgszlog = gm_min_size_for_length(gm->maxmsglen);

#if !NODEBUG
if(gmfmdbg>=1){printf("GM: Min size for msg len %d(0x%x) is %d(0x%x)\n",gm->maxmsglen,gm->maxmsglen,gm->maxmsgszlog,gm->maxmsgszlog);fflush(stdout);}
#endif

    status=gm_set_acceptable_sizes(gm->port,GM_LOW_PRIORITY,1<<gm->maxmsgszlog);
    ON_ERR( ("Couldn't set msg acceptable sizes") );

    gm->orig.nst = gm->curr.nst = gm_num_send_tokens( gm->port );
#if !NODEBUG
if(gmfmdbg>=1){printf("gm_num_send_tokens()=%d\n",gm->orig.nst);fflush(stdout);}
#endif

    gm->orig.nrt = gm->curr.nrt = gm_num_receive_tokens( gm->port );
#if !NODEBUG
if(gmfmdbg>=1){printf("gm_num_receive_tokens()=%d\n",gm->orig.nrt);fflush(stdout);}
#endif

    gm->sendbufs = 0;
    for( i = 0; i < gm->orig.nst; i++ )
    {
        GMMsg *sbuf = 0;
	sbuf = gm_dma_malloc( gm->port, gm->maxmsglen );
	status = !!sbuf ? GM_SUCCESS : GM_FAILURE;
	ON_ERR( ("Couldn't allocate dma send buffer #%d",i) );
	sbuf->next = gm->sendbufs;
	gm->sendbufs = sbuf;
    }

    for( i = 0; i < gm->orig.nrt; i++ )
    {
	GMMsg *rbuf = 0;
	rbuf = gm_dma_malloc( gm->port, gm->maxmsglen );
	status = !!rbuf ? GM_SUCCESS : GM_FAILURE;
	ON_ERR( ("Couldn't allocate dma receive buffer #%d",i) );
        gm_provide_receive_buffer_with_tag( gm->port, rbuf, gm->maxmsgszlog,
	    GM_LOW_PRIORITY, 0 );
    }
#if !NODEBUG
if(gmfmdbg>=1){printf("Registered %d receive bufs\n",gm->orig.nrt);fflush(stdout);}
#endif

    gm->bar.nrecd = 0;
    GM_barrier();

#undef ON_ERR
}

/*---------------------------------------------------------------------------*/
void GM_finalize( void )
{
    gm_close( gm->port );
#if !NODEBUG
if(gmfmdbg>=1){printf("gm_close() done\n");fflush(stdout);}
#endif

    gm_finalize();
#if !NODEBUG
if(gmfmdbg>=1){printf("gm_finalize() done\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
static void send_callback( struct gm_port *port,
    void *context, gm_status_t status )
{
    MYASSERT( status == GM_SUCCESS, ("Fatal error on send");gm_perror("",status));
    MYASSERT( port == gm->port, ("ports must match") );
    MYASSERT( gm->curr.nst < gm->orig.nst, ("%u %u",gm->curr.nst,gm->orig.nst) );

    {
    GMMsg *sbuf = (GMMsg *)context;
    sbuf->next = gm->sendbufs;
    gm->sendbufs = sbuf;
    gm->curr.nst++;
    }
}

/*---------------------------------------------------------------------------*/
GM_stream *GM_begin_message( int recipient, int length, int handler,
    int src_id, int dest_id )
{
    GM_stream *sendstream = 0;

    MYASSERT( length <= GMMAXDATALEN,
            ("GM msg size %d can't exceed compiled %d",length,GMMAXDATALEN));
    MYASSERT( gm->curr.nst <= gm->orig.nst, ("%u",gm->curr.nst) );

    while( gm->curr.nst <= 0 )
    {
        MYASSERT(0,("To be completed"));
    }
    MYASSERT( gm->sendbufs, ("%u",gm->curr.nst) );

    {
    GMMsg *sbuf = gm->sendbufs;
    gm->sendbufs = sbuf->next;
    --gm->curr.nst;

    sbuf->internal = 0;
    sbuf->src_id = src_id;
    sbuf->dest_id = dest_id;
    sbuf->handler = handler;
    sbuf->src_pe = gm->nodeid;
    sbuf->dest_pe = recipient;
    sbuf->pieces.npieces = 0;
    sendstream = (GM_stream *)sbuf;
    }

    return sendstream;
}

/*---------------------------------------------------------------------------*/
void GM_send_piece( GM_stream *sendstream, void *buffer, int length )
{
    GMMsg *msg = (GMMsg *)sendstream;
    GMMsgPieces *pcs = 0;
    int offset = 0;

    MYASSERT( msg && msg->src_pe == GM_nodeid, ("!" ) );

    pcs = &msg->pieces;

    if( pcs->npieces > 0 )
    {
	GMMsgPieceSpec *lastpc = &pcs->pspec[pcs->npieces-1];
	offset = lastpc->offset + lastpc->length;
	offset = ((offset-1)/16 + 1) * 16;
    }

    MYASSERT( offset+length <= GMMAXDATALEN,
            ("GM msg size %d can't exceed compiled %d",
	    offset+length, GMMAXDATALEN) );

    MYASSERT( pcs->npieces < GMMAXPIECES,
            ("GM msg pieces can't exceed compiled %d pieces", GMMAXPIECES) );
    {
    GMMsgPieceSpec *newspec = &pcs->pspec[pcs->npieces++];
    newspec->offset = offset;
    newspec->length = length;
    memcpy( &pcs->pdata[offset], (char *)buffer, length );
    }
}

/*---------------------------------------------------------------------------*/
void GM_end_message( GM_stream *sendstream )
{
    GMMsg *sbuf = (GMMsg *)sendstream;
    unsigned dest_gmid = gm->peers[sbuf->dest_pe].inodeid;
    unsigned dest_gmport = gm->peers[sbuf->dest_pe].iportnum;
    int msglen = gm->maxmsglen - GMMAXDATALEN;
    GMMsgPieces *pcs = &sbuf->pieces;
    int databytes = 0;

    if( pcs->npieces > 0 )
    {
        GMMsgPieceSpec *lastpc = &pcs->pspec[pcs->npieces-1];
	databytes = lastpc->offset + lastpc->length;
    }
    msglen += databytes;

    (void)
    gm_send_with_callback( gm->port, sbuf, gm->maxmsgszlog, msglen,
                           GM_LOW_PRIORITY, dest_gmid, dest_gmport,
			   send_callback, sbuf );
}

/*---------------------------------------------------------------------------*/
static int npieces_recd; /*#pieces received by user from current message*/

/*---------------------------------------------------------------------------*/
void GM_receive( void *buffer, GM_stream *receivestream, unsigned int length )
{
    GMMsg *msg = (GMMsg *)receivestream;
    int next_piece = npieces_recd++;
    GMMsgPieces *pcs = 0;

    MYASSERT( msg && msg->dest_pe == GM_nodeid, ("!" ) );
    pcs = &msg->pieces;
    MYASSERT( next_piece < pcs->npieces, ("Only %d pieces exist", pcs->npieces) );

    {
    GMMsgPieceSpec *next_spec = &pcs->pspec[next_piece];
    int offset = next_spec->offset;
    MYASSERT( length <= next_spec->length,
            ("Only %d < %u bytes in piece", next_spec->length, length) );
    memcpy( (char *)buffer, &pcs->pdata[offset], length );
    }
}

/*---------------------------------------------------------------------------*/
int GM_extract( unsigned int maxbytes )
{
    int nbytes = 0;
#if !NODEBUG
if(gmfmdbg>=5){printf("GM_extract() started\n");fflush(stdout);}
#endif
    while( nbytes < maxbytes && gm_receive_pending( gm->port ) )
    {
        union gm_recv_event *e = 0;
	struct gm_recv *recv = 0;
#if !NODEBUG
if(gmfmdbg>=2){printf("Doing blocking receive...\n");fflush(stdout);}
#endif
	e = gm_receive( gm->port );
#if !NODEBUG
if(gmfmdbg>=2){printf("Done receive.\n");fflush(stdout);}
#endif
	recv = &e->recv;
	switch( gm_ntohc(recv->type) )
	{
	    case GM_RECV_EVENT:
	    {
		int len = gm_ntohl(recv->length);
		GMMsg *msg = (GMMsg *)gm_ntohp(recv->buffer);
#if !NODEBUG
if(gmfmdbg>=2){printf("Recd msg %p len %d!\n",msg,len);fflush(stdout);print_gmmsg(msg);}
#endif
		MYASSERT( 0 <= len && len <= gm->maxmsglen, ("%d",len) );
		{
		    GM_stream *gm_stream = (GM_stream *)msg;
		    int handler = msg->handler, gm_sender = msg->src_pe;
		    int src_fm_id = msg->src_id, dest_fm_id = msg->dest_id;
		    npieces_recd = 0;
#if !NODEBUG
if(gmfmdbg>=3){printf("Recd %sternal msg\n", msg->internal ? "in" : "ex");};
#endif
		    if( msg->internal )
		    {
		        MYASSERT( fmprivcb, ("!") );
		        fmprivcb( handler, gm_stream, gm_sender,
			          src_fm_id, dest_fm_id);
		    }
		    else
		    {
		        MYASSERT( fmcb, ("!") );
		        fmcb( handler, gm_stream, gm_sender,
			      src_fm_id, dest_fm_id);
		    }
		}
                nbytes += len;
		/*Return buffer to GM*/
                gm_provide_receive_buffer_with_tag( gm->port,
						    gm_ntohp(recv->buffer),
		                                    gm_ntohc(recv->size),
						    GM_LOW_PRIORITY,
						    gm_ntohc(recv->tag) );
#if !NODEBUG
if(gmfmdbg>=2){printf("Returned event %p to GM!\n",msg);fflush(stdout);}
#endif
		break;
	    }
	    default:
	    {
#if !NODEBUG
if(gmfmdbg>=2){printf("Recd unknown event %d!\n",recv->type);fflush(stdout);}
#endif
	        (void) gm_unknown( gm->port, e );
#if !NODEBUG
if(gmfmdbg>=2){printf("gm_unknown done!\n");fflush(stdout);}
#endif
		break;
	    }
	}
#if !NODEBUG
if(gmfmdbg>=2){printf("Retrying gm_receive_pending %d\n",nbytes);fflush(stdout);}
#endif
    }
#if !NODEBUG
if(gmfmdbg>=5){printf("GM_extract()=%d\n",nbytes);fflush(stdout);}
#endif
    return nbytes;
}

/*---------------------------------------------------------------------------*/
int GM_numpieces( GM_stream *gm_stream )
{
    GMMsg *msg = (GMMsg *)gm_stream;
    GMMsgPieces *pcs = &msg->pieces;

    return pcs->npieces;
}

/*---------------------------------------------------------------------------*/
int GM_piecelen( GM_stream *gm_stream, int i )
{
    GMMsg *msg = (GMMsg *)gm_stream;
    GMMsgPieces *pcs = &msg->pieces;
    GMMsgPieceSpec *spec = 0;

    MYASSERT( 0 <= i && i < pcs->npieces, ("Only %d pieces", pcs->npieces) );

    spec = &pcs->pspec[i];

    return spec->length;
}

/*---------------------------------------------------------------------------*/
int GM_debug_level( int level )
{
    int old = gmfmdbg;
    gmfmdbg = level;
    return old;
}

/*---------------------------------------------------------------------------*/
