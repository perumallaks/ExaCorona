/*---------------------------------------------------------------------------*/
/* Portable mixed communication FM implementation.                           */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mycompat.h"
#include "fmshm.h"
#include "fmgm.h"
#include "fmtcp.h"
#include "fmmpi.h"
#include "fm.h"

/*---------------------------------------------------------------------------*/
#define FMMAXPE MAX_PE

/*---------------------------------------------------------------------------*/
#define MAX(a,b) (((a)>(b)) ? (a) : (b))

/*---------------------------------------------------------------------------*/
#define FMMAXPIECES MAX( MAX(SHMMAXPIECES, GMMAXPIECES), TCPMAXPIECES )
#define FMMAXPIECELEN MAX( MAX(SHMMAXPIECELEN, GMMAXPIECELEN), TCPMAXPIECELEN )

/*---------------------------------------------------------------------------*/
#if TCP_AVAILABLE
  #define FMMAXHOSTNAMELEN 1000 /*CUSTOMIZE*/
#elif MPI_AVAILABLE
  #define FMMAXHOSTNAMELEN 4 /*CUSTOMIZE*/
#else
  #define FMMAXHOSTNAMELEN 100 /*CUSTOMIZE*/
#endif

typedef char FMNodeName[FMMAXHOSTNAMELEN];

/*---------------------------------------------------------------------------*/
struct XFaceStruct;
typedef struct XFaceStruct XFace;
typedef void *XFaceStream;

/*---------------------------------------------------------------------------*/
typedef enum
{
    FM_XFACE_CLASS_SHM,   /*Shared memory*/
    FM_XFACE_CLASS_MYR,   /*Myrinet*/
    FM_XFACE_CLASS_TCP,   /*TCP LAN or WAN*/
    FM_XFACE_CLASS_MPI,   /*MPI*/
    FM_XFACE_CLASS_NUM,   /*Total number of interface classes*/
    FM_XFACE_CLASS_NUL,   /*Void; drop it on the floor*/
    FM_XFACE_CLASS_BAD,   /*Invalid class type*/
} XFaceClassTag;

/*---------------------------------------------------------------------------*/
#define LINK_COST_SHM 10
#define LINK_COST_MYR 40
#define LINK_COST_LAN 100
#define LINK_COST_WAN 1000
#define LINK_COST_MPI 20
#define LINK_COST_INF 1e10

/*---------------------------------------------------------------------------*/
typedef enum
{
    FM_SUBNET_SHM,  /*Shared memory*/
    FM_SUBNET_MYR,  /*Myrinet*/
    FM_SUBNET_LAN,  /*TCP local area*/
    FM_SUBNET_WAN,  /*TCP wide area*/
    FM_SUBNET_MPI,  /*MPI virtual cluster*/
    FM_SUBNET_NUM,  /*Number of subnet types*/
    FM_SUBNET_BAD,  /*Invalid subnet type*/
} SubnetType;

/*---------------------------------------------------------------------------*/
#define XFACE_CLASS_STR(ct) \
		                    ((ct)==FM_XFACE_CLASS_SHM ? "SHM" :        \
		                    ((ct)==FM_XFACE_CLASS_MYR ? "MYR" :        \
		                    ((ct)==FM_XFACE_CLASS_TCP ? "TCP" :        \
		                    ((ct)==FM_XFACE_CLASS_MPI ? "MPI" :        \
		                    ((ct)==FM_XFACE_CLASS_NUL ? "NUL" :        \
		                    ((ct)==FM_XFACE_CLASS_BAD ? "BAD" :        \
				                                "???" ))))))

/*---------------------------------------------------------------------------*/
#define SUBNET_STR_TO_TYPE(sn) \
		                    (!strcmp((sn),"SHM") ? FM_SUBNET_SHM :\
		                    (!strcmp((sn),"MYR") ? FM_SUBNET_MYR :\
				    (!strcmp((sn),"LAN") ? FM_SUBNET_LAN :\
				    (!strcmp((sn),"WAN") ? FM_SUBNET_WAN :\
				    (!strcmp((sn),"MPI") ? FM_SUBNET_MPI :\
				                           FM_SUBNET_BAD)))))

/*---------------------------------------------------------------------------*/
#define SUBNET_TYPE_TO_XFACE_CLASS(sn) \
		                    ((sn)==FM_SUBNET_SHM ? FM_XFACE_CLASS_SHM :\
		                    ((sn)==FM_SUBNET_MYR ? FM_XFACE_CLASS_MYR :\
				    ((sn)==FM_SUBNET_LAN ? FM_XFACE_CLASS_TCP :\
				    ((sn)==FM_SUBNET_WAN ? FM_XFACE_CLASS_TCP :\
				    ((sn)==FM_SUBNET_MPI ? FM_XFACE_CLASS_MPI :\
				                         FM_XFACE_CLASS_NUL)))))

/*---------------------------------------------------------------------------*/
#define SUBNET_LINK_COST(sn) \
		                    ((sn)==FM_SUBNET_SHM ? LINK_COST_SHM :     \
		                    ((sn)==FM_SUBNET_MYR ? LINK_COST_MYR :     \
		                    ((sn)==FM_SUBNET_LAN ? LINK_COST_LAN :     \
		                    ((sn)==FM_SUBNET_WAN ? LINK_COST_WAN :     \
		                    ((sn)==FM_SUBNET_MPI ? LINK_COST_MPI :     \
				                           LINK_COST_INF)))))

/*---------------------------------------------------------------------------*/
typedef struct
{
    XFaceClassTag xf_cltag;

    void (*xf_new)(XFace *xf);
    void (*xf_delete)(XFace *xf);

    void (*xf_constructor)(XFace *xf, int i, int N, FMNodeName nodenames[]);
    void (*xf_destructor)(XFace *xf);

    void (*xf_begin_message)(XFace *xf, XFaceStream *pstream,
                             int, int, int, int, int);
    void (*xf_send_piece)(XFace *xf, XFaceStream stream,
                             void *buf, int len);
    void (*xf_end_message)(XFace *xf, XFaceStream stream);
    int  (*xf_extract)(XFace *xf, int max_bytes);
    int  (*xf_num_pieces)(XFace *xf, XFaceStream stream);
    int  (*xf_piece_len)(XFace *xf, XFaceStream stream, int piece_num);
    void (*xf_recv_piece)(XFace *xf, XFaceStream stream, void *buf, int maxlen);
    void *xf_callback;
    int   xf_maxpiecelen;
} XFaceClass;

/*---------------------------------------------------------------------------*/
typedef void *XFaceInstance;

/*---------------------------------------------------------------------------*/
struct XFaceStruct
{
    char *xf_name;
    int xf_subnet_id; /*Globally unique ID of the subnet this xface belongs to*/

    int xf_sub_id; /*ID of this xface in its subnet; 0 <= x_sub_id < xf_sub_n*/
    int xf_sub_n;  /*No. of peer nodes in the subnet this xface belongs to*/

    int xf_map[FMMAXPE];/*map[i]=xf_sub_id of i if FMID i is in subnet;else=-1*/
    int xf_inv_map[FMMAXPE]; /*inv_map[i]=FMID of i'th member; 0<=i<xf_sub_n*/
    FMNodeName xf_canname[FMMAXPE];/*canname[i]=canonical name of i'th member*/

    XFaceClass *xf_class;     /*Ref to this interface's class definition*/
    XFaceInstance xf_instance;/*Opaque ref to instance of this interface class*/
    int xf_constructed;       /*Constructor already called on this interface?*/
};

/*---------------------------------------------------------------------------*/
typedef struct
{
    int dest_id;     /*FM ID of final destination processor*/
    int next_hop;    /*FM ID of next hop processor*/
    XFace *xf;       /*Interface via which the next hop processor is reached*/
} XFaceNextHopEntry;

/*---------------------------------------------------------------------------*/
typedef struct
{
    XFaceNextHopEntry nhop_entry[FMMAXPE];
} XFaceNextHopTable;

/*---------------------------------------------------------------------------*/
int absorb_or_route( int handler, void *in_stream,
    int src_pe, int src_id, int dest_id);

/*---------------------------------------------------------------------------*/
void xf_shm_new(XFace *xf)
{
    xf->xf_instance = 0; /*Only one (static) instance supported by SHMFM*/
}
void xf_shm_delete(XFace *xf)
{
    /*Do nothing*/
}
void xf_shm_constructor(XFace *xf, int i, int N, FMNodeName nodenames[])
{
    SHM_initialize( i, N, (SHMCallback*)xf->xf_class->xf_callback );
}
void xf_shm_destructor(XFace *xf)
{
    SHM_finalize();
}
void xf_shm_begin_message(XFace *xf, XFaceStream *pstream,
    int recipient, int length, int handler, int src_id, int dest_id )
{
    *pstream = SHM_begin_message( recipient, length, handler, src_id, dest_id );
}
void xf_shm_send_piece(XFace *xf, XFaceStream stream, void *buf, int len)
{
    SHM_send_piece( stream, buf, len );
}
void xf_shm_end_message(XFace *xf, XFaceStream stream)
{
    SHM_end_message( stream );
}
int xf_shm_extract(XFace *xf, int max_bytes)
{
    return SHM_extract( max_bytes );
}
int xf_shm_num_pieces(XFace *xf, XFaceStream stream)
{
    return SHM_numpieces( stream );
}
int xf_shm_piece_len(XFace *xf, XFaceStream stream, int piece_num)
{
    return SHM_piecelen( stream, piece_num );
}
void xf_shm_recv_piece(XFace *xf, XFaceStream stream, void *buf, int maxlen)
{
    SHM_receive( buf, stream, maxlen );
}
int xf_shm_callback(int handler, SHM_stream *stream,
    int src_pe, int src_id, int dest_id)
{
    return absorb_or_route( handler, stream, src_pe, src_id, dest_id );
}

