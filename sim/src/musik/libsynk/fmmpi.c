/*---------------------------------------------------------------------------*/
/* FM-like interface for MPI communication.                                  */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mycompat.h"
#include "fmmpi.h"

#ifndef FALSE
    #define FALSE 0
    #define TRUE  1
#endif

/*----------------------------------------------------------------------------*/
#if MPI_AVAILABLE
    #include <mpi.h>
    #ifndef SSIZE_MAX
    #define SSIZE_MAX 32767
    #endif
#else
    #define MPI_SUCCESS 0
    #define MPI_ERROR 1
    #define MPI_Init(a,b)      (void)0
    #define MPI_Finalize()     (void)0
    #define MPI_Comm_rank(a,b) (void)0
    #define MPI_Comm_size(a,b) (void)0
    #define MPI_Buffer_attach(a,b) (MPI_ERROR)
    #define MPI_Pack(a,b,c,d,e,f,g) (MPI_ERROR)
    #define MPI_Send(a,b,c,d,e,f) (MPI_ERROR)
    #define MPI_Bsend(a,b,c,d,e,f) (MPI_ERROR)
    #define MPI_Unpack(a,b,c,d,e,f,g) (MPI_ERROR)
    #define MPI_Recv(a,b,c,d,e,f,g) (MPI_ERROR)
    #define MPI_Iprobe(a,b,c,d,e) (MPI_ERROR)
    #define MPI_Probe(a,b,c,d,e) (MPI_ERROR)
    #define MPI_Barrier(a) (MPI_ERROR)
    #define MPI_BYTE 0
    #define MPI_PACKED 0
    #define MPI_COMM_WORLD 0
    #define MPI_ANY_SOURCE 0
    #define MPI_BSEND_OVERHEAD 0
    #ifndef SSIZE_MAX
    #define SSIZE_MAX 32767
    #endif
    typedef struct { int MPI_SOURCE; int MPI_TAG; } MPI_Status;
    typedef void *MPI_Request;
    #define MPI_REQUEST_NULL 0
    #define MPI_UNDEFINED 0
#endif

/*---------------------------------------------------------------------------*/
typedef struct { void *iov_base; int iov_len; } IOVEC;

/*---------------------------------------------------------------------------*/
static int mpifmdbg = 0;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int src_id;  /*Original sender's caller-specific ID*/
    int dest_id; /*Original receiver's caller-specific ID*/
    int src_pe;
    int dest_pe;
    int handler;
    int npieces; /*Including this header piece; hence always >=1*/
    int piecelen[FMMPIMAXPIECES]; /*Byte length of each piece*/
    int totbytes;/*#bytes in all pieces combined; hence always >= sizeof(hdr)*/
} FMMPIMsgHeaderPiece;

/*---------------------------------------------------------------------------*/
typedef struct
{
    IOVEC pieces[FMMPIMAXPIECES];
    FMMPIMsgHeaderPiece hdr;
} FMMPISendMsg;

/*---------------------------------------------------------------------------*/
typedef struct
{
    FMMPIMsgHeaderPiece hdr;
    int npieces_recd;
    int nbytes_recd;
    int position;
} FMMPIRecvMsg;

/*---------------------------------------------------------------------------*/
typedef struct Flctl_PPSBufStruct /*Postponed Send Buffer*/
{
    FMMPISendMsg msg;
    struct Flctl_PPSBufStruct *prev, *next;
} Flctl_PPSBuf;
/*---------------------------------------------------------------------------*/
static Flctl_PPSBuf *flctl_ppsbuf_new( void );
static void flctl_ppsbuf_delete( Flctl_PPSBuf *fbuf );
static void flctl_ppsbuf_prepend( Flctl_PPSBuf **head, Flctl_PPSBuf *fbuf );
static void flctl_ppsbuf_delink( Flctl_PPSBuf **pfbuf );

/*---------------------------------------------------------------------------*/
typedef struct
{
    int inited;

    int nodeid;
    int numnodes;

    int msg_tag;

    FMMPISendMsg send_msg;
    FMMPIRecvMsg recv_msg;

    #define BLOAT_FACTOR 2
    #define MAXBUFLEN BLOAT_FACTOR*FMMPIMAXDATALEN
    #define MAXPENDINGMSGS 1000L /*CUSTOMIZE*/
#define STATICBUFS 0
#if STATICBUFS
    char attachbuf[MAXPENDINGMSGS * (MAXBUFLEN + MPI_BSEND_OVERHEAD)];
    char packbuf[MAXBUFLEN];
#else /*STATICBUFS*/
    char *attachbuf;
    char *packbuf;
#endif /*STATICBUFS*/
    int attachbuflen;
    int packbuflen;

    BOOLEAN prerecv;
    char **prerecvbuf;
    MPI_Request *prerecvreq;
    MPI_Status *prerecvstat;
    int *prerecvtestidx;
    int prerecvtestcount;
    BOOLEAN prerecvtestany;
    int prerecvbufcount;
    int prerecvbufsz;
    int prerecvbufidx;

    unsigned long probetot, probereadytot, probenztot;
    BOOLEAN probeprevready;

    Flctl_PPSBuf *flctl_ppsbuflist;
    int flctl_msg_tag; /*tag for handshake msgs*/
    long flctl_k; /*every how many sends to do blocking ack/handshake*/
    BOOLEAN flctl_icantsendto[FMMPIMAXPE];
    long flctl_nsentto[FMMPIMAXPE];
    long flctl_nrecdfrom[FMMPIMAXPE];
} FMMPIState;

