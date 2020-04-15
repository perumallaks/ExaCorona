/*---------------------------------------------------------------------------*/
/* Portable shared-memory FM-like implementation using SysV shm calls.       */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mycompat.h"
#include "fmshm.h"

#if !SHM_AVAILABLE
    #define IPC_RMID 0
    #define IPC_CREAT 0
    #define IPC_EXCL 0
    struct shmid_ds { unsigned long shm_nattch; };
    #define shmctl(x,y,z) 0
    #define shmget(x,y,z) -1
    #define shmat(x,y,z) ((void *)-1)
    #define shmdt(x) (void)0
    #define getuid() -1
#else
    #include <sys/types.h>
    #include <sys/ipc.h>
    #include <sys/shm.h>
    #include <sys/sem.h>
    #include <unistd.h>
    #include <sched.h>
#endif

/*---------------------------------------------------------------------------*/
static int shmfmdbg = 0;

/*---------------------------------------------------------------------------*/
typedef int SHMMsgID;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int length;    /*Actual #bytes in this piece*/
    int offset;    /*Into data byte array; pointer alignment accounted for*/
} SHMMsgPieceSpec;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int npieces;                        /*Actual #pieces in this message*/
    SHMMsgPieceSpec pspec[SHMMAXPIECES];/*Pieces pointing into data array*/
    char pdata[SHMMAXDATALEN];          /*Compacted/linearized data of pieces*/
} SHMMsgPieces;

/*---------------------------------------------------------------------------*/
typedef struct _SHMMsg
{
    int synch;
    int src_id;
    int dest_id;
    int handler;
    int src_pe;
    int dest_pe;
    SHMMsgPieces pieces;
    SHMMsgID msg_id;
    SHMMsgID next;
} SHMMsg;

/*---------------------------------------------------------------------------*/
typedef struct
{
     struct
     {
	 struct
	 {
             SHMMsgID msgq;  /*Circular*/
	     SHMMsgID sendp; /*Sender's current buffer in msgq*/
	     SHMMsgID recvp; /*Receiver's current buffer in msgq*/
	 } to[SHMMAXPE];
     } from[SHMMAXPE];
     int bar[SHMMAXPE]; 
     SHMMsg msgs[1]; /*Actual array size determined at runtime*/
} SHMSharedMemory;

/*---------------------------------------------------------------------------*/
int SHM_nodeid;
int SHM_numnodes;