/*---------------------------------------------------------------------------*/
void xf_myr_new(XFace *xf)
{
    xf->xf_instance = 0; /*Only one (static) instance supported by GMFM*/
}
void xf_myr_delete(XFace *xf)
{
    /*Do nothing*/
}
void xf_myr_constructor(XFace *xf, int i, int N, FMNodeName nodenames[])
{
    int j = 0;
    char *nms[FMMAXPE];
    for(j=0;j<N;j++){nms[j]=nodenames[j];}

    GM_initialize( i, N, nms, (GMCallback*)xf->xf_class->xf_callback );
}
void xf_myr_destructor(XFace *xf)
{
    GM_finalize();
}
void xf_myr_begin_message(XFace *xf, XFaceStream *pstream,
    int recipient, int length, int handler, int src_id, int dest_id )
{
    *pstream = GM_begin_message( recipient, length, handler, src_id, dest_id );
}
void xf_myr_send_piece(XFace *xf, XFaceStream stream, void *buf, int len)
{
    GM_send_piece( stream, buf, len );
}
void xf_myr_end_message(XFace *xf, XFaceStream stream)
{
    GM_end_message( stream );
}
int xf_myr_extract(XFace *xf, int max_bytes)
{
    return GM_extract( max_bytes );
}
int xf_myr_num_pieces(XFace *xf, XFaceStream stream)
{
    return GM_numpieces( stream );
}
int xf_myr_piece_len(XFace *xf, XFaceStream stream, int piece_num)
{
    return GM_piecelen( stream, piece_num );
}
void xf_myr_recv_piece(XFace *xf, XFaceStream stream, void *buf, int maxlen)
{
    GM_receive( buf, stream, maxlen );
}
int xf_myr_callback(int handler, GM_stream *stream,
    int src_pe, int src_id, int dest_id)
{
    return absorb_or_route( handler, stream, src_pe, src_id, dest_id );
}

/*---------------------------------------------------------------------------*/
void xf_tcp_new(XFace *xf)
{
    xf->xf_instance = 0;
}
void xf_tcp_delete(XFace *xf)
{
    /*Do nothing*/
}
void xf_tcp_constructor(XFace *xf, int i, int N, FMNodeName nodenames[])
{
    int j = 0;
    char *nms[FMMAXPE];
    for(j=0;j<N;j++){nms[j]=nodenames[j];}

    TCP_initialize( nms, i, N, (TCPCallback*)xf->xf_class->xf_callback );
}
void xf_tcp_destructor(XFace *xf)
{
    TCP_finalize();
}
void xf_tcp_begin_message(XFace *xf, XFaceStream *pstream,
    int recipient, int length, int handler, int src_id, int dest_id )
{
    *pstream = TCP_begin_message( recipient, length, handler, src_id, dest_id );
}
void xf_tcp_send_piece(XFace *xf, XFaceStream stream, void *buf, int len)
{
    TCP_send_piece( stream, buf, len );
}
void xf_tcp_end_message(XFace *xf, XFaceStream stream)
{
    TCP_end_message( stream );
}
int xf_tcp_extract(XFace *xf, int max_bytes)
{
    return TCP_extract( max_bytes );
}
int xf_tcp_num_pieces(XFace *xf, XFaceStream stream)
{
    return TCP_numpieces( stream );
}
int xf_tcp_piece_len(XFace *xf, XFaceStream stream, int piece_num)
{
    return TCP_piecelen( stream, piece_num );
}
void xf_tcp_recv_piece(XFace *xf, XFaceStream stream, void *buf, int maxlen)
{
    TCP_receive( buf, stream, maxlen );
}
int xf_tcp_callback(int handler, TCP_stream *stream,
    int src_pe, int src_id, int dest_id)
{
    return absorb_or_route( handler, stream, src_pe, src_id, dest_id );
}

/*---------------------------------------------------------------------------*/
void xf_mpi_new(XFace *xf)
{
    xf->xf_instance = 0; /*Only one (static) instance supported by FMMPIFM*/
}
void xf_mpi_delete(XFace *xf)
{
    /*Do nothing*/
}
void xf_mpi_constructor(XFace *xf, int i, int N, FMNodeName nodenames[])
{
    FMMPI_initialize( i, N, (FMMPICallback*)xf->xf_class->xf_callback );
}
void xf_mpi_destructor(XFace *xf)
{
    FMMPI_finalize();
}
void xf_mpi_begin_message(XFace *xf, XFaceStream *pstream,
    int recipient, int length, int handler, int src_id, int dest_id )
{
    *pstream = FMMPI_begin_message(recipient, length, handler, src_id, dest_id);
}
void xf_mpi_send_piece(XFace *xf, XFaceStream stream, void *buf, int len)
{
    FMMPI_send_piece( stream, buf, len );
}
void xf_mpi_end_message(XFace *xf, XFaceStream stream)
{
    FMMPI_end_message( stream );
}
int xf_mpi_extract(XFace *xf, int max_bytes)
{
    return FMMPI_extract( max_bytes );
}
int xf_mpi_num_pieces(XFace *xf, XFaceStream stream)
{
    return FMMPI_numpieces( stream );
}
int xf_mpi_piece_len(XFace *xf, XFaceStream stream, int piece_num)
{
    return FMMPI_piecelen( stream, piece_num );
}
void xf_mpi_recv_piece(XFace *xf, XFaceStream stream, void *buf, int maxlen)
{
    FMMPI_receive( buf, stream, maxlen );
}
int xf_mpi_callback(int handler, FMMPI_stream *stream,
    int src_pe, int src_id, int dest_id)
{
    return absorb_or_route( handler, stream, src_pe, src_id, dest_id );
}

/*---------------------------------------------------------------------------*/
static XFaceClass xf_classes[FM_XFACE_CLASS_NUM] =
{
    {
	FM_XFACE_CLASS_SHM,
        xf_shm_new,           xf_shm_delete,
	xf_shm_constructor,   xf_shm_destructor,
	xf_shm_begin_message, xf_shm_send_piece,   xf_shm_end_message,
	xf_shm_extract,       xf_shm_num_pieces,   xf_shm_piece_len,
        xf_shm_recv_piece,    xf_shm_callback,
	SHMMAXPIECELEN
    },
    {
	FM_XFACE_CLASS_MYR,
        xf_myr_new,           xf_myr_delete,
	xf_myr_constructor,   xf_myr_destructor,
	xf_myr_begin_message, xf_myr_send_piece,   xf_myr_end_message,
	xf_myr_extract,       xf_myr_num_pieces,   xf_myr_piece_len,
        xf_myr_recv_piece,    xf_myr_callback,
	GMMAXPIECELEN
    },
    {
	FM_XFACE_CLASS_TCP,
        xf_tcp_new,           xf_tcp_delete,
	xf_tcp_constructor,   xf_tcp_destructor,
	xf_tcp_begin_message, xf_tcp_send_piece,   xf_tcp_end_message,
	xf_tcp_extract,       xf_tcp_num_pieces,   xf_tcp_piece_len,
        xf_tcp_recv_piece,    xf_tcp_callback,
	TCPMAXPIECELEN
    },
    {
	FM_XFACE_CLASS_MPI,
        xf_mpi_new,           xf_mpi_delete,
	xf_mpi_constructor,   xf_mpi_destructor,
	xf_mpi_begin_message, xf_mpi_send_piece,   xf_mpi_end_message,
	xf_mpi_extract,       xf_mpi_num_pieces,   xf_mpi_piece_len,
        xf_mpi_recv_piece,    xf_mpi_callback,
	FMMPIMAXPIECELEN
    }
};

/*---------------------------------------------------------------------------*/
typedef struct
{
    int numnodes;                 /*Number of nodes in this group*/
    FMNodeName canname[FMMAXPE];  /*Canonical hostnames of nodes in this group*/
    int fmnodeid[FMMAXPE];        /*FM IDs of nodes in this group*/
} NodeGroup;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int ngroups;           /*Number of groups*/
    NodeGroup group[10/*XXX*//*FMMAXPE*/];/*Information for each group*/
} NodeGroupMap;