/*---------------------------------------------------------------------------*/
int FMMPI_nodeid;
int FMMPI_numnodes;
static FMMPICallback *fmcb = 0;
static FMMPIState *mpi = 0;

/*----------------------------------------------------------------------------*/
#if PORTALS_AVAILABLE
    #include "fmport.c"
    #define USE_PORTALS TRUE
#else /*PORTALS_AVAILABLE*/
    #define USE_PORTALS FALSE
    #define FMPTL_Init(_1) (void)0
    #define FMPTL_Finish(_1) (void)0
    #define FMPTL_RecvMesg(_1,_2,_3,_4) (0)
    #define FMPTL_SendMesg(_1,_2) (void)0
    #define FMPTL_RecvPiece(_1,_2) (void)0
#endif /*PORTALS_AVAILABLE*/
static int use_portals = USE_PORTALS;
#undef USE_PORTALS

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
static Flctl_PPSBuf *flctl_ppsbuf_freelist = 0;

/*---------------------------------------------------------------------------*/
static Flctl_PPSBuf *flctl_ppsbuf_new()
{
    Flctl_PPSBuf *fbuf =  0;
    if( !flctl_ppsbuf_freelist )
    {
        fbuf =  calloc(1,sizeof(Flctl_PPSBuf));
    }
    else
    {
        fbuf = flctl_ppsbuf_freelist;
        flctl_ppsbuf_freelist = flctl_ppsbuf_freelist->next;
        if( flctl_ppsbuf_freelist ) { flctl_ppsbuf_freelist->prev = 0; }
    }

    fbuf->prev = fbuf->next = 0;

    return fbuf;
}

/*---------------------------------------------------------------------------*/
static void flctl_ppsbuf_delete( Flctl_PPSBuf *fbuf )
{
    fbuf->prev = 0;
    fbuf->next = flctl_ppsbuf_freelist;
    if( flctl_ppsbuf_freelist ) { flctl_ppsbuf_freelist->prev = fbuf; }
    flctl_ppsbuf_freelist = fbuf;
}

/*---------------------------------------------------------------------------*/
static void flctl_ppsbuf_prepend( Flctl_PPSBuf **head, Flctl_PPSBuf *fbuf )
{
    fbuf->prev = 0;
    fbuf->next = (*head);
    if(*head) { (*head)->prev = fbuf; }
    (*head) = fbuf;
}