/*---------------------------------------------------------------------------*/
static int shmkey = -1;
static int fmshmid = -1;
static SHMSharedMemory *fmshm = 0;
static int removed_shm = 0;
static SHMCallback *fmcb = 0;
static int nbufs[SHMMAXPE][SHMMAXPE];

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
static void config(void)
{
    char *estr = 0;

    estr = getenv("FMSHM_DEBUG");
    shmfmdbg = estr ? atoi(estr) : 0;
#if !NODEBUG
if(shmfmdbg>=1){printf("FMSHM_DEBUG=%d\n",shmfmdbg);fflush(stdout);}
#endif

    if( (estr = getenv("FMSHM_KEY")) )
        shmkey = atoi( estr );
    else
        shmkey = getuid();

#if !NODEBUG
if(shmfmdbg>=0){printf( "SHM_KEY=%d\n",shmkey);fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
static void allocate_buffers( int totbufs )
{
    int i, j, k;
    int bufs_sofar = 0;
    int n2 = SHM_numnodes*(SHM_numnodes-1);
    for( i = 0; i < SHM_numnodes; i++ )
    {
        for( j = 0; j < SHM_numnodes; j++ )
	{
	    nbufs[i][j] = ((i==j) ? 0 : (totbufs / n2));
	    bufs_sofar += nbufs[i][j];
	}
    }

    if(shmfmdbg>=1)
    {
        printf("Buffer allocation:\n");
        printf("-----------------\n");
        for( i = 0; i < SHM_numnodes; i++ )
        {
	    printf("From %3d: ", i);
            for( j = 0; j < SHM_numnodes; j++ )
	    {
	        printf("  %3d", nbufs[i][j]);
	    }
	    printf("\n");
        }
        printf("Total buffers=%d, leftover=%d\n",bufs_sofar,totbufs-bufs_sofar);
        printf("-----------------\n");
    }

    for( i = 0, bufs_sofar = 0; i < SHM_numnodes; i++ )
    {
	for( j = 0; j < SHM_numnodes; j++ )
	{
            fmshm->from[i].to[j].msgq = -1;
            fmshm->from[i].to[j].sendp = -1;
            fmshm->from[i].to[j].recvp = -1;
	    if( i != j )
	    {
		SHMMsgID first = -1, last = -1;
	        for( k = nbufs[i][j]-1; k >= 0; --k )
	        {
	            SHMMsg *msg = 0;
		    MYASSERT( bufs_sofar < totbufs, ("Out of buffers") );
		    msg = &fmshm->msgs[bufs_sofar];
		    msg->msg_id = bufs_sofar++;
		    msg->next = first; first = msg->msg_id;
		    if( last < 0 ) last = msg->msg_id;
	        }
		MYASSERT( first >= 0 && last >= 0, ("!") );
		fmshm->msgs[last].next = first;
		fmshm->from[i].to[j].msgq = first;
		fmshm->from[i].to[j].sendp = first;
		fmshm->from[i].to[j].recvp = first;
	    }
	}
    }

#if !NODEBUG
if(shmfmdbg>=1){printf("Buffers leftover %d\n", totbufs-bufs_sofar);fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
#define SYNCHVAL 1234
#define READER_READY(msg) (((msg) && ((msg)->synch != 0)) ? 1 : 0)
#define READER_LOCK(msg) do{ \
	MYASSERT((msg),("!")); \
if(shmfmdbg>=3){printf("READER_LOCK(%d:%d)\n",msg->msg_id,msg->synch);} \
	while(!READER_READY(msg)) {;} \
	MYASSERT( (msg)->synch==SYNCHVAL, ("READER_LOCK %d",SHM_nodeid) ); \
    }while(0)
#define READER_UNLOCK(msg) do{ \
	MYASSERT((msg),("!")); \
	MYASSERT( (msg)->synch==SYNCHVAL, ("READER_UNLOCK %d",SHM_nodeid) ); \
if(shmfmdbg>=3){printf("READER_UNLOCK(%d:%d)\n",msg->msg_id,msg->synch);} \
        (msg)->synch = 0; \
    }while(0)
#define WRITER_READY(msg) (((msg) && ((msg)->synch == 0)) ? 1 : 0)
#define WRITER_LOCK(msg) do{ \
	MYASSERT((msg),("!")); \
if(shmfmdbg>=3){printf("WRITER_LOCK(%d:%d)\n",msg->msg_id,msg->synch);} \
	while(!WRITER_READY(msg)) {;} \
	MYASSERT( (msg)->synch==0, ("WRITER_LOCK %d",SHM_nodeid) ); \
    }while(0)
#define WRITER_UNLOCK(msg) do{ \
	MYASSERT((msg),("!")); \
	MYASSERT( (msg)->synch==0, ("WRITER_UNLOCK %d",SHM_nodeid) ); \
if(shmfmdbg>=3){printf("WRITER_UNLOCK(%d:%d)\n",msg->msg_id,msg->synch);} \
	(msg)->synch = SYNCHVAL; \
    }while(0)

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
void SHM_initialize( int nodeid, int numnodes, SHMCallback *cb )
{
    MYASSERT( 1 <= numnodes && numnodes <= SHMMAXPE,
            ("#SHM nodes %d must lie in [1..%d]", numnodes, SHMMAXPE) );
    MYASSERT( 0 <= nodeid && nodeid < numnodes,
            ("SHM node ID must lie in [0..%d]", numnodes-1) );

    SHM_nodeid = nodeid;
    SHM_numnodes = numnodes;

#if !NODEBUG
if(shmfmdbg>=0){printf("SHM_nodeid=%d, SHM_numnodes=%d\n",SHM_nodeid,SHM_numnodes);fflush(stdout);}
#endif

    MYASSERT( cb, ("Message callback required") );
    fmcb = cb;

    config();

    if( SHM_numnodes > 1 )
    {
	double onemeg = 1024.0*1024, maxmem = 0;
	unsigned long njoined = 0;
	char *qbufstr = getenv("FMSHM_BUFSPERPEQ");
	char *maxmemstr = getenv("FMSHM_TOTMEMORY");
	int i = 0, nbufs_per_q = 0, totbufs = 0, totqueues = 0;
        int shmid, shmsize, shmflags;
	void *shm;

       if(!qbufstr){qbufstr=getenv("FMSHM_BUFSPERPE");} /*For backward compat*/

	nbufs_per_q = qbufstr ? atoi(qbufstr) : -1;
	maxmem = maxmemstr ? atof(maxmemstr) : -1;
	totqueues = (SHM_numnodes * (SHM_numnodes-1));

	if( nbufs_per_q <= 0 && maxmem <= 0 )
	{
	    maxmem = onemeg; /*1MB shared segment*/
#if !NODEBUG
if(shmfmdbg>=0){printf("Using default shared segment limit of %.4lf MB.\n",maxmem/(onemeg));fflush(stdout);}
#endif
	}
	else if( maxmem > 0 )
	{
#if !NODEBUG
if(shmfmdbg>=0){printf("Using specified shared segment limit of %.4lf MB.\n",maxmem/(onemeg));fflush(stdout);}
#endif
	    if( nbufs_per_q > 0 )
	    {
#if !NODEBUG
if(shmfmdbg>=0){printf("Overriding specified no. of bufs per PE Q of %d.\n",nbufs_per_q);fflush(stdout);}
#endif
	        nbufs_per_q = -1;
	    }
	}
	else
	{
	    maxmem = nbufs_per_q * totqueues * sizeof(SHMMsg);
#if !NODEBUG
if(shmfmdbg>=0){printf("Using specified no. of bufs per PE Q of %d, and computed shared segment limit of %.4lf MB.\n",nbufs_per_q,maxmem/(onemeg));fflush(stdout);}
#endif
	}

	MYASSERT( maxmem > 0, ("%lf %d", maxmem, nbufs_per_q) );
	nbufs_per_q = maxmem / (totqueues * sizeof(SHMMsg));
	totbufs = nbufs_per_q*totqueues;

#if !NODEBUG
if(shmfmdbg>=0){printf("Using %d bufs per PE Q, total Qs %d, total bufs %d.\n",nbufs_per_q,totqueues,totbufs);fflush(stdout);}
#endif

	if( SHM_nodeid == 0 )
	{
	    int oldshmid = shmget( shmkey, 1, 0 );
	    if( oldshmid >= 0 )
	    {
#if !NODEBUG
if(shmfmdbg>=1){printf("Deleting old zombie shm segment %d.\n",oldshmid);fflush(stdout);}
#endif
	        shmctl( oldshmid, IPC_RMID, 0 );
	    }
	}

	shmsize = sizeof(SHMSharedMemory) + totbufs*sizeof(SHMMsg);
	shmflags = 0666 | (SHM_nodeid==0 ? IPC_CREAT|IPC_EXCL : 0);
	do
	{
#if !NODEBUG
if(shmfmdbg>=1){printf("shmget(0x%x,%.4fMB,0x%x)\n",shmkey,shmsize/(onemeg),shmflags);fflush(stdout);}
#endif
	    shmid = shmget( shmkey, shmsize, shmflags );
	    MYASSERT( !(SHM_nodeid==0 && shmid < 0),
	            ("Node 0: shmkey %d bad or in use", shmkey); perror("") );
	    if( shmid < 0 )
	    {
#if !NODEBUG
if(shmfmdbg>=1){printf("SHM node %d: Retrying shmget...\n",SHM_nodeid);fflush(stdout);}
#endif
                sleep(1);
	    }
	}while(shmid < 0);

#if !NODEBUG
if(shmfmdbg>=1){printf("SHM node %d: shmid=%d.\n",SHM_nodeid,shmid);fflush(stdout);}
#endif

	fmshmid = shmid;

#if !NODEBUG
if(shmfmdbg>=1){printf("shmat(%x,%x,%x)\n",shmid,0,0);fflush(stdout);}
#endif
	shm = shmat( shmid, (char *)0, 0 );
	MYASSERT( shm != (void *)-1, ("shmat(shmid=%d)", shmid) );

	fmshm = (SHMSharedMemory *)shm;
#if !NODEBUG
if(shmfmdbg>=1){printf("fmshm=%p\n",fmshm);fflush(stdout);}
#endif

	if( SHM_nodeid == 0 )
	{
	    allocate_buffers( totbufs );
	}

#if !NODEBUG
if(shmfmdbg>=0){printf("Waiting for all SHM processors to join...\n");fflush(stdout);}
#endif
	for(;;)
	{
	    struct shmid_ds shmstat;
	    int x = shmctl( shmid, IPC_STAT, &shmstat );
	    MYASSERT( x == 0, ("shmctl failure"); perror("") );
	    njoined = shmstat.shm_nattch;

#if !NODEBUG
if(shmfmdbg>=1){printf("#joined = %lu\n", njoined);fflush(stdout);}
#endif

if(0)MYASSERT( 1 <= njoined && njoined <= SHM_numnodes, ("%lu",njoined) );
	    if( njoined >= SHM_numnodes ) break;

#if !NODEBUG
if(shmfmdbg>=1){printf( "%lu joined, %lu more to join\n",njoined,SHM_numnodes-njoined);fflush(stdout);}
#endif

	    sleep(2);
	}

#if !NODEBUG
if(shmfmdbg>=0){printf("All %lu processors joined.\n",njoined);fflush(stdout);}
#endif

	if( SHM_nodeid == 0 )
	{
	    int nbar = 0;
	    fmshm->bar[0] = SHM_numnodes;
	    while(1)
	    {
		int i = 0;
		for( i = 1, nbar = 1; i < SHM_numnodes; i++ )
		{
		    int x = fmshm->bar[i];
		    if( x == 1 ) nbar++;
		}
		if( nbar == SHM_numnodes )
		{
		    break;
		}
		else
		{
#if !NODEBUG
if(shmfmdbg>=1){printf("SHM node 0: Waiting for %d shm nodes to reach barrier...\n",SHM_numnodes-nbar);fflush(stdout);}
#endif
		    sleep(1);
		}
	    }
	    fmshm->bar[0] = 0; /*Prevent advancing of next session's feds*/
	}
	else
	{
	    volatile int x = 0;
	    while( (x = fmshm->bar[0]) == 0 )
	    {
#if !NODEBUG
if(shmfmdbg>=1){printf("SHM node %d: Waiting for shm node 0 to reach barrier...\n",SHM_nodeid);fflush(stdout);}
#endif
		sleep(1);
	    }
	    fmshm->bar[SHM_nodeid] = 1;
	}
#if !NODEBUG
if(shmfmdbg>=1){printf("Reached barrier.\n");fflush(stdout);}
#endif

	for( i = 0; i < SHM_numnodes; i++ )
	{
	    int j = 0;
	    for( j = 0; j < SHM_numnodes; j++ )
	    {
	       SHMMsgID mid = -1;
	       SHMMsg *m = 0;
               if( i != j )
	       {
#if !NODEBUG
if(shmfmdbg>=2)
{
printf("from[%d]to[%d]",i,j);
mid=fmshm->from[i].to[j].msgq; m=&fmshm->msgs[mid];
MYASSERT(mid==m->msg_id,("%d,%d",mid,m->msg_id)); printf("\tmsgq=%d",mid);
mid=fmshm->from[i].to[j].sendp; m=&fmshm->msgs[mid];
MYASSERT(mid==m->msg_id,("%d,%d",mid,m->msg_id)); printf("\tsendp=%d",mid);
mid=fmshm->from[i].to[j].recvp; m=&fmshm->msgs[mid];
MYASSERT(mid==m->msg_id,("%d,%d",mid,m->msg_id)); printf("\trecvp=%d",mid);
printf("\n"); fflush(stdout);
}
#endif
	       }
	    }
	}
    }

#if !NODEBUG
if(shmfmdbg>=0){printf("SHM_initialize() done.\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void SHM_finalize( void )
{
    if( !removed_shm && fmshmid >= 0 )
    {
        shmctl( fmshmid, IPC_RMID, 0 ); removed_shm = 1;
    }
    fmshmid = -1;

    if( fmshm ) shmdt( (void *)fmshm );
    fmshm = 0;
}

/*---------------------------------------------------------------------------*/
SHM_stream *SHM_begin_message( int recipient, int length, int handler,
    int src_id, int dest_id )
{
    SHM_stream *stream = 0;
    SHMMsgID mid = -1;
    SHMMsg *msg = 0;
    unsigned long ntries = 0, maxtries = 4000000000LU;
    int from = SHM_nodeid, to = recipient;

    MYASSERT( 0 <= to && to < SHM_numnodes && to != from, ("%d", to) );
    MYASSERT( length <= SHMMAXDATALEN,
            ("SHM msg size %d can't exceed compiled %d",length,SHMMAXDATALEN));

    mid = fmshm->from[from].to[to].sendp;
    msg = &fmshm->msgs[mid];
    MYASSERT( msg && msg->msg_id==mid, ("from %d to %d", from, to) );
    for( ntries = 0; !WRITER_READY(msg) && ntries < maxtries; ntries++ ) {}

    if( !WRITER_READY(msg) )
    {
#if !NODEBUG
if(shmfmdbg>=1){printf("SHM node %d sending to %d with buffer ID %d - out of buffers or deadlocked\n",from,to,msg->msg_id);fflush(stdout);}
#endif
    }
    else
    {
        WRITER_LOCK(msg);
        {
        msg->src_id = src_id;
        msg->dest_id = dest_id;
        msg->src_pe = from;
        msg->dest_pe = to;
        msg->handler = handler;
        msg->pieces.npieces = 0;
        stream = (SHM_stream *)msg;
        }
#if !NODEBUG
if(shmfmdbg>=3){printf("SHM_begin_message() msg ID %d\n",msg->msg_id);fflush(stdout);}
#endif
    }

    return stream;
}

/*---------------------------------------------------------------------------*/
void SHM_send_piece( SHM_stream *sendstream, void *buffer, int length )
{
    SHMMsg *msg = (SHMMsg *)sendstream;
    SHMMsgPieces *pcs = 0;
    int offset = 0;

    MYASSERT( msg && msg->src_pe == SHM_nodeid, ("!" ) );
    MYASSERT( msg->msg_id==fmshm->from[msg->src_pe].to[msg->dest_pe].sendp,("!"));

    pcs = &msg->pieces;

    if( pcs->npieces > 0 )
    {
	SHMMsgPieceSpec *lastpc = &pcs->pspec[pcs->npieces-1];
	offset = lastpc->offset + lastpc->length;
	offset = ((offset-1)/16 + 1) * 16;
    }

    MYASSERT( offset+length <= SHMMAXDATALEN,
            ("SHM msg size %d can't exceed compiled %d",
	    offset+length, SHMMAXDATALEN) );

    MYASSERT( pcs->npieces < SHMMAXPIECES,
            ("SHM msg pieces can't exceed compiled %d pieces", SHMMAXPIECES) );
    {
    SHMMsgPieceSpec *newspec = &pcs->pspec[pcs->npieces++];
    newspec->offset = offset;
    newspec->length = length;
    memcpy( &pcs->pdata[offset], (char *)buffer, length );
    }
}

/*---------------------------------------------------------------------------*/
void SHM_end_message( SHM_stream *sendstream )
{
    SHMMsg *msg = (SHMMsg *)sendstream;

    MYASSERT( msg && msg->src_pe == SHM_nodeid, ("!" ) );
    MYASSERT( msg->msg_id==fmshm->from[msg->src_pe].to[msg->dest_pe].sendp,("!"));

#if !NODEBUG
if(shmfmdbg>=3){printf("SHM_end_message() msg ID %d\n",msg->msg_id);fflush(stdout);}
#endif
    {
	fmshm->from[msg->src_pe].to[msg->dest_pe].sendp = msg->next;
    }
    WRITER_UNLOCK( msg );
}

/*---------------------------------------------------------------------------*/
static int npieces_recd; /*#pieces received by user from current message*/

/*---------------------------------------------------------------------------*/
void SHM_receive( void *buffer, SHM_stream *receivestream, unsigned int length )
{
    SHMMsg *msg = (SHMMsg *)receivestream;
    int next_piece = npieces_recd++;
    SHMMsgPieces *pcs = 0;

    MYASSERT( msg && msg->dest_pe == SHM_nodeid, ("!" ) );
    MYASSERT( msg->msg_id==fmshm->from[msg->src_pe].to[msg->dest_pe].recvp,("!"));
    pcs = &msg->pieces;
    MYASSERT( next_piece < pcs->npieces, ("Only %d pieces exist", pcs->npieces) );

    {
    SHMMsgPieceSpec *next_spec = &pcs->pspec[next_piece];
    int offset = next_spec->offset;
    MYASSERT( length <= next_spec->length,
            ("Only %d < %u bytes in piece", next_spec->length, length) );
    memcpy( (char *)buffer, &pcs->pdata[offset], length );
    }
}

/*---------------------------------------------------------------------------*/
int SHM_extract( unsigned int maxbytes )
{
    int nbytes = 0;
    static int pe = 0;
    int i = 0;

    if( SHM_nodeid == 0 && !removed_shm && fmshmid >= 0 )
    {
        shmctl( fmshmid, IPC_RMID, 0 ); removed_shm = 1;
    }

    if( SHM_numnodes <= 1 ) return 0;

    while( nbytes < maxbytes )
    {
	int nready = 0;
        for( i = 0; i < SHM_numnodes-1; i++ )
        {
            if( pe == SHM_nodeid ) pe++;
	    pe %= SHM_numnodes;
	    if( pe != SHM_nodeid )
	    {
                SHMMsg *msg = 0;
		SHMMsgID mid = fmshm->from[pe].to[SHM_nodeid].recvp;
		MYASSERT( 0 <= mid, ("%d",mid) );
                msg = &fmshm->msgs[mid];
	        if( READER_READY( msg ) )
	        {
		    SHM_stream *shm_stream = (SHM_stream *)msg;

		    READER_LOCK( msg );
#if !NODEBUG
if(shmfmdbg>=3){printf("Detected incoming SHM msg src_pe=%d,src_id=%d,dest_id=%d!\n",msg->src_pe,msg->src_id,msg->dest_id);fflush(stdout);}
#endif
		    nready++;
		    npieces_recd = 0;
	            fmcb( msg->handler, shm_stream, msg->src_pe,
		          msg->src_id, msg->dest_id );
                    fmshm->from[pe].to[SHM_nodeid].recvp = msg->next;
		    nbytes += SHMMAXDATALEN;
		    READER_UNLOCK( msg );
	        }
	    }
	    pe++; pe %= SHM_numnodes;
        }
	if( nready <= 0 ) break;
    }

    return nbytes;
}

/*---------------------------------------------------------------------------*/
int SHM_numpieces( SHM_stream *shm_stream )
{
    SHMMsg *msg = (SHMMsg *)shm_stream;
    SHMMsgPieces *pcs = &msg->pieces;

    MYASSERT( msg->msg_id==fmshm->from[msg->src_pe].to[msg->dest_pe].recvp,("!"));
    return pcs->npieces;
}

/*---------------------------------------------------------------------------*/
int SHM_piecelen( SHM_stream *shm_stream, int i )
{
    SHMMsg *msg = (SHMMsg *)shm_stream;
    SHMMsgPieces *pcs = &msg->pieces;
    SHMMsgPieceSpec *spec = 0;

    MYASSERT(msg->msg_id==fmshm->from[msg->src_pe].to[msg->dest_pe].recvp,("!"));
    MYASSERT( 0 <= i && i < pcs->npieces, ("Only %d pieces", pcs->npieces) );

    spec = &pcs->pspec[i];

    return spec->length;
}

/*---------------------------------------------------------------------------*/
int SHM_debug_level( int level )
{
    int old = shmfmdbg;
    shmfmdbg = level;
    return old;
}

/*---------------------------------------------------------------------------*/