/*---------------------------------------------------------------------------*/
typedef struct
{
    /*If xf_cltag!=NUL, then i->j has direct link, using xf_cltag interface*/
    /*If xf_cltag==NUL, then i->j is reached via i->next_hop*/

    XFaceClassTag xf_cltag;/*Type of subnet to which this link xface belongs*/
    int subnet_id;         /*ID of subnet to which this link xface belongs*/
    int next_hop;          /*FM node ID of next processor to route via*/
    double cost;           /*Link cost (say, latency); smaller is better*/
} AdjEntry;

/*---------------------------------------------------------------------------*/
#if MPI_AVAILABLE
#define NOADJMATRIX 1
#endif

/*---------------------------------------------------------------------------*/
typedef struct
{
#if NOADJMATRIX
#else /*NOADJMATRIX*/
    AdjEntry matrix[FMMAXPE][FMMAXPE]; /*Info on reaching from FM nodes i to j*/
#endif /*NOADJMATRIX*/
} AdjMatrix;

/*---------------------------------------------------------------------------*/
typedef struct
{
    AdjMatrix adj; /*How to reach from one FM node to another*/
    int tot_subnets; /*Total number of ALL subnets in the network*/
    NodeGroupMap grps[FM_SUBNET_NUM]; /*Grouping info for ALL subnets*/
    NodeGroup *subnets[FMMAXPE]; /*Pointers to group info indexed by subnet_id*/
    XFaceNextHopTable nhop_table;     /*Next hop info from this PE to others*/
} Network;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int dbg;

    #define MAX_XFACES 10
    int nxfaces; /*Actual number of interfaces instantiated at this processor*/
    XFace xfaces[MAX_XFACES]; /*Info for each instantiated xface at this proc.*/

    Network net; /*Info about entire network*/

    FMNodeName FM_nodenames[FMMAXPE]; /*Hostnames of all nodes*/

    int next_handler; /*Which is the next free slot in FM handler table*/

    struct
    {
        struct
        {
            FM_stream stream;  /*Currently active stream*/
	    XFace *xf;         /*Currently active interface*/
        } in, out;             /*Incoming, Outgoing*/
    } curr;
} MixFMData;

/*---------------------------------------------------------------------------*/
ULONG FM_nodeid;
ULONG FM_numnodes;
FM_handler **FM_handler_table;

/*---------------------------------------------------------------------------*/
static MixFMData *mixfm = 0;

/*---------------------------------------------------------------------------*/
static int use_shm =
                      #if SHM_AVAILABLE
		          1
                      #else
                          0
		      #endif
		          ;
static int use_gm =
                      #if GM_AVAILABLE
                          1
                      #else
		          0
		      #endif
		          ;
static int use_tcp =
                      #if TCP_AVAILABLE
                          1
                      #else
		          0
		      #endif
		          ;
static int use_mpi =
                      #if MPI_AVAILABLE
                          1
                      #else
		          0
		      #endif
		          ;

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
int absorb_or_route( int handler, void *in_stream,
    int src_pe, int src_id, int dest_id)
{
    MYASSERT( src_id != FM_nodeid, ("Loop detected") );
    MYASSERT( 0 <= src_id && src_id < FM_numnodes, ("%d",src_id) );
    MYASSERT( 0 <= dest_id && dest_id < FM_numnodes, ("%d",dest_id) );

    mixfm->curr.in.stream = in_stream;
    if( dest_id == FM_nodeid )
    {
        /*Destined to self; call the local handler*/
	FM_handler *hf = FM_handler_table[handler];
	MYASSERT( hf, ("Handler function %d must exist",handler) );
	hf( in_stream, src_id );
    }
    else
    {
        /*Destined to some other node; route towards it*/
        XFaceNextHopEntry *nhe =&mixfm->net.nhop_table.nhop_entry[dest_id];
        int nh = nhe->next_hop;
	mixfm->curr.out.xf = nhe->xf;

#if !NODEBUG
if(mixfm->dbg>=3){printf("Forwarding msg from %d via %d to %d\n",src_id,nh,dest_id);}
#endif

	/*Copy pieces from incoming interface to outgoing interface*/
	{
	  XFaceStream out_stream = 0;
	  int p = 0, np = 0, dest_pe = 0, maxlen = 0;
	  XFace *in_xf = mixfm->curr.in.xf, *out_xf = mixfm->curr.out.xf;
	  MYASSERT( in_xf && out_xf, ("%p %p",in_xf,out_xf) );

	  maxlen = out_xf->xf_class->xf_maxpiecelen;
	  dest_pe = out_xf->xf_map[nh];
	  np = in_xf->xf_class->xf_num_pieces( in_xf, in_stream );
	  MYASSERT( 0 <= np && np <= FMMAXPIECES, ("%d %d",np,FMMAXPIECES) );

	  out_xf->xf_class->xf_begin_message( out_xf, &out_stream,
              dest_pe, maxlen, handler, src_id, dest_id );
	  mixfm->curr.out.stream = out_stream;

	  for( p = 0; p < np; p++ )
	  {
	      typedef char ReceiveBuf[FMMAXPIECELEN];
	      ReceiveBuf buf[FMMAXPIECES];
	      int piecelen = in_xf->xf_class->xf_piece_len(in_xf,
	                        in_stream, p);
	      MYASSERT( 0 < piecelen && piecelen <= FMMAXPIECELEN,
	              ("%d %d",piecelen,FMMAXPIECELEN) );
	      in_xf->xf_class->xf_recv_piece(in_xf, in_stream,
	                                      buf[p], piecelen);
	      out_xf->xf_class->xf_send_piece(out_xf, out_stream,
	                                      buf[p], piecelen);
	  }

	  out_xf->xf_class->xf_end_message( out_xf, out_stream );
	}

	mixfm->curr.out.xf = 0; mixfm->curr.out.stream = 0;
    }

    return FM_CONTINUE;
}

/*---------------------------------------------------------------------------*/
static void print_xface( XFace *xf )
{
    int i = 0;

    printf("Xface name=\"%s\" subnet_id=%d sub_id=%d sub_n=%d\n",
           xf->xf_name, xf->xf_subnet_id, xf->xf_sub_id, xf->xf_sub_n);

    printf("\tinv_map={");
    for( i = 0; i < xf->xf_sub_n; i++ )
    {
        printf("%s%d",(i>0?", ":""),xf->xf_inv_map[i]);
    }
    printf("}\n");

    printf("\tcanname={");
    for( i = 0; i < xf->xf_sub_n; i++ )
    {
        printf("%s\"%s\"",(i>0?", ":""),xf->xf_canname[i]);
    }
    printf("}\n");
}

/*---------------------------------------------------------------------------*/
static void config( void )
{
    int i = 0, val = 0;
    char *estr = 0;

    estr = getenv("FM_NUMNODES");
    MYASSERT( estr, ("FM: Expected env var FM_NUMNODES") );
    FM_numnodes = atoi( estr );
    MYASSERT( 0 < FM_numnodes && FM_numnodes <= FMMAXPE,
	    ("FM: Bad FM_NPROCS=%lu; must be in [1..%d]", FM_numnodes,FMMAXPE));

    estr = getenv("FM_NODEID");
    MYASSERT( estr, ("FM: Expected env var FM_NODEID") );
    val = atoi( estr );
    MYASSERT( 0 <= val && val < FM_numnodes,
	    ("FM: Bad FM_NODEID=%d; must be in [0..%lu]",
		      val, FM_numnodes-1) );
    FM_nodeid = val;

    for( i = 0; i < FM_numnodes; i++ )
    {
	char temp[1000], *fmn = mixfm->FM_nodenames[i];
#if NOADJMATRIX
	if(i>0)
	{
	strncpy( fmn, estr, FMMAXHOSTNAMELEN ); fmn[FMMAXHOSTNAMELEN-1] = 0;
        continue;
	}
#endif /*NOADJMATRIX*/
	sprintf( temp, "FM_NODENAME_%d", i );
        estr = getenv(temp);
        MYASSERT( estr, ("FM: Expected env var %s",temp) );
	strncpy( fmn, estr, FMMAXHOSTNAMELEN ); fmn[FMMAXHOSTNAMELEN-1] = 0;
#if !NODEBUG
if(mixfm->dbg>=6){printf("MIXFM: Node[%d]=\"%s\"\n", i, fmn);fflush(stdout);}
#endif
    }

#if !NODEBUG
if(mixfm->dbg>=1){if(FM_nodeid==0){printf("FM_nodeid=%lu, FM_numnodes=%lu\n",FM_nodeid,FM_numnodes);fflush(stdout);}}
#endif
}

/*---------------------------------------------------------------------------*/
static void group_mpi_nodes( void )
{
    int i = 0;
    NodeGroupMap *mpigrps = &mixfm->net.grps[FM_SUBNET_MPI];
    NodeGroup *newgrp = &mpigrps->group[0];
    mpigrps->ngroups = 1;
    newgrp->numnodes = 0;

    for( i = 0; i < FM_numnodes; i++ )
    {
	int memberid = newgrp->numnodes++;

	MYASSERT( 0 <= memberid && memberid <= FMMPIMAXPE-1, ("%d",memberid) );

	newgrp->fmnodeid[memberid] = i;
	strcpy( newgrp->canname[memberid], mixfm->FM_nodenames[i] );
    }
}