/*---------------------------------------------------------------------------*/
static void flctl_ppsbuf_delink( Flctl_PPSBuf **pfbuf )
{
    Flctl_PPSBuf *fbuf = (*pfbuf), *saved_next = 0;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:delink() enter %p\n",FMMPI_nodeid,*pfbuf);fflush(stdout);}
#endif
    saved_next = fbuf->next;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:delink() 1 %p\n",FMMPI_nodeid,*pfbuf);fflush(stdout);}
#endif
    if(fbuf->prev) { fbuf->prev->next = fbuf->next; }
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:delink() 2 %p\n",FMMPI_nodeid,*pfbuf);fflush(stdout);}
#endif
    if(fbuf->next) { fbuf->next->prev = fbuf->prev; }
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:delink() mid %p\n",FMMPI_nodeid,*pfbuf);fflush(stdout);}
#endif
    fbuf->prev = 0;
    fbuf->next = 0;
    (*pfbuf) = saved_next;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:delink() leave %p\n",FMMPI_nodeid,*pfbuf);fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
static void fmmpi_msg_deepcopy( FMMPISendMsg *copied_msg,
    const FMMPISendMsg *msg )
{
    int p = 0;

    memcpy( &copied_msg->hdr, &msg->hdr, sizeof(msg->hdr) );
    for( p = 0; p < msg->hdr.npieces; p++ )
    {
        const IOVEC *src_pc = &msg->pieces[p];
        IOVEC *dest_pc = &copied_msg->pieces[p];
        void *iovp = dest_pc->iov_base;
        if( dest_pc->iov_len <= 0 )
        {
            MYASSERT( !iovp, ("iovp") );
        }
        else
        {
            MYASSERT( dest_pc->iov_base, ("iovbase") );
            if( dest_pc->iov_len < src_pc->iov_len )
            {
                free( iovp );
                dest_pc->iov_base = 0;
                dest_pc->iov_len = 0;
            }
        }

        if( !dest_pc->iov_base )
        {
            iovp = malloc( src_pc->iov_len );
        }
        dest_pc->iov_base = iovp;
        dest_pc->iov_len = src_pc->iov_len;
        memcpy( dest_pc->iov_base, src_pc->iov_base, src_pc->iov_len );
    }
}

/*---------------------------------------------------------------------------*/
static void flctl_recv_dummy( void )
{
    int dummy = 0, retcode = 0, from_pe = -1;
    MPI_Status status;
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI DummyRecv\n",FMMPI_nodeid);fflush(stdout);}
#endif
    retcode = MPI_Recv( (void*)&dummy, 1, MPI_INT,
                        MPI_ANY_SOURCE, mpi->flctl_msg_tag,
                        MPI_COMM_WORLD, &status );
    MYASSERT( retcode == MPI_SUCCESS, ("MPI_Recv of dummy must succeed!") );

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI DummyRecd from %d\n",FMMPI_nodeid,from_pe);fflush(stdout);}
#endif

    from_pe = status.MPI_SOURCE;
    MYASSERT( (mpi->flctl_k > 0 &&
              (mpi->flctl_nsentto[from_pe] % mpi->flctl_k == 0)),
              ("%d: from_pe=%d nsent=%ld",FMMPI_nodeid,from_pe,
               mpi->flctl_nsentto[from_pe]) );
    MYASSERT( mpi->flctl_icantsendto[from_pe], ("icantsendto") );
    mpi->flctl_icantsendto[from_pe] = FALSE;
}

/*---------------------------------------------------------------------------*/
static void flctl_send_dummy( int src_pe )
{
    if(mpi->flctl_k > 0 &&
       (++mpi->flctl_nrecdfrom[src_pe] % mpi->flctl_k == 0))
    {
        int dummy = 0, retcode = 0;
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI DummySend to %d\n",FMMPI_nodeid,src_pe);fflush(stdout);}
#endif
        retcode = MPI_Bsend( (void*)&dummy, 1, MPI_INT,
                             src_pe, mpi->flctl_msg_tag, MPI_COMM_WORLD );
        MYASSERT( retcode == MPI_SUCCESS, ("MPI_Bsend of dummy must succeed!"));
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI DummySent to %d\n",FMMPI_nodeid,src_pe);fflush(stdout);}
#endif
    }
}

/*---------------------------------------------------------------------------*/
static void prerecv( int i )
{
    int retcode = MPI_Irecv( mpi->prerecvbuf[i], mpi->prerecvbufsz, MPI_PACKED,
                             MPI_ANY_SOURCE, mpi->msg_tag, MPI_COMM_WORLD,
                             &mpi->prerecvreq[i] );
    MYASSERT( retcode == MPI_SUCCESS, ("MPI_Irecv %d",retcode) );
}

/*---------------------------------------------------------------------------*/
void FMMPI_portals_init( void )
{
    char *useportalsstr= getenv("FMMPI_USEPORTALS");  /*TRUE or FALSE*/

    use_portals = useportalsstr&&!strcmp(useportalsstr,"TRUE");
    if( use_portals )
    {
        #if !PORTALS_AVAILABLE
            MYASSERT(!use_portals,("%d: FMMPI Portals not available\n",FMMPI_nodeid));
            use_portals = FALSE;
        #else /*PORTALS_AVAILABLE*/
#if !NODEBUG
if(mpifmdbg>=0){printf("%d: FMMPI Using Portals\n",FMMPI_nodeid);fflush(stdout);}
#endif

            FMPTL_Init( FMMPI_nodeid );
            MPI_Barrier( MPI_COMM_WORLD );

#if !NODEBUG
if(mpifmdbg>=0){printf("%d: FMMPI Portals initialized\n",FMMPI_nodeid);fflush(stdout);}
#endif
        #endif /*PORTALS_AVAILABLE*/
    }
    else
    {
#if !NODEBUG
        #if PORTALS_AVAILABLE
if(mpifmdbg>=0){printf("%d: FMMPI Portals available but not used\n",FMMPI_nodeid);fflush(stdout);}
        #else
if(mpifmdbg>=0){if(0){printf("%d: FMMPI Portals not available\n",FMMPI_nodeid);fflush(stdout);}}
        #endif /*PORTALS_AVAILABLE*/
#endif
    }
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
void FMMPI_pre_initialize(int *pac, char ***pav)
{
    if( !mpi || !mpi->inited )
    {
        int i = 0, rank = -1, size = 0;
        char *estr = getenv("FMMPI_DEBUG");
        char *pprstr = getenv("FMMPI_PREPOSTRECV"); /*TRUE or FALSE*/
        char *pprcstr = getenv("FMMPI_PREPOSTRECVCOUNT"); /*Integer >0*/
        char *testanystr = getenv("FMMPI_PREPOSTTESTANY"); /*TRUE or FALSE*/
        mpifmdbg = estr ? atoi(estr) : 1;

#if !NODEBUG
if(mpifmdbg>=2){printf("FMMPI_DEBUG=%d\n",mpifmdbg);fflush(stdout);}
#endif

#if !NODEBUG
if(mpifmdbg>=2){printf("FMMPI_pre_init() started.\n");fflush(stdout);}
#endif

        MPI_Init( pac, pav );
        MPI_Comm_rank( MPI_COMM_WORLD, &rank );
        MPI_Comm_size( MPI_COMM_WORLD, &size );
        MPI_Barrier( MPI_COMM_WORLD );

        mpi = (FMMPIState*)calloc(1,sizeof(FMMPIState));
        MYASSERT( mpi, ("Need %lu bytes",sizeof(FMMPIState)) );

        mpi->inited = 1;
        mpi->nodeid = FMMPI_nodeid = rank;
        mpi->numnodes = FMMPI_numnodes = size;
        mpi->msg_tag = 123;

        FMMPI_portals_init();

        #if STATICBUFS
        {
            mpi->attachbuflen = sizeof(mpi->attachbuf);
            mpi->packbuflen = sizeof(mpi->packbuf);
        }
        #else /*STATICBUFS*/
        {
            int nmsgs = (use_portals ? 1 : MAXPENDINGMSGS);
            int nbytes = nmsgs * (MAXBUFLEN + MPI_BSEND_OVERHEAD);

            mpi->attachbuf = (char*)malloc( nbytes );
            mpi->packbuf = (char*)malloc( MAXBUFLEN );

            mpi->attachbuflen = nbytes;
            mpi->packbuflen = MAXBUFLEN;
        }
        #endif /*STATICBUFS*/

        mpi->prerecv = use_portals ? FALSE :
                       (pprstr ? !strcmp(pprstr,"TRUE") : FALSE);
        mpi->prerecvbufcount = !mpi->prerecv ? 1 : (pprcstr?atoi(pprcstr):100);
        mpi->prerecvtestany = testanystr ? !strcmp(testanystr,"TRUE") : FALSE;
        mpi->prerecvbufsz = MAXBUFLEN;
        mpi->prerecvbufidx = mpi->prerecv ? -1 : 0;
        mpi->prerecvbuf = (char **)malloc(mpi->prerecvbufcount*sizeof(char*));
        mpi->prerecvreq = (MPI_Request *)
                           malloc(mpi->prerecvbufcount*sizeof(MPI_Request));
        mpi->prerecvstat = (MPI_Status *)
                           malloc(mpi->prerecvbufcount*sizeof(MPI_Status));
        mpi->prerecvtestcount = 0;
        mpi->prerecvtestidx = (int *)
                           malloc(mpi->prerecvbufcount*sizeof(int));
        for( i = 0; i < mpi->prerecvbufcount; i++ )
        {
            char *buf = (char *)malloc(mpi->prerecvbufsz*sizeof(char));
            MYASSERT( buf, ("Allocate %d bytes for %d of %d prerecv bufs",
                            mpi->prerecvbufsz, i, mpi->prerecvbufcount) );
            mpi->prerecvbuf[i] = buf;
        }

        mpi->probetot = 0;
        mpi->probereadytot = 0;
        mpi->probenztot = 0;
        mpi->probeprevready = FALSE;

        MPI_Barrier( MPI_COMM_WORLD );
        if( mpi->prerecv )
        {
            int i = 0;
            for( i = 0; i < mpi->prerecvbufcount; i++ )
            {
                prerecv( i );
            }
        }
        MPI_Barrier( MPI_COMM_WORLD );

        mpi->flctl_ppsbuflist = 0;
        mpi->flctl_msg_tag = 456;
        mpi->flctl_k = getenv("FMMPI_FLCTLK") ?
	                 atoi(getenv("FMMPI_FLCTLK")) : 0; /*CUSTOMIZE*/

        MPI_Buffer_attach( mpi->attachbuf, mpi->attachbuflen );

        MPI_Barrier( MPI_COMM_WORLD );

if(FMMPI_nodeid==0||mpifmdbg>1){printf( "FMMPI_nodeid=%d, FMMPI_numnodes=%d FMMPI_FLCTLK=%ld\n", FMMPI_nodeid, FMMPI_numnodes, mpi->flctl_k);fflush(stdout);}
    }
}

/*---------------------------------------------------------------------------*/
void FMMPI_post_initialize( void )
{
    LSCFGLD( "FMMPI_DEBUG", (long)mpifmdbg,
             "FMMPI debug intensity" );
    LSCFGST( "FMMPI_USEPORTALS", (use_portals?"TRUE":"FALSE"),
             "FMMPI uses portals instead of MPI?" );
    LSCFGLD( "FMMPI_FLCTLK", (long)mpi->flctl_k,
             "FMMPI MPI flow control param" );
    LSCFGST( "FMMPI_PREPOSTRECV", (mpi->prerecv?"TRUE":"FALSE"),
             "FMMPI uses preposted sends/recvs" );
    LSCFGLD( "FMMPI_PREPOSTRECVCOUNT", (long)mpi->prerecvbufcount,
             "FMMPI if prerecv, #recv bufs to prepost initially" );
    LSCFGST( "FMMPI_PREPOSTTESTANY", (mpi->prerecvtestany?"TRUE":"FALSE"),
             "FMMPI if prerecv, use testany instead of testsome " );
#if 1 || !NODEBUG
if(mpi->nodeid==0 && mpifmdbg>=0){printf("FMMPI %susing prepost recv %d\n",(mpi->prerecv?"":"not "),mpi->prerecvbufcount);fflush(stdout);}
if(mpi->nodeid==0 && mpifmdbg>=0){printf("FMMPI using prepost test%s\n",(mpi->prerecvtestany?"any":"some"));fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void FMMPI_initialize( int nodeid, int numnodes, FMMPICallback *cb )
{
#if !NODEBUG
if(mpifmdbg>=2){printf("FMMPI_initialize() started.\n");fflush(stdout);}
#endif

    MYASSERT( cb, ("Message callback required") );
    fmcb = cb;

    {
        int argc = 0; char *targv[2] = {"argv0",0}, **argv = targv;
        FMMPI_pre_initialize( &argc, &argv );
        MYASSERT( nodeid == FMMPI_nodeid,
                  ("nodeid=%d rank=%d",nodeid,FMMPI_nodeid) );
        MYASSERT( numnodes == FMMPI_numnodes,
                  ("numnodes=%d size=%d",numnodes,FMMPI_numnodes) );
    }

    MYASSERT( 0 < mpi->numnodes && mpi->numnodes <= FMMPIMAXPE,
            ("#nodes %d must be in [1..%d]", mpi->numnodes, FMMPIMAXPE) );

    MYASSERT( 0 <= mpi->nodeid && mpi->nodeid < mpi->numnodes,
            ("Node %d must be in [0..%d]", mpi->nodeid, mpi->numnodes-1) );

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI joining barrier\n",FMMPI_nodeid);fflush(stdout);}
#endif
        MPI_Barrier( MPI_COMM_WORLD );
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI done barrier\n",FMMPI_nodeid);fflush(stdout);}
#endif

#if !NODEBUG
if(mpifmdbg>=2){printf("FMMPI_initialize() done.\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void FMMPI_finalize( void )
{
#if !NODEBUG
if(FMMPI_nodeid==0)
{
if(mpifmdbg>=1){printf("%d: FMMPI_finalize()\n",FMMPI_nodeid);fflush(stdout);}
if(mpifmdbg>=1){printf("%d: MPI_Barrier()\n",FMMPI_nodeid);fflush(stdout);}
}
#endif

if(mpifmdbg>=0){if(FMMPI_nodeid==0){printf("%d: MPI probetot %lu probereadytot %lu %10.5lf %% probenztot %lu\n",FMMPI_nodeid, mpi->probetot, mpi->probereadytot, (mpi->probetot<=0?0.0:(mpi->probereadytot*100.0/mpi->probetot)), mpi->probenztot);fflush(stdout);}}

    if( use_portals )
    {
        FMPTL_Finish( FMMPI_nodeid );
    }

    if( mpi->prerecv )
    {
        int i = 0;
        for( i = 0; i < mpi->prerecvbufcount; i++ )
        {
            if( mpi->prerecvreq[i] != MPI_REQUEST_NULL )
            {
                int retcode = MPI_Cancel( &mpi->prerecvreq[i] );
                MYASSERT( retcode == MPI_SUCCESS, ("retcode") );
                retcode = MPI_Request_free( &mpi->prerecvreq[i] );
                MYASSERT( retcode == MPI_SUCCESS, ("retcode") );
                MYASSERT( mpi->prerecvreq[i] == MPI_REQUEST_NULL, ("prerecvreq") );
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD); /*Synchronize everyone one last time*/
#if !NODEBUG
if(mpifmdbg>=3){printf("%d:SKIPPING CALLING MPI_Finalize()\n",FMMPI_nodeid);fflush(stdout);}
#endif
    if(0)MPI_Finalize(); /*Does an implicit MPI_Buffer_detach()*/
if(FMMPI_nodeid==0)
{
#if !NODEBUG
if(mpifmdbg>=1){printf("%d: FMMPI All done\n",FMMPI_nodeid);fflush(stdout);}
#endif
}
}

/*---------------------------------------------------------------------------*/
FMMPI_stream *FMMPI_begin_message( int recipient, int length, int handler,
    int src_id, int dest_id )
{
    FMMPISendMsg *msg = &mpi->send_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;
    IOVEC *iov = &msg->pieces[0];

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI_begin_message(to=%d, len=%d)\n",FMMPI_nodeid,recipient,length);fflush(stdout);}
#endif

    MYASSERT( 0 <= recipient && recipient < mpi->numnodes,
            ("Bad FMMPI recipient ID %d must be in [0..%d]",
             recipient, mpi->numnodes-1) );
    MYASSERT( 0 <= length && length <= FMMPIMAXPIECELEN,
            ("Msg size %d must be <= %d", length, FMMPIMAXPIECELEN));

    hdr->src_id = src_id;
    hdr->dest_id = dest_id;
    hdr->src_pe = mpi->nodeid;
    hdr->dest_pe = recipient;
    hdr->handler = handler;
    hdr->npieces = 1;
    hdr->piecelen[0] = sizeof(*hdr);
    hdr->totbytes = sizeof(*hdr);

    iov->iov_base = (char *)hdr;
    iov->iov_len = hdr->totbytes;

    return (FMMPI_stream *)msg;
}

/*---------------------------------------------------------------------------*/
void FMMPI_send_piece( FMMPI_stream *sendstream, void *buffer, int length )
{
    FMMPISendMsg *msg = &mpi->send_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI_send_piece(buf=%p,len=%d)\n",FMMPI_nodeid,buffer,length);fflush(stdout);}
#endif

    MYASSERT( sendstream == (FMMPI_stream *)msg, ("!") );
if(0)/*XXX Don't need to check for SSIZE_MAX limit for purely MPI-only runs*/
    MYASSERT( buffer && 0 <= length && length <= (SSIZE_MAX-hdr->totbytes),
            ("buffer=%p, length = %d SSIZE_MAX=%d", buffer, length, SSIZE_MAX));
    MYASSERT( hdr->npieces < FMMPIMAXPIECES,
              ("No. of pieces can't exceed compiled %d pieces",FMMPIMAXPIECES));
    MYASSERT( length <= FMMPIMAXPIECELEN,
              ("Piecelen %d can't exceed compiled %d",length,FMMPIMAXPIECELEN));
    MYASSERT( hdr->totbytes+length <= mpi->packbuflen,
              ("%d + %d <= %d", hdr->totbytes, length, mpi->packbuflen) );

    {
    int pn = hdr->npieces++;
    IOVEC *iov = &msg->pieces[pn];
    iov->iov_base = buffer;
    iov->iov_len = length;
    hdr->piecelen[pn] = length;
    hdr->totbytes += length;
    }
}

/*---------------------------------------------------------------------------*/
static void do_mpi_send( const FMMPISendMsg *msg )
{
#if !NODEBUG
if(mpifmdbg>=2){printf("%d:do_mpi_send()\n",FMMPI_nodeid);fflush(stdout);}
#endif

  if( use_portals )
  {
    FMPTL_SendMesg( FMMPI_nodeid, msg );
  }
  else
  {
    int i = 0, position = 0, retcode = 0;
    const FMMPIMsgHeaderPiece *hdr = &msg->hdr;

    for( i = 0; i < hdr->npieces; i++ )
    {
        const IOVEC *piece = &msg->pieces[i];
        retcode = MPI_Pack( piece->iov_base, piece->iov_len, MPI_BYTE,
                           (void*)mpi->packbuf, mpi->packbuflen, &position,
                           MPI_COMM_WORLD );
        MYASSERT( retcode == MPI_SUCCESS, ("MPI_Pack must succeed!") );
    }

    retcode = MPI_Bsend( mpi->packbuf, position, MPI_PACKED,
                         hdr->dest_pe, mpi->msg_tag, MPI_COMM_WORLD );
    MYASSERT( retcode == MPI_SUCCESS, ("MPI_Send must succeed!") );
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI Bsent to %d\n",FMMPI_nodeid,hdr->dest_pe);fflush(stdout);}
#endif

    mpi->flctl_nsentto[hdr->dest_pe]++;
    if( mpi->flctl_k > 0 &&
        (mpi->flctl_nsentto[hdr->dest_pe] % mpi->flctl_k == 0))
    {
        mpi->flctl_icantsendto[hdr->dest_pe] = TRUE;
    }
  }
}

/*---------------------------------------------------------------------------*/
static void service_pps( void )
{
    Flctl_PPSBuf **pfbuf = &mpi->flctl_ppsbuflist;
    while( *pfbuf )
    {
        long dest_pe = (*pfbuf)->msg.hdr.dest_pe;
        if( !mpi->flctl_icantsendto[dest_pe] )
        {
            Flctl_PPSBuf *fbuf = (*pfbuf);
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() do_mpi_send\n",FMMPI_nodeid);fflush(stdout);}
#endif
            do_mpi_send( &((*pfbuf)->msg) );
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() delink\n",FMMPI_nodeid);fflush(stdout);}
#endif
            flctl_ppsbuf_delink( pfbuf );
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() delete\n",FMMPI_nodeid);fflush(stdout);}
#endif
            flctl_ppsbuf_delete( fbuf );
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() done if\n",FMMPI_nodeid);fflush(stdout);}
#endif
        }
        else
        {
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() else\n",FMMPI_nodeid);fflush(stdout);}
#endif
            pfbuf = &((*pfbuf)->next);
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:service_pps() done else\n",FMMPI_nodeid);fflush(stdout);}
#endif
        }
    }
}

/*---------------------------------------------------------------------------*/
void FMMPI_end_message( FMMPI_stream *sendstream )
{
    FMMPISendMsg *msg = &mpi->send_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: FMMPI_end_message()\n",FMMPI_nodeid);fflush(stdout);}
#endif

    MYASSERT( sendstream == (FMMPI_stream *)msg, ("!") );
    MYASSERT( hdr->totbytes <= mpi->packbuflen,
              ("%d %d", hdr->totbytes, mpi->packbuflen) );

    if( mpi->flctl_icantsendto[hdr->dest_pe] )
    {
        Flctl_PPSBuf *ppsbuf = 0;
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() new\n",FMMPI_nodeid);fflush(stdout);}
#endif
        ppsbuf = flctl_ppsbuf_new();
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() deepcopy\n",FMMPI_nodeid);fflush(stdout);}
#endif
        fmmpi_msg_deepcopy(&ppsbuf->msg, msg);
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() prepend\n",FMMPI_nodeid);fflush(stdout);}
#endif
        flctl_ppsbuf_prepend( &mpi->flctl_ppsbuflist, ppsbuf );
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() done if\n",FMMPI_nodeid);fflush(stdout);}
#endif
    }
    else
    {
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() do_mpi_send\n",FMMPI_nodeid);fflush(stdout);}
#endif
        do_mpi_send( msg );
#if !NODEBUG
if(mpifmdbg>=5){printf("%d: FMMPI_end_message() done else\n",FMMPI_nodeid);fflush(stdout);}
#endif
    }
}

/*---------------------------------------------------------------------------*/
int FMMPI_receive( void *buffer, FMMPI_stream *receivestream,
                   unsigned int length )
{
    FMMPIRecvMsg *msg = &mpi->recv_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;
    int pn = 0, retcode = 0;

    MYASSERT( receivestream == msg, ("cyclic check") );

    pn = msg->npieces_recd++;

    if( pn > 0 )
    {
        MYASSERT( pn < hdr->npieces,
                ("Only #%d pieces", hdr->npieces) );
        MYASSERT( length == hdr->piecelen[pn],
                ("request len %d != sent len %d", length, hdr->piecelen[pn]));
        MYASSERT( length <= hdr->totbytes - msg->nbytes_recd,
                ("%d, %d, %d", length, hdr->totbytes, msg->nbytes_recd) );
    }

  if( use_portals )
  {
    FMPTL_RecvPiece( buffer, length );
  }
  else
  {
    retcode = MPI_Unpack( mpi->prerecvbuf[mpi->prerecvbufidx],mpi->prerecvbufsz,
                     &msg->position, buffer, length, MPI_BYTE, MPI_COMM_WORLD );
    MYASSERT( retcode == MPI_SUCCESS, ("MPI_Unpack must succeed!") );
  }

    msg->nbytes_recd += length;

    return length;
}

/*---------------------------------------------------------------------------*/
static int service_mesg( int from_pe )
{
    int i = 0, nbytes = 0, nrecd = 0, retcode = 0;
    FMMPIRecvMsg *msg = &mpi->recv_msg;
    FMMPIMsgHeaderPiece temphdr, *hdr = &msg->hdr;

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_mesg(from_pe=%d)\n",FMMPI_nodeid,from_pe);fflush(stdout);}
#endif

    msg->nbytes_recd = 0; msg->npieces_recd = 0; msg->position = 0;

    hdr->src_id = -1; hdr->dest_id = -1;
    hdr->src_pe = from_pe; hdr->dest_pe = mpi->nodeid;
    hdr->handler = 0; hdr->npieces = 0; hdr->totbytes = 0;

    nrecd = FMMPI_receive( &temphdr, msg, sizeof(temphdr) );

    if( nrecd <= 0 )
    {
        /*Do nothing*/
#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_mesg() nrecd=0!\n",FMMPI_nodeid);fflush(stdout);}
#endif
        MYASSERT( 0, ("NRECD=0") );
    }
    else
    {
        MYASSERT( temphdr.src_pe == hdr->src_pe,
            ("src pes must agree: %d != %d", temphdr.src_pe, hdr->src_pe) );
        MYASSERT( temphdr.dest_pe == hdr->dest_pe,
            ("dest pes must agree: %d != %d", temphdr.dest_pe, hdr->dest_pe) );
        MYASSERT( temphdr.totbytes >= sizeof(*hdr),
            ("msg size: %d >= %lu", temphdr.totbytes, sizeof(*hdr)) );
        MYASSERT( temphdr.piecelen[0] == sizeof(*hdr),
            ("piecelen[0] %d != hdr sz %lu",temphdr.piecelen[0],sizeof(*hdr)));

        *hdr = temphdr;

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_mesg(from_pe=%d) calling fmcb(...,src_pe=%d, src_id=%d, dest_id=%d)\n",FMMPI_nodeid, from_pe, hdr->src_pe, hdr->src_id, hdr->dest_id);fflush(stdout);}
#endif

        fmcb( hdr->handler, msg, hdr->src_pe, hdr->src_id, hdr->dest_id );

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_mesg(from_pe=%d) done fmcb\n", FMMPI_nodeid, from_pe);fflush(stdout);}
#endif

        nrecd = 0;
        for( i = msg->npieces_recd; i < hdr->npieces; i++ )
        {
            char temp_piece[FMMPIMAXPIECELEN];
            int len = hdr->piecelen[i];
            MYASSERT( len <= sizeof(temp_piece),
                      ("%d %lu",len,sizeof(temp_piece)) );
            nrecd = FMMPI_receive( temp_piece, msg, len );
            if( nrecd <= 0 ) break;
        }

        MYASSERT( nrecd <= 0 || msg->nbytes_recd == hdr->totbytes,
            ("%d, %d", msg->nbytes_recd, hdr->totbytes) );

        nbytes = msg->nbytes_recd;

        flctl_send_dummy( hdr->src_pe );
    }

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_mesg(from_pe=%d) all done\n",FMMPI_nodeid, from_pe);fflush(stdout);}
#endif

    return nbytes;
}

/*---------------------------------------------------------------------------*/
static int service_recv( void )
{
    int nbytes = 0, retcode = 0, from_pe = 0;
    MPI_Status status;

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: MPI_Recv() started\n",FMMPI_nodeid);fflush(stdout);}
#endif

    retcode = MPI_Recv( mpi->prerecvbuf[mpi->prerecvbufidx], mpi->prerecvbufsz,
                        MPI_PACKED, MPI_ANY_SOURCE, mpi->msg_tag,
                        MPI_COMM_WORLD, &status );
    MYASSERT( retcode == MPI_SUCCESS, ("MPI_Recv must succeed!") );
    from_pe = status.MPI_SOURCE;

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: MPI_Recv() got msg from %d tag %d\n",FMMPI_nodeid,from_pe,mpi->msg_tag);fflush(stdout);}
#endif

    nbytes += service_mesg( from_pe );

    return nbytes;
}

/*---------------------------------------------------------------------------*/
static int service_prerecv( void )
{
    int nbytes = 0, tsi = 0;
    for( tsi = 0; tsi < mpi->prerecvtestcount; tsi++ )
    {
        int from_pe = -1;
        MPI_Request *req = 0;
        MPI_Status *stat = 0;
        int pri = mpi->prerecvtestidx[tsi];

        MYASSERT( 0 <= pri && pri < mpi->prerecvbufcount, ("Sanity %d",pri) );
        req = &mpi->prerecvreq[pri];
        stat = &mpi->prerecvstat[tsi];
        from_pe = stat->MPI_SOURCE;
        mpi->prerecvbufidx = pri;
        nbytes += service_mesg( from_pe );
        mpi->prerecvbufidx = -1;
        MYASSERT( *req == MPI_REQUEST_NULL, ("Must've been reset %d",pri) );
    }

    for( tsi = 0; tsi < mpi->prerecvtestcount; tsi++ )
    {
        int pri = mpi->prerecvtestidx[tsi];
        prerecv( pri );
    }

#if !NODEBUG
if(mpifmdbg>=2){printf("%d: service_prerecv() done %d\n",FMMPI_nodeid,mpi->prerecvtestcount);fflush(stdout);}
#endif

    return nbytes;
}

/*---------------------------------------------------------------------------*/
static int poll_all_once( unsigned int maxbytes )
{
    int nbytes = 0;
if(mpifmdbg>=6){printf("%d: poll_all_once() %u\n",FMMPI_nodeid,maxbytes);fflush(stdout);}

    while( (nbytes < maxbytes) || mpi->flctl_ppsbuflist )
    {
        int ready1 = 0, retcode1 = 0; MPI_Status status1;
        int ready2 = 0, retcode2 = 0; MPI_Status status2;
        int ready3 = 0, retcode3 = 0; MPI_Status status3;

        if( !mpi->prerecv && nbytes < maxbytes )
        {
            retcode1 = MPI_Iprobe( MPI_ANY_SOURCE, mpi->msg_tag,
                                   MPI_COMM_WORLD, &ready1, &status1 );
            MYASSERT( retcode1 == MPI_SUCCESS, ("MPI_Iprobe must succeed!") );
            mpi->probetot++;
        }

        if( mpi->flctl_k > 0 )
        {
            retcode2 = MPI_Iprobe( MPI_ANY_SOURCE, mpi->flctl_msg_tag,
                                   MPI_COMM_WORLD, &ready2, &status2 );
            MYASSERT( retcode2 == MPI_SUCCESS, ("MPI_Iprobe must succeed!") );
            mpi->probetot++;
        }

        if( mpi->prerecv && nbytes < maxbytes )
        {
            mpi->prerecvtestcount = 0;
            if( mpi->prerecvtestany )
            {
                int index = -1, flag = -1;
                MPI_Status status;
                retcode3 = MPI_Testany( mpi->prerecvbufcount, mpi->prerecvreq,
                                        &index, &flag, &status );
                MYASSERT(retcode3==MPI_SUCCESS, ("MPI_Testany must succeed!"));
                ready3 = (flag != 0);
                MYASSERT(!ready3 || (0<= index && index < mpi->prerecvbufcount),
                          ("MPI_Testany flag sanity check") );
                MYASSERT( ready3 || (index == MPI_UNDEFINED),
                          ("MPI_Testany index sanity check") );
                if( ready3 )
                {
                    mpi->prerecvtestcount = 1;
                    mpi->prerecvtestidx[0] = index;
                    mpi->prerecvstat[0] = status;
                }
            }
            else
            {
                retcode3 = MPI_Testsome( mpi->prerecvbufcount, mpi->prerecvreq,
                                         &mpi->prerecvtestcount,
                                         mpi->prerecvtestidx,
                                         mpi->prerecvstat );
                MYASSERT(retcode3==MPI_SUCCESS, ("MPI_Testsome must succeed!"));
                ready3 = (mpi->prerecvtestcount > 0);
            }
            mpi->probetot++;
        }

#if !NODEBUG
if(mpifmdbg>=6){if(ready1){printf("%d: MPI_Iprobe() ready1=%d\n",FMMPI_nodeid,ready1);fflush(stdout);}}
if(mpifmdbg>=6){if(ready2){printf("%d: MPI_Iprobe() ready2=%d\n",FMMPI_nodeid,ready2);fflush(stdout);}}
if(mpifmdbg>=6){if(ready3){printf("%d: MPI_Testsome() ready3=%d\n",FMMPI_nodeid,ready3);fflush(stdout);}}
#endif

        if( !ready1 && !ready2 && !ready3 && !mpi->flctl_ppsbuflist ) break;

        if( ready1 )
        {
            nbytes += service_recv();
            mpi->probereadytot++;
        }

        if( ready2 )
        {
            flctl_recv_dummy();
            mpi->probereadytot++;
        }

        if( ready3 )
        {
            nbytes += service_prerecv();
            mpi->probereadytot++;
        }

        if( mpi->flctl_ppsbuflist )
        {
            service_pps();
        }
    }
if(mpifmdbg>=6){printf("%d: done poll_all_once() %d\n",FMMPI_nodeid,nbytes);fflush(stdout);}

    return nbytes;
}

/*---------------------------------------------------------------------------*/
int FMMPI_extract( unsigned int maxbytes )
{
    int nbytes = 0, niter = 0;

#if !NODEBUG
if(mpifmdbg>=10){printf("%d: FMMPI_extract(%u)\n",FMMPI_nodeid,maxbytes);fflush(stdout);}
#endif

if( mpi->numnodes <= 1 ) return 0;

    while( nbytes < maxbytes )
    {
      if( use_portals )
      {
        int from_pe = -1;
        FMMPIRecvMsg *rmsg = &mpi->recv_msg;
        int got = FMPTL_RecvMesg( FMMPI_nodeid, rmsg, &from_pe, niter );
        mpi->probetot++;
        if( !got ) break;
        mpi->probereadytot++;
        nbytes += service_mesg( from_pe );
      }
      else
      {
        int m = poll_all_once( maxbytes-nbytes );
        if( m <= 0 ) break;
        nbytes += m;
      }

      niter++;
    }

    if( !mpi->probeprevready && niter > 0 ) { mpi->probenztot++; }
    mpi->probeprevready = niter > 0;

    return nbytes;
}

/*---------------------------------------------------------------------------*/
int FMMPI_numpieces( FMMPI_stream *mpi_stream )
{
    FMMPIRecvMsg *msg = &mpi->recv_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;
    MYASSERT( mpi_stream == msg, ("!") );
    return hdr->npieces-1;
}

/*---------------------------------------------------------------------------*/
int FMMPI_piecelen( FMMPI_stream *mpi_stream, int i )
{
    FMMPIRecvMsg *msg = &mpi->recv_msg;
    FMMPIMsgHeaderPiece *hdr = &msg->hdr;

    MYASSERT( mpi_stream == msg, ("!") );
    MYASSERT( 0<=i && i < hdr->npieces-1, ("Only #%d pieces", hdr->npieces-1) );

    return hdr->piecelen[i+1];
}

/*---------------------------------------------------------------------------*/
int FMMPI_debug_level( int level )
{
    int old = mpifmdbg;
    mpifmdbg = level;
    return old;
}

/*---------------------------------------------------------------------------*/