/*---------------------------------------------------------------------------*/
static void group_shm_nodes( void )
{
    int i = 0, g = 0;
    NodeGroupMap *shmgrps = &mixfm->net.grps[FM_SUBNET_SHM];

    shmgrps->ngroups = 0;
    for( i = 0; i < FM_numnodes; i++ )
    {
        int groupid = -1, memberid = -1;
	const char *fmnm = mixfm->FM_nodenames[i];
	NodeGroup *addtogrp = 0;

	for( g = 0; g < shmgrps->ngroups; g++ )
	{
	    NodeGroup *grp = &shmgrps->group[g];
	    if( !strcmp( grp->canname[0], fmnm ) )
	    {
	        if( use_shm )
		{
		    addtogrp = grp;
		    groupid = g;
		    break;
		}
	    }
	}

	if( !addtogrp )
	{
	    NodeGroup *newgrp = &shmgrps->group[shmgrps->ngroups];
	    char *nm = newgrp->canname[0];
	    strcpy( nm, fmnm );
	    newgrp->numnodes = 0;
	    addtogrp = newgrp;
	    groupid = shmgrps->ngroups++;
#if !NODEBUG
if(mixfm->dbg>=6){printf("Detected new group[%d] \"%s\"\n",groupid,nm);}
#endif
	}

	MYASSERT( addtogrp, ("!") );
	MYASSERT( 0 <= addtogrp->numnodes && addtogrp->numnodes <= SHMMAXPE-1,
	        ("%d",addtogrp->numnodes) );

	memberid = addtogrp->numnodes++;
	addtogrp->fmnodeid[memberid] = i;
	if(memberid!=0)
	    strcpy( addtogrp->canname[memberid], addtogrp->canname[0] );
#if !NODEBUG
if(mixfm->dbg>=6){printf("Node %d added to group %d\n",i,groupid);}
#endif
    }

    for( g = 0; g < shmgrps->ngroups; )
    {
	NodeGroup *grp = &shmgrps->group[g];
#if !NODEBUG
if(mixfm->dbg>=6){printf("Group %d has %d nodes\n",g,grp->numnodes);}
#endif
        if( grp->numnodes > 1 )
	{
	    g++;
	}
	else
	{
	    int g2 = 0;
#if !NODEBUG
if(mixfm->dbg>=6){printf("Removing group %d\n",g);}
#endif
	    for( g2 = g+1; g2 < shmgrps->ngroups; g2++ )
	    {
	        NodeGroup *grp_other = &shmgrps->group[g2];
	        *grp = *grp_other;
	    }
	    --shmgrps->ngroups;
	}
    }
#if !NODEBUG
if(mixfm->dbg>=6){printf("Number of non-singleton shm groups %d\n",shmgrps->ngroups);}
#endif
}

/*---------------------------------------------------------------------------*/
static void read_network_file( void )
{
    FILE *fp = 0;
    char *net_filename = getenv("FM_NETFILE");

    if( !net_filename ) net_filename = "net.txt";
if(0)/*XXX*/ fp = fopen( net_filename, "r" );

    if( !fp )
    {
	int i = 0;
        SubnetType nettype = SUBNET_STR_TO_TYPE("LAN");
        NodeGroupMap *grps = &mixfm->net.grps[nettype];
	NodeGroup *grp = &grps->group[grps->ngroups++];

#if !NODEBUG
if(mixfm->dbg>=2){printf("Note: No network description file \"%s\".\nAssuming shared memory & LAN (TCP) connectivity among all nodes...\n",net_filename);}
#endif

	/*Add LAN (TCP) connectivity among leader nodes of shm groups*/
	grp->numnodes = 0;
	for( i = 0; i < FM_numnodes; i++ )
	{
	    int j = 0;
	    /*See if we already added the group leader of node i's shm group*/
	    for( j = 0; j < i; j++ )
	    {
		if( use_shm )
		{
	            if( !strcmp( mixfm->FM_nodenames[j],
		                 mixfm->FM_nodenames[i] ) )
		        break;
		}
	    }

	    if( j < i )
	    {
	        /*Already added; skip adding this node to the LAN group*/
	    }
	    else
	    {
	        /*Add this first/leader node of an shm group to the LAN group*/
	        int k = grp->numnodes++;
	        MYASSERT( k < FMMAXPE, ("Too many nodes %d",k) );
	        strcpy( grp->canname[k], mixfm->FM_nodenames[i] );
	        grp->fmnodeid[k] = i;
	    }
	}
    }
    else
    {
	char line[10000];
	int linenum = 0;
	int offset = 0;
#if !NODEBUG
if(mixfm->dbg>=1){printf("Using network description file \"%s\"...\n",net_filename);}
#endif
	while( fgets( line+offset, sizeof(line)-offset, fp ) )
	{
	    char nettype_str[1000];
	    int nbytes = 0, nscanned = 0;
	    SubnetType nettype;

	    ++linenum;
	    if( line[0] == 0 ) { continue; } /*Ignore empty line*/
	    if( line[strlen(line)-1] == '\n' ) /*Chop off trailing newline*/
	        { line[strlen(line)-1] = 0; }
	    if( line[0] == 0 ) { continue; } /*Ignore empty line*/
	    if( line[strlen(line)-1] == '\\' ) /*Line break & continuation*/
	        { offset = strlen(line)-1; continue; }
	    offset = 0;
	    if( line[0] == 0 ) { continue; } /*Ignore empty line*/
	    { char *p; if( (p=strchr(line,'#')) ) *p = 0; } /*Strip comments*/
	    { char *p=line+strlen(line);
	      while(*p && isspace(*p))*p--=0; } /*Trim any trailing whitespace*/
	    if( line[0] == 0 ) { continue; } /*Ignore empty line*/

	    nscanned = sscanf(line, "%s%n", nettype_str, &nbytes);
	    MYASSERT( nscanned >= 1, ("In file \"%s\" line %d: "
		    "valid subnet type required", net_filename, linenum) );
	    nettype = SUBNET_STR_TO_TYPE(nettype_str);

#if !NODEBUG
if(mixfm->dbg>=4){printf("Read subnet type \"%s\"\n",nettype_str);}
#endif
	    MYASSERT( nettype != FM_SUBNET_BAD,
	            ("Unknown subnet type \"%s\" in file \"%s\" line %d",
		    nettype_str, net_filename, linenum) );
	    MYASSERT( nettype != FM_SUBNET_SHM,
	            ("In file \"%s\" line %d: SHM subnet is implicit and "
		     "hence must not be specified", net_filename, linenum) );

	    /*Now parse off hostnames: "hostname[=othername] ..."*/
	    /*The "othername" is useful for specifying alternative, */
	    /*subnet-specific name for each node*/
	    {
		int i = 0;
                NodeGroupMap *grps = &mixfm->net.grps[nettype];
		NodeGroup *grp = &grps->group[grps->ngroups++];
	        char *hnamestr = line + nbytes;

		grp->numnodes = 0;

		while( hnamestr[0] )
		{
		    char *p = 0;
		    FMNodeName hname, othername;
		    hname[0] = 0;
		    sscanf(hnamestr, "%s%n", hname, &nbytes);
		    hnamestr += nbytes;
		    if( !hname[0] ) break;
		    p = strchr( hname, '=' );
		    if( p ) *p = 0;
		    strcpy( othername, p ? p+1 : hname );
#if !NODEBUG
if(mixfm->dbg>=4){printf("Read subnet member \"%s\"=\"%s\"\n",hname,othername);}
#endif

		    /*Add this node to the subnet group, if it is included*/
		    {
		        for( i = 0; i < FM_numnodes; i++ )
			{
			    if( !strcmp( mixfm->FM_nodenames[i], hname ) )
			        break;
			}

			if( i < FM_numnodes )
			{
			    int j = grp->numnodes++;
			    MYASSERT( j < FMMAXPE,
			      ("Too many nodes %d in subnet %s",j,nettype_str));
			    strcpy( grp->canname[j], othername );
			    grp->fmnodeid[j] = i;
			}
		    }
		}

		if( grp->numnodes > 1 )
		{
#if !NODEBUG
if(mixfm->dbg>=3){printf("Note: %d nodes used from subnet %s on line %d.\n",grp->numnodes,nettype_str,linenum);}
#endif
		}
		else
		{
		    --grps->ngroups;
#if !NODEBUG
if(mixfm->dbg>=1){printf("Note: No links belonging to subnet %s on line %d are\nused. Here is the corresponding subnet specification:\n\t\"%s\"\n",nettype_str,linenum,line);}
#endif
		}
	    }
	}
        fclose(fp);
    }
}

/*---------------------------------------------------------------------------*/
static void group_nodes( void )
{
    #if MPI_AVAILABLE
    group_mpi_nodes();
    #else /*MPI_AVAILABLE*/
    group_shm_nodes();
    #endif /*MPI_AVAILABLE*/
    read_network_file();
}

/*---------------------------------------------------------------------------*/
static void print_adj_matrix( void )
{
    AdjMatrix *adj = &mixfm->net.adj;
    int i = 0, j = 0;

	printf("-----------------------------------\n");
	printf("%10.10s%3.3s", "", "");
        for( i = 0; i < FM_numnodes; i++ )
        {
            printf("%5.5s%-4d%5.5s", "", i, "");
        }
	printf("\n");
        for( i = 0; i < FM_numnodes; i++ )
        {
	    printf("%10.10s%3d", mixfm->FM_nodenames[i], i);
            for( j = 0; j < FM_numnodes; j++ )
            {
#if NOADJMATRIX
	        AdjEntry dummy, *aij = &dummy;
	        aij->xf_cltag = FM_XFACE_CLASS_MPI;
	        aij->subnet_id = 0;
	        aij->next_hop = j;
#else /*NOADJMATRIX*/
	        AdjEntry *aij = &adj->matrix[i][j];
#endif /*NOADJMATRIX*/
		if( aij->xf_cltag == FM_XFACE_CLASS_NUL )
		{
		    printf("%14.14s","");
		}
		else
		{
	            printf("%3.3s%3.3s%-3d->%-3d",
		           "", XFACE_CLASS_STR(aij->xf_cltag),
		           aij->subnet_id, aij->next_hop);
		}
            }
	    printf("\n");
        }
	printf("-----------------------------------\n");
}

/*---------------------------------------------------------------------------*/
static void make_adj_matrix( void )
{
    int i = 0, j = 0, k = 0, x = 0, y = 0, g = 0, s = 0;
    AdjMatrix *adj = &mixfm->net.adj;

#if NOADJMATRIX
    /*Do Nothing*/
#else /*NOADJMATRIX*/
    /*Initialize*/
    for( i = 0; i < FM_numnodes; i++ )
    {
	for( j = 0; j < FM_numnodes; j++ )
	{
	    AdjEntry dummy, *ae = &dummy;

#if !NODEBUG
if(mixfm->dbg>=3){if(i==0&&j==0){printf("%d: Omitting ADJMATRIX\n",FM_nodeid);fflush(stdout);}}
#endif
            ae = &adj->matrix[i][j];
	    if(i==0&&j==0){printf("Using ADJMATRIX\n");fflush(stdout);}
	    ae->xf_cltag = FM_XFACE_CLASS_NUL;
	    ae->subnet_id = -1;
	    ae->next_hop = -1;
	    ae->cost = LINK_COST_INF;
	}
    }
#endif /*NOADJMATRIX*/

    /*Set up subnets info*/
    for( s = 0; s < FM_SUBNET_NUM; s++ )
    {
      NodeGroupMap *grps = &mixfm->net.grps[s];
      for( g = 0; g < grps->ngroups; g++ )
      {
	NodeGroup *grp = &grps->group[g];
	if( grp->numnodes > 1 )
	{
	  int subnet_id = mixfm->net.tot_subnets++;
	  mixfm->net.subnets[subnet_id] = grp;
#if !NODEBUG
if(mixfm->dbg>=5){printf("Subnet type %d group %d ID %d\n",s,g,subnet_id);}
#endif

#if NOADJMATRIX
          /*Do Nothing*/
#else /*NOADJMATRIX*/
	  for( x = 0; x < grp->numnodes; x++ )
	  {
	    for( y = 0; y < grp->numnodes; y++ )
	    {
	      if( x == y )
	      {
	      }
	      else
	      {
		  i = grp->fmnodeid[x];
		  j = grp->fmnodeid[y];
		  MYASSERT( 0 <= i < FM_numnodes, ("%d",i) );
		  MYASSERT( 0 <= j < FM_numnodes, ("%d",j) );
		  {
		      AdjEntry *ae = &adj->matrix[i][j];
		      if( SUBNET_LINK_COST(s) < ae->cost )
		      {
		          ae->xf_cltag = SUBNET_TYPE_TO_XFACE_CLASS(s);
		          ae->subnet_id = subnet_id;
		          ae->next_hop = j;
		          ae->cost = SUBNET_LINK_COST(s);
		      }
		  }
	      }
	    }
	  }
#endif /*NOADJMATRIX*/
	}
      }
    }

#if !NODEBUG
if(mixfm->dbg>=6){printf("Direct links:\n");print_adj_matrix();}
#endif

#if NOADJMATRIX
    /*Do nothing*/
#else /*NOADJMATRIX*/
    /*Compute nexthops of shortest-paths, via transitive closure*/
    for( k = 0; k < FM_numnodes; k++ )
    {
        for( i = 0; i < FM_numnodes; i++ )
	{
            for( j = 0; j < FM_numnodes; j++ )
	    {
	        AdjEntry *aik = &adj->matrix[i][k];
		AdjEntry *akj = &adj->matrix[k][j];
		AdjEntry *aij = &adj->matrix[i][j];

		if( i == j ) continue; /*Skip loopbacks*/

		if(
		    /*No direct route i->j, but can go i->k->j*/
		    ( aij->xf_cltag == FM_XFACE_CLASS_NUL &&
		      aik->xf_cltag != FM_XFACE_CLASS_NUL &&
		      akj->xf_cltag != FM_XFACE_CLASS_NUL )
		    ||
		    /*Route i->k->j is cheaper than current route i->j*/
		    ( aij->xf_cltag != FM_XFACE_CLASS_NUL &&
		      aik->xf_cltag != FM_XFACE_CLASS_NUL &&
		      akj->xf_cltag != FM_XFACE_CLASS_NUL &&
		      aik->cost+akj->cost < aij->cost )
		  )
		  /*Route from i to j using the i->k direct link*/
		{
		    aij->xf_cltag = aik->xf_cltag;
		    aij->subnet_id = aik->subnet_id;
		    aij->next_hop = aik->next_hop;
		    aij->cost = aik->cost + akj->cost;
		}
		else
		{
		    /*Do nothing*/
		}
	    }
	}
    }
#endif /*NOADJMATRIX*/

#if !NODEBUG
if(mixfm->dbg>=6){printf("Next hops transitive closure:\n");print_adj_matrix();}
#endif

#if NOADJMATRIX
    /*Do nothing*/
#else /*NOADJMATRIX*/
    for( i = 0; i < FM_numnodes; i++ )
    {
        for( j = 0; j < FM_numnodes; j++ )
	{
	    if( i != j )
	    {
#if 0
	        AdjEntry dummy, *aij = &dummy;
	        aij->xf_cltag = FM_XFACE_CLASS_MPI;
	        aij->subnet_id = 0;
	        aij->next_hop = j;
#endif
	        AdjEntry *aij = &adj->matrix[i][j];
	        MYASSERT( aij->xf_cltag != FM_XFACE_CLASS_NUL,
		        ("\n\nNode #%d %s unreachable from node #%d %s.\n"
			 "Check your network information file.",
			 j, mixfm->FM_nodenames[j], i, mixfm->FM_nodenames[i]));
	    }
	}
    }
#endif /*NOADJMATRIX*/
}

/*---------------------------------------------------------------------------*/
static void instantiate_interfaces( void )
{
    int subnet_id = 0;
    AdjMatrix *adj = &mixfm->net.adj;

    mixfm->nxfaces = 0;
    for( subnet_id = 0; subnet_id < mixfm->net.tot_subnets; subnet_id++ )
    {
	char incl[FMMAXPE];
        XFaceClassTag cltag = FM_XFACE_CLASS_BAD;
	int i = 0, j = 0, nincl = 0;
#if NOADJMATRIX
	for( i = 0; i < FM_numnodes; i++ ) incl[i] = 1;
	nincl = FM_numnodes;
	cltag = FM_XFACE_CLASS_MPI;
#else /*NOADJMATRIX*/
	for( i = 0; i < FM_numnodes; i++ ) incl[i] = 0;
	for( i = 0; i < FM_numnodes; i++ )
	{
	    for( j = 0; j < FM_numnodes; j++ )
	    {
	        AdjEntry *aij = &adj->matrix[i][j];
		if( i != j && aij->xf_cltag != FM_XFACE_CLASS_NUL &&
		    aij->subnet_id==subnet_id && aij->next_hop==j )
		{
		    if( !incl[i] ) { incl[i] = 1; nincl++; }
		    if( !incl[j] ) { incl[j] = 1; nincl++; }
		    if( cltag == FM_XFACE_CLASS_BAD ) cltag = aij->xf_cltag;
		    MYASSERT( cltag==aij->xf_cltag,("%d %d",cltag,aij->xf_cltag));
		}
	    }
	}
#endif /*NOADJMATRIX*/

	MYASSERT( use_shm || !(cltag == FM_XFACE_CLASS_SHM && incl[FM_nodeid]),
	        ("Can't use SHM on this federate.\n") );
	MYASSERT( use_gm || !(cltag == FM_XFACE_CLASS_MYR && incl[FM_nodeid]),
	        ("Can't use GM/Myrinet on this federate.\n") );
	MYASSERT( use_tcp || !(cltag == FM_XFACE_CLASS_TCP && incl[FM_nodeid]),
	        ("Can't use TCP on this federate.\n") );
	MYASSERT( use_mpi || !(cltag == FM_XFACE_CLASS_MPI && incl[FM_nodeid]),
	        ("Can't use MPI on this federate.\n") );

	if( incl[FM_nodeid] ) /*I am in this subnet*/
	{
	    /*Need to instantiate an interface for this subnet*/

	    char *snm = (char *)malloc( 20*sizeof(char) );
	    int numnodes = 0, myid = -1;
	    NodeGroup *sgrp = mixfm->net.subnets[subnet_id];
	    XFace *xf = &mixfm->xfaces[mixfm->nxfaces++];

	    xf->xf_class = &xf_classes[cltag];
	    sprintf(snm,"%s%d",XFACE_CLASS_STR(cltag),subnet_id);
	    xf->xf_name = snm;
	    xf->xf_subnet_id = subnet_id;
	    xf->xf_constructed = 0;

	    /*For this subnet, find #nodes, map, inv_map, cannames & ID*/
	    for( i = 0; i < FM_numnodes; i++ )
	    {
		if( !incl[i] )
		{
		    xf->xf_map[i] = -1;
		}
		else
		{
		    int id = numnodes++, m = 0;
		    if( i == FM_nodeid ) myid = id;
		    xf->xf_map[i] = id;
		    xf->xf_inv_map[id] = i;

#if NOADJMATRIX
                    m = i; /*Self*/
#else /*NOADJMATRIX*/
		    for( m = 0; m < sgrp->numnodes; m++ )
		    {
		        if( sgrp->fmnodeid[m] == i ) break;
		    }
#endif /*NOADJMATRIX*/
		    MYASSERT( m < sgrp->numnodes, ("%d %d",subnet_id,i) );
		    strcpy( xf->xf_canname[id], sgrp->canname[m] );
		}
	    }

	    MYASSERT( nincl == numnodes, ("%d %d",nincl,numnodes) );
	    MYASSERT( 2 <= numnodes && numnodes <= FM_numnodes,
	            ("%d %ld",numnodes,FM_numnodes) );
	    MYASSERT( 0 <= myid && myid < numnodes, ("%d %d",myid,numnodes) );

	    xf->xf_sub_id = myid;
	    xf->xf_sub_n = numnodes;

	    xf->xf_class->xf_new( xf );

#if !NODEBUG
if(mixfm->dbg>=4){print_xface( xf );}
#endif
        }
    }
}

/*---------------------------------------------------------------------------*/
static void make_nexthop_table( void )
{
    int i = 0, j = 0;
    AdjMatrix *adj = &mixfm->net.adj;

    for( i = FM_nodeid, j = 0; j < FM_numnodes; j++ )
    {
#if NOADJMATRIX
	AdjEntry dummy, *aij = &dummy;
	aij->xf_cltag = FM_XFACE_CLASS_MPI;
	aij->subnet_id = 0;
	aij->next_hop = j;
#else /*NOADJMATRIX*/
	AdjEntry *aij = &adj->matrix[i][j];
#endif /*NOADJMATRIX*/
        if( i == j )
	{
	}
	else
	{
	    int x = 0;
	    XFace *found_xf = 0;
	    for( x = 0; x < mixfm->nxfaces; x++ )
	    {
	        XFace *xf = &mixfm->xfaces[x];
	        if( xf->xf_subnet_id == aij->subnet_id )
		{
		    found_xf = xf;
		    break;
		}
	    }
	    MYASSERT( found_xf, ("Interface not found for dest FM node %d", j) );
	    {
	        XFaceNextHopEntry *nhe =&mixfm->net.nhop_table.nhop_entry[j];
	        nhe->dest_id = j;
	        nhe->next_hop = aij->next_hop;
	        nhe->xf = found_xf;
	    }
	}
    }
}

/*---------------------------------------------------------------------------*/
static void initialize_interfaces( void )
{
    int s = 0, x = 0;

    /*Initialize the interfaces in ascending order of subnet type*/
    for( s = 0; s < FM_SUBNET_NUM; s++ )
    {
        for( x = 0; x < mixfm->nxfaces; x++ )
	{
	    XFace *xf = &mixfm->xfaces[x];
	    if( !xf->xf_constructed &&
	        xf->xf_class->xf_cltag == SUBNET_TYPE_TO_XFACE_CLASS(s) )
	    {
		int myid = xf->xf_sub_id, numgrpnodes = xf->xf_sub_n;
		FMNodeName *cannames = xf->xf_canname;
	        xf->xf_class->xf_constructor( xf, myid, numgrpnodes, cannames );
		xf->xf_constructed = 1;
	    }
	}
    }
}

/*---------------------------------------------------------------------------*/
static void finalize_interfaces( void )
{
    int s = 0, x = 0;

    /*Finalize the interfaces in descending order of subnet type*/
    for( s = FM_SUBNET_NUM-1; s >= 0; --s )
    {
        for( x = mixfm->nxfaces-1; x >= 0; --x )
	{
	    XFace *xf = &mixfm->xfaces[x];
	    if( xf->xf_class->xf_cltag == SUBNET_TYPE_TO_XFACE_CLASS(s) )
	    {
	        xf->xf_class->xf_destructor( xf );
	    }
	}
    }
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
void FM_pre_init( int *pac, char ***av ) /*Appl MUST call PreInit if using MPI*/
{
    #if MPI_AVAILABLE
	FMMPI_pre_initialize( pac, av );
	/*Create NODEINFO env var*/
	{
	    char *hname = "H" /*"localhost"*/;
	    char *nstr = (char *)malloc(FMMAXHOSTNAMELEN+200);
	    sprintf(nstr,"NODEINFO=%d:%d@%s:%d",
		    FMMPI_numnodes,FMMPI_numnodes,hname,FMMPI_nodeid);
	    putenv(nstr);
	}
    #endif /*MPI_AVAILABLE*/
}

/*---------------------------------------------------------------------------*/
void FM_initialize( const char *stdout_fprefix )
{
    FM_handler_table=(FM_handler**)calloc(FM_MAX_HANDLERS, sizeof(FM_handler*));
    MYASSERT( FM_handler_table, ("!") );
    mixfm = (MixFMData *)calloc( 1, sizeof( MixFMData ) );
    MYASSERT(mixfm,("Can't alloc %lu bytes for MixFMData",sizeof(MixFMData)));

    {char *estr = getenv("FM_DEBUG"); mixfm->dbg = estr ? atoi(estr) : 0;}

#if !NODEBUG
if(mixfm->dbg>=2){printf("FM MAX_PE=%d\n",MAX_PE);fflush(stdout);}
if(mixfm->dbg>=2){printf("FM_DEBUG=%d\n",mixfm->dbg);fflush(stdout);}
if(mixfm->dbg>=2){printf("FM_initialize() started.\n");fflush(stdout);}
if(mixfm->dbg>=2){printf("Sizeof(MixFMData)== %.2lf MB.\n",sizeof(MixFMData)/1e6);}
#endif

    config();
    group_nodes();
    make_adj_matrix();
    instantiate_interfaces();
    make_nexthop_table();
    initialize_interfaces();

    _printlscfg = (_printlscfg && (FM_nodeid < _printlsdbgmaxfmid));

    if((FM_nodeid < _printlsdbgmaxfmid) && stdout_fprefix)
    {
        int retcode = 0;
        char *stdout_fname = malloc(sizeof(stdout_fprefix)+20);
        sprintf(stdout_fname,"%s-%lu.log",stdout_fprefix,FM_nodeid);

#if !NODEBUG
if(mixfm->dbg>=0){if(FM_numnodes<5){printf("%lu: Redirecting stdout to \"%s\"\n",FM_nodeid,stdout_fname);}else if(FM_nodeid==0){printf("Redirecting stdout to \"%s-R\".log, R=[0..%lu]\n",stdout_fprefix,FM_numnodes-1);}fflush(stdout);}
#endif
        retcode = redirect_stdout(stdout_fname);
        LSCFGLD( "FM_DEBUG", (long)mixfm->dbg, "FM debug level" );
        LSCFGLD( "FM_NUMNODES", (long)FM_numnodes, "FM number of nodes" );
        LSCFGLD( "FM_NODEID", (long)FM_nodeid, "FM node ID" );
        LSCFGLD( "FM_MAXPE", (long)MAX_PE, "FM max #PE" );

#if !NODEBUG
if(mixfm->dbg>=0){if(retcode!=0){printf("%lu: Failed to redirect stdout to \"%s\"\n",FM_nodeid,stdout_fname);fflush(stdout);}}
#endif
        free(stdout_fname);
    }

    #if MPI_AVAILABLE
	FMMPI_post_initialize();
    #endif

#if !NODEBUG
if(mixfm->dbg>=2){printf("FM_initialize() done.\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void FM_finalize( void )
{
    finalize_interfaces();
}

/*---------------------------------------------------------------------------*/
FM_stream *FM_begin_message( ULONG to_fm_id, ULONG length, ULONG handler )
{
    MYASSERT( to_fm_id < FM_numnodes, ("%ld",to_fm_id) );
    MYASSERT( to_fm_id != FM_nodeid, ("Can't send to self %ld", to_fm_id) );

    {
      XFaceNextHopEntry *nhe =&mixfm->net.nhop_table.nhop_entry[to_fm_id];
      int nh = nhe->next_hop;
      XFace *xf = nhe->xf;

#if !NODEBUG
if(mixfm->dbg>=3){printf("%s from %ld via %d to %ld\n", (nh==to_fm_id?"Directly sending":"Indirectly forwarding"), FM_nodeid, nh, to_fm_id);}
#endif

      MYASSERT( nhe->dest_id == to_fm_id, ("%d %ld",nhe->dest_id,to_fm_id) );
      MYASSERT( 0 <= nh && nh < FM_numnodes, ("%d",nh) );
      MYASSERT( xf, ("Interface must exist") );
      {
        int m = xf->xf_map[nh];
	MYASSERT( 0 <= m && m < xf->xf_sub_n, ("%d %d",m,xf->xf_sub_n) );
	MYASSERT( xf->xf_inv_map[m] == nh,
	        ("Circular check %d %d",xf->xf_inv_map[m], nh) );
	MYASSERT( m != xf->xf_sub_id, ("Can't send to self") );
	mixfm->curr.out.xf = xf;
	xf->xf_class->xf_begin_message( xf, &mixfm->curr.out.stream,
	    m, length, handler, FM_nodeid, to_fm_id );
      }
    }

    return mixfm->curr.out.stream;
}

/*---------------------------------------------------------------------------*/
void FM_send_piece( FM_stream *sendstream, void *buffer, ULONG length )
{
    XFace *xf = mixfm->curr.out.xf;
    MYASSERT( sendstream == mixfm->curr.out.stream,
            ("%p %p",sendstream,mixfm->curr.out.stream) );
    MYASSERT( xf, ("An interface must be currently active") );
    xf->xf_class->xf_send_piece( xf, sendstream, buffer, length );
}

/*---------------------------------------------------------------------------*/
void FM_end_message( FM_stream *sendstream )
{
    XFace *xf = mixfm->curr.out.xf;
    MYASSERT( sendstream == mixfm->curr.out.stream,
            ("%p %p",sendstream,mixfm->curr.out.stream) );
    MYASSERT( xf, ("An interface must be currently active") );
    xf->xf_class->xf_end_message( xf, sendstream );

    mixfm->curr.out.xf = 0;
    mixfm->curr.out.stream = 0;
}

/*---------------------------------------------------------------------------*/
void FM_receive( void *buffer, FM_stream *receivestream, unsigned int length )
{
    XFace *xf = mixfm->curr.in.xf;
    MYASSERT( receivestream == mixfm->curr.in.stream,
            ("%p %p",receivestream,mixfm->curr.in.stream) );
    MYASSERT( xf, ("An interface must be currently active") );
    xf->xf_class->xf_recv_piece( xf, receivestream, buffer, length );
}

/*---------------------------------------------------------------------------*/
static int extracting = 0;
int FM_extract( unsigned int maxbytes )
{
    int x = 0, nextracted = 0;

    if( extracting++ > 0 ) return 0;

    for( x = 0; nextracted < maxbytes && x < mixfm->nxfaces; x++ )
    {
	XFace *xf = &mixfm->xfaces[x];

	mixfm->curr.in.xf = xf; mixfm->curr.in.stream = 0;
	mixfm->curr.out.xf = xf; mixfm->curr.out.stream = 0;

	nextracted += xf->xf_class->xf_extract( xf, maxbytes-nextracted );

	mixfm->curr.in.xf = 0; mixfm->curr.in.stream = 0;
	mixfm->curr.out.xf = xf; mixfm->curr.out.stream = 0;
    }

    extracting--;

    return nextracted;
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
ULONG FM_register_handler( ULONG id, FM_handler *handler )
{
    if( id == FM_ANY_ID ) id = mixfm->next_handler++;

    MYASSERT( id < FM_MAX_HANDLERS && !FM_handler_table[id], ("%lu",id));

    FM_handler_table[id] = handler;

#if !NODEBUG
if(mixfm->dbg>=2){printf("Registered handler %lu = %p\n",id, handler);fflush(stdout);}
#endif

    return id;
}

/*---------------------------------------------------------------------------*/
int FM_debug_level( int level )
{
    int old = mixfm->dbg;
    mixfm->dbg = level;
    return old;
}

/*---------------------------------------------------------------------------*/
void FM_set_parameter( int parameter, int arg )
{
    switch( parameter )
    {
        case 0: FM_debug_level( arg ); break;
	case 1: SHM_debug_level( arg ); break;
	case 2: GM_debug_level( arg ); break;
	case 3: TCP_debug_level( arg ); break;
	default: break;
    }
}

/*---------------------------------------------------------------------------*/
void FM_RemovePeer(unsigned senderID)
{
    MYASSERT( 0, ("Not implemented") );
}

/*---------------------------------------------------------------------------*/
int FM_IsTransportSupported( FM_Transport t ) {return t==FM_TRANSPORT_RELIABLE;}
FM_Transport FM_GetTransport( void ) {return FM_TRANSPORT_RELIABLE;}
int FM_SetTransport( FM_Transport t ) {return FM_IsTransportSupported(t);}

/*---------------------------------------------------------------------------*/
/* Based on FML Code from Dr. Richard Fujimoto.                              */
/*---------------------------------------------------------------------------*/
int FML_NextHandle = 0;		/* number of next available handler ID */
int FML_InitFM = 0;		/* flag indicating FM has been initialized */
int FML_MaxHandles = 1024;      /* max number of handles; FM default */
unsigned int fmh_Barrier;	/* handler ID for FML barrier */
long FML_NReceived=0;           /* how many processors replied to barrier req*/
int PFML_Barrier(FM_stream *, unsigned);

#include "rmbar.h"
static int use_rmb = 1;

/*---------------------------------------------------------------------------*/
void FML_FMInit(const char *stdout_fname)
{
  FM_initialize(stdout_fname);
  FML_InitFM = 1;

  if( use_rmb )
  {
      rmb_init();
  }
  else
  {
      /* Set up handler for barrier */
      int error = FML_RegisterHandler(&fmh_Barrier, PFML_Barrier);
      MYASSERT( !error, ("%d: FML_FMInit Failed\n", (int) FM_nodeid) );
  }
}

/*---------------------------------------------------------------------------*/
long FML_RegisterHandler(unsigned int *HandleID, FM_handler hfunction)
{
  if (FML_InitFM == 0)	/* check that FM has already been initialized */
  {
    fprintf(stderr, "FML_RegisterHandler: FM not initialized\n");
    fflush(stderr);
    return (1);
  }
  if (FML_NextHandle >= FML_MaxHandles) /* check if too many handlers */
  {
    fprintf(stderr, "FML_RegisterHandler: too many handles\n");
    fflush(stderr);
    return (2);
  }

  /* set up handler linkage and return ID */
  *HandleID = FML_NextHandle;
  FM_register_handler( FML_NextHandle, hfunction );
  FML_NextHandle++;
  return (0);
}

/*---------------------------------------------------------------------------*/
void FML_PrintHandlerTable (void)
{
  int i;

  printf ("*** FM Handler Table for Node %d ***\n", (int) FM_nodeid);
  for (i=0; i<FML_NextHandle; i++)
    printf ("%d: %p\n", i, FM_handler_table[i]);
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/
/* Simple barrier; does not guarantee all messages flushed out of system     */
/* Broadcast a msg, wait until a broadcast is recd from each other processor.*/
/* Use v=1 for final barrier, 0 otherwise                                    */
/*---------------------------------------------------------------------------*/
static void FML_BarrierInternal(int v) 
{
  int i;
 
  #if MPI_AVAILABLE
  if( 1 ) { MPI_Barrier( MPI_COMM_WORLD ); return; }
  #endif
  if( use_rmb ) { rmb_barrier(); return; }

  FML_NReceived++;

  /* broadcast message saying this processor has reached barrier */
  for(i=0; i<FM_numnodes; i++)
  {
    FM_stream *strm;
    if(i == FM_nodeid) continue;

    do
    {
    strm = FM_begin_message(i, sizeof(v), fmh_Barrier);
    if( !strm ) FM_extract(~0); /*break deadlock due to full queues*/
    } while( !strm );

    MYASSERT( strm, ("FML_Barrier failed") );
    FM_send_piece(strm, &v, sizeof(v));
    FM_end_message (strm);
  }

  /* wait until message received from all other processors */
  while(FML_NReceived < FM_numnodes)
  {
    FM_extract(~0);
  }

  /* reset counter; do subtract because new barrier may have already started */
  FML_NReceived -= FM_numnodes;
}

/*---------------------------------------------------------------------------*/
void FML_Barrier( void ) { FML_BarrierInternal(0); }

/*---------------------------------------------------------------------------*/
void FML_FinalBarrier(void) { FML_BarrierInternal(1); }

/*---------------------------------------------------------------------------*/
/* Handler for simple barrier                                                */
/*---------------------------------------------------------------------------*/
int PFML_Barrier(FM_stream *strm, unsigned senderID)
{
  int v=0;

  FM_receive(&v, strm, sizeof(v));
  if(v)
  {
      FM_RemovePeer(senderID);
  }
  FML_NReceived++;
  return(FM_CONTINUE);
}

/*---------------------------------------------------------------------------*/
/* NODEINFO utilities.                                                       */
/*---------------------------------------------------------------------------*/
#define MAX_HOSTNAMELEN 100
void FM_getenv_nodeinfo( NodeInfo *s )
{
    int i = 0;

    s->nproc = 1;
    s->my_index = 0;

    for( i = 0; i < MAX_PE; i++ )
    {
	s->node_names[i] = malloc( sizeof(char) * (MAX_HOSTNAMELEN+100) );
	sprintf( s->node_names[i], "%s", "" );
    }

    /* Get node information */
    {
    char *env_var_name = "NODEINFO";
    char *env_var_format = "npe:nodename,nodename,...,nodename:myindex";

    char *nstr = getenv( env_var_name );
    char *p = 0;

    if( !nstr )
    {
	nstr = "1:localhost:0";
        fprintf(stderr,
	        "\n\n*** Warning: NODEINFO environment variable not defined.\n"
	        "*** Assuming single process (i.e., NODEINFO=\"%s\").\n\n\n",
		nstr);
        fflush(stdout);
    }

    MYASSERT( nstr, ("Environment variable %s must be specified.\n"
		   "It should of the form \"%s\".\n",
		   env_var_name, env_var_format) );

    nstr = strdup( nstr ); /* Don't destroy the environment */
    MYASSERT( nstr, ("Insufficient memory duping NODEINFO") );
#if !NODEBUG
if(0){fprintf(stdout,"%s=\"%s\"\n",env_var_name,nstr);fflush(stdout);}
#endif
    p = strchr( nstr, ':' );
    MYASSERT( p, ("Ill-formed npe value for environment variable %s.\n"
		   "It should of the form \"%s\".\n",
		   env_var_name, env_var_format) );
    *p = 0;
    s->nproc = atoi( nstr );
    nstr = p+1;
    MYASSERT( 0 < s->nproc, ("npe value %d must be positive in env variable %s.\n"
		   "It should of the form \"%s\".\n",
		   s->nproc, env_var_name, env_var_format ) );
    MYASSERT( s->nproc <= MAX_PE, ("npe value %d too large (max=%d) in "
                   "env variable %s.\nIt should of the form \"%s\".\n",
		   s->nproc, MAX_PE, env_var_name, env_var_format) );

    for( i = 0; i < s->nproc; )
    {
	char delimit_char = ',', *comma = 0;
	int ninstances = 1;

	if( !( comma = strchr( nstr, delimit_char ) ) ) delimit_char = ':';

	comma = strchr( nstr, delimit_char );
	MYASSERT( comma, ("Ill-formed node %d value for env variable %s.\n"
		          "It should of the form \"%s\".\n",
		          i, env_var_name, env_var_format) );
	*comma = 0;
	if( isdigit(nstr[0]) ) /*Encoding as n@host, for n instances of host*/
	{
	    char *at = strchr( nstr, '@' );
	    MYASSERT( at && strlen(at+1) > 0,
	            ("Ill-formed node name %d value for env variable %s.\n"
		          "It should of the form \"%s\".\n",
		          i, env_var_name, env_var_format) );
	    *at = 0;
	    ninstances = atoi(nstr);
	    nstr = at+1;
	    MYASSERT( ninstances+i <= s->nproc,
		    ("Ill-formed node name %d value for env variable %s.\n"
		     "It should of the form \"%s\".\n",
		     i, env_var_name, env_var_format ) );
	}

	if( nstr[0] == '#' ) nstr++;

#if !NODEBUG
if(0){printf("Detected %d instance(s) of \"%s\" in %s\n",ninstances,nstr,env_var_name);fflush(stdout);}
#endif

	if( nstr[0] )
	{
            #define ADDNODENAME(_nn) do{                         \
	            int inst = 0;                                \
		    for( inst = 0; inst < ninstances; inst++ ) { \
	                MYASSERT(i<MAX_PE,("%s",_nn));           \
		        strcpy(s->node_names[i++],_nn);          \
		    }                                            \
		}while(0)

	    char oparen_char = '(', cparen_char = ')';
	    if( !strchr( nstr, oparen_char ) )
	    {
	        /*Normal hostname*/
		ADDNODENAME( nstr );
	    }
	    else /*Encoding as hn(i-j,...) representing hni..hnj*/
	    {
	        char *prefix = 0, *oparen=0, *cparen=0, *ids=0;
	        prefix = nstr;
	        oparen = strchr( prefix, oparen_char );
	        cparen = strchr( oparen, cparen_char );
		MYASSERT( cparen, ("Mismatched parens") );
		MYASSERT( cparen+1==comma, ("Trailing chars after parens") );
		*oparen = 0;
		*cparen = 0;
		ids = oparen+1;
		while(*ids && ids<cparen)
		{
		    int rangei=0, rangej=0, nbytes=0, k=0;
		    sscanf(ids,"%d%n",&rangei,&nbytes);
		    MYASSERT( nbytes > 0, ("\"%s\"",ids) );
		    MYASSERT( 0 <= rangei, ("%d",rangei) );
		    rangej = rangei;
		    ids += nbytes;
		    if(*ids && *ids == '-')
		    {
			ids++;
		        sscanf(ids,"%d%n",&rangej,&nbytes);
		        MYASSERT( 0 <= rangej, ("%d",rangej) );
		        ids += nbytes;
		    }
		    if(*ids == ';') ids++;
		    MYASSERT( i+(rangej-rangei+1) <= s->nproc,
			    ("%d %d %d %d",i,rangei,rangej,s->nproc) );
		    for( k = rangei; k <= rangej; k++ )
		    {
			char nodename[MAX_HOSTNAMELEN];
			sprintf( nodename, "%s%d", prefix, k );
			ADDNODENAME( nodename );
		    }
		}
		MYASSERT( !(*ids) && ids==cparen, ("%s",ids) );
	    }
	}
	nstr = comma+1;
    }
    if( (p = strchr( nstr, ':' )) ) *p = 0;
    s->my_index = atoi( nstr );
    nstr = p ? p+1 : nstr+strlen(nstr);

#if !NODEBUG
if(0){fprintf( stdout, "\tMyIndex=%d\n", s->my_index ); fflush(stdout);}
#endif

    MYASSERT( 0 <= s->my_index && s->my_index < s->nproc,
	    ("Ill-formed my_index value %d for environment variable %s.\n"
	      "It should of the form \"%s\".\n",
	      s->my_index, env_var_name, env_var_format) );
    } /* Node information */

    {
	char *nprocs_str = malloc( sizeof(char) * 100 );
	char *id_str = malloc( sizeof(char) * 100 );

	sprintf( nprocs_str, "FM_NUMNODES=%d", s->nproc ); putenv( nprocs_str );
	sprintf( id_str, "FM_NODEID=%d", s->my_index ); putenv( id_str );

	for( i = 0; i < s->nproc; i++ )
	{
	    const char *nm = s->node_names[i];
	    char *node_str = malloc( sizeof(char) * (strlen(nm)+100) ); 
            sprintf( node_str, "FM_NODENAME_%d=%s", i, nm ); putenv( node_str );
#if NOADJMATRIX
    break; /*node 0 is enough*/
#endif /*NOADJMATRIX*/
	}

	for( i = s->nproc; i < MAX_PE; i++ )
	{
	    free( s->node_names[i] );
	    s->node_names[i] = 0;
	}
    }
}

/*---------------------------------------------------------------------------*/
void FM_setenv_nodenames( const NodeInfo *s )
{
    int i = 0;
    char *nprocs_str = malloc( sizeof(char) * 20 );
    char *id_str = malloc( sizeof(char) * 20 );

    sprintf( nprocs_str, "FM_NUMNODES=%d", s->nproc ); putenv( nprocs_str );
    sprintf( id_str, "FM_NODEID=%d", s->my_index ); putenv( id_str );

    for( i = 0; i < s->nproc; i++ )
    {
        const char *nm = s->node_names[i];
        char *node_str = malloc( sizeof(char) * (strlen(nm)+20) ); 
        sprintf( node_str, "FM_NODENAME_%d=%s", i, nm ); putenv( node_str );
#if NOADJMATRIX
    break; /*node 0 is enough*/
#endif /*NOADJMATRIX*/
    }
}

/*---------------------------------------------------------------------------*/
void FM_freeenv_nodenames( NodeInfo *s )
{
    int i = 0;

    for( i = 0; i < s->nproc; i++ )
    {
        if( s->node_names[i] )
	{
	    free( s->node_names[i] );
	    s->node_names[i] = 0;
	}
    }
}

/*---------------------------------------------------------------------------*/
void FM_process_nodeinfo( NodeInfo *s )
{
    FM_getenv_nodeinfo( s );
    FM_setenv_nodenames( s );
    FM_freeenv_nodenames( s );
}

/*---------------------------------------------------------------------------*/
