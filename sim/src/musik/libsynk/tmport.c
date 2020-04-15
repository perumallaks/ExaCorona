/*----------------------------------------------------------------------------*/
/* Portals support for reductions used inside TM.                             */
/* Author(s): Kalyan S. Perumalla and Alfred J. Park */
/*----------------------------------------------------------------------------*/
#if PORTALS_TYPE_CRAY
  #include <portals/portals3.h>
  #include <portals/nal.h>
  #define PORTALS_TABLE_INDEX 37 /*CUSTOMIZE (Cray-alloted shared by NCCS/VT)*/
  #define PE2PTLID( _ni, _pe, _ptlid, _rc) do{ \
      (_ptlid) = (_ni)->map[(_pe)]; \
      (_rc)=PTL_OK; \
      }while(0)
  #define PTLINITPIDMAP( _ni, _plim, _rc, _rv ) do{ \
      (_rc)=PtlNIInit( IFACE_FROM_BRIDGE_AND_NALID(PTL_BRIDGE_UK,PTL_IFACE_SS),\
                       PTL_PID_ANY, NULL, &(_plim), &((_ni)->nihdl) ); \
      MYASSERT( (_rc) == PTL_OK || (_rc) == PTL_IFACE_DUP, \
                ("Bad PTLNInit %d %s\n", (_rc), ptl_err_str[(_rc)]) ); \
      (_rc) = PMI_Initialized( &(_rv) ); \
      if( (_rv) == 0 ) \
      { \
          (_rc) = PMI_Init( &(_rv) ); \
          MYASSERT( (_rc) == PMI_SUCCESS, ("Bad PMI_Init %d\n", (_rc)) ); \
      } \
      (_rc) = PMI_CNOS_Get_nidpid_map( &(_ni)->map ); \
      MYASSERT( (_rc) == PMI_SUCCESS, ("Bad Get_nidpid_map %d\n", (_rc)) ); \
      (_rc) = PtlGetId( (_ni)->nihdl, &(_ni)->id ); \
      MYASSERT( (_rc) ==PTL_OK, ("Bad PtlGetID %d\n", (_rc)) ); \
      }while(0)
#elif PORTALS_TYPE_REFERENCE
  #include <portals3.h>
  #include <p3nal_utcp.h>
  #include <p3rt/p3rt.h>
  #include <p3api/debug.h>
  #define PORTALS_TABLE_INDEX 7 /*CUSTOMIZE*/
  #define PE2PTLID(_ni,_pe,_ptlid,_rc) do{ \
      (_rc) = PtlGetRankId((_ni)->nihdl, (_pe), &(_ptlid)); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlGetRankId: %d %s\n", \
               (_rc), PtlErrorStr((_rc)))); \
      }while(0)
  #define PTLINITPIDMAP( _ni, _plim, _rc, _rv ) do{ \
      unsigned rank, size; \
      char tmp[32]; /* 64-bit unsigned int shouldn't exceed 20 digits */ \
      sprintf(tmp, "%lu", FM_nodeid); \
      setenv("PTL_MY_RID", tmp, 1); \
      (_rc) = PtlSetRank(PTL_INVALID_HANDLE, -1, -1); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlSetRank: %s\n", PtlErrorStr((_rc)))); \
      (_rc) = PtlNIInit(PTL_IFACE_DEFAULT, FM_nodeid, NULL, \
          &(_plim), &((_ni)->nihdl)); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlNIInit: %s\n", PtlErrorStr((_rc)))); \
      (_rc) = PtlGetRank((_ni)->nihdl, &rank, &size); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlGetRank: %s\n", PtlErrorStr((_rc)))); \
      printf("fm id: %lu of %lu, ptl_rank %u ptl_size %u\n", FM_nodeid, \
          FM_numnodes, rank, size); \
      MYASSERT(rank == FM_nodeid && size == FM_numnodes, \
          ("rank and size mismatch to FM_*")); \
      (_rc) = PtlGetId( (_ni)->nihdl, &(_ni)->id ); \
      MYASSERT( (_rc) ==PTL_OK, ("Bad PtlGetID %d\n", (_rc)) ); \
      MYASSERT(FM_nodeid == (_ni)->id.pid, ("pid to FM_nodeid mismatch")); \
      }while(0)
  #ifndef PTL_NOACK_REQ
    #define PTL_NOACK_REQ PTL_NO_ACK_REQ
  #endif
#else
  #error Unknown PORTALS_TYPE
#endif

#ifndef PMI_SUCCESS
  #define PMI_SUCCESS 0
#endif /*PMI_SUCCESS*/

/*----------------------------------------------------------------------------*/
typedef struct
{
    ptl_pt_index_t tabidx;
    ptl_handle_ni_t nihdl;
    ptl_handle_eq_t send_eq_h, recv_eq_h;
    int send_eq_size, recv_eq_size;
    ptl_process_id_t id, *map;
    long pending_sends, total_sends, total_recvs;
} ITMPTL_NetInfo;
static ITMPTL_NetInfo tmptl_netinfo;

/*----------------------------------------------------------------------------*/
static void ITMPTL_Setup( int tabidx, int sendeqsz, int recveqsz )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int retval, retcode;
    int num_interfaces;
    ptl_ni_limits_t ptl_limits;

    ni->tabidx = tabidx;

    ni->send_eq_size = sendeqsz;
    ni->recv_eq_size = recveqsz;

    ni->pending_sends = 0;
    ni->total_sends = 0; ni->total_recvs = 0;

    retcode = PtlInit( &num_interfaces );
    MYASSERT( retcode == PTL_OK, ("Bad PtlInit %d\n", retcode) );

    PTLINITPIDMAP( ni, ptl_limits, retcode, retval );

    retcode = PtlEQAlloc( ni->nihdl, ni->send_eq_size, NULL, &ni->send_eq_h );
    MYASSERT( retcode == PTL_OK, ("Bad PtlEQAlloc %d\n", retcode ) );

    retcode = PtlEQAlloc( ni->nihdl, ni->recv_eq_size, NULL, &ni->recv_eq_h );
    MYASSERT( retcode == PTL_OK, ("Bad PtlEQAlloc %d\n", retcode ) );
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_Teardown( int finish )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    if( finish )
    {
        PtlNIFini( ni->nihdl );
        PtlFini();
    }
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_PrepareToSend( ptl_handle_md_t *md_h,
    void *buf2send, int size2send, void *identifier )
{
    const ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int retcode;
    ptl_md_t md;

    md.start = buf2send;
    md.max_size = size2send;

    md.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE;
    md.eq_handle = ni->send_eq_h;
    md.threshold = PTL_MD_THRESH_INF;
    md.user_ptr  = identifier;
    retcode = PtlMDBind( ni->nihdl, md, PTL_RETAIN, md_h );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMDBind %d %s\n", retcode, ptl_err_str[retcode]) );
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_Send( ptl_handle_md_t *local_mdh,
    ptl_size_t loc_offset, ptl_match_bits_t mbits, ptl_size_t rem_offset,
    ptl_size_t bytes, int dest, ptl_hdr_data_t hdr )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int retcode;
    ptl_size_t offset_local = 0;
    ptl_process_id_t ptldest;

    PE2PTLID( ni, dest, ptldest, retcode );
    retcode = PtlPutRegion(
                    *local_mdh,
                    loc_offset,
                    bytes,
                    PTL_NOACK_REQ,
                    ptldest,
                    ni->tabidx,
                    0,  /*no access ctl*/
                    mbits,
                    rem_offset,
                    hdr
                    );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlPutRegion %d %s\n", retcode, ptl_err_str[retcode] ) );
    ni->pending_sends++;
    ni->total_sends++;
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_FreeSendHandle( ptl_handle_md_t *mdh )
{
    int retcode = PtlMDUnlink( *mdh );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMDUnlink %d %s", retcode, ptl_err_str[retcode]) );
    free( mdh );
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_FinishAllPendingSends( void )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int retcode;
    ptl_event_t ev;
    ptl_event_t *pev=&ev;

    pev->type=0;
    while( ni->pending_sends > 0 )
    {
        retcode = PtlEQWait( ni->send_eq_h, pev );
        MYASSERT( retcode == PTL_OK,
                  ("Bad PtlEQWait %d %s\n", retcode, ptl_err_str[retcode]) ); 

        MYASSERT( pev->ni_fail_type == PTL_NI_OK,
                  ("Bad fail type %d\n", pev->ni_fail_type) ); 

        if( pev->type == PTL_EVENT_SEND_END )
        {
            ni->pending_sends--; /*One down*/
        }
        else /*Modify here if ACKs are requested for put*/
        {
            MYASSERT( 0, ("Unknown event %d\n", pev->type) );
        }
    }
}

/*----------------------------------------------------------------------------*/
static ptl_handle_md_t *ITMPTL_PrepareToRecv( ptl_match_bits_t mbits,
    ptl_match_bits_t ignbits, void *buf2recv, int size2recv, void *identifier )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int retcode;
    ptl_md_t md;
    ptl_process_id_t match_id;
    ptl_handle_me_t me_h;
    ptl_handle_md_t *md_h = (ptl_handle_md_t *)malloc(sizeof(ptl_handle_md_t));

    match_id.nid = PTL_NID_ANY;
    match_id.pid = PTL_PID_ANY;

    /*Attach ME to table*/
    retcode = PtlMEAttach( ni->nihdl, ni->tabidx, match_id, mbits, ignbits,
                           PTL_RETAIN, PTL_INS_AFTER, &me_h );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMEAttach %d %s\n", retcode, ptl_err_str[retcode]) );

    /*Attach MD to this ME*/
    md.user_ptr = identifier;
    md.start = buf2recv;
    md.length = size2recv;
    md.eq_handle = ni->recv_eq_h;
    md.max_size = 0;
    md.threshold = PTL_MD_THRESH_INF;
    md.options = PTL_MD_OP_GET | PTL_MD_OP_PUT |
                 PTL_MD_EVENT_START_DISABLE | PTL_MD_MANAGE_REMOTE;
    
    retcode = PtlMDAttach( me_h, md, PTL_RETAIN, md_h );
    MYASSERT( retcode == PTL_OK, ("Bad PtlMDAttach %d\n", retcode) );

    return md_h;
}

/*----------------------------------------------------------------------------*/
static void ITMPTL_FreeRecvHandle( ptl_handle_md_t *mdh )
{
    int retcode = PtlMDUnlink( *mdh );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMDUnlink %d %s", retcode, ptl_err_str[retcode]) );
    free( mdh );
}

/*----------------------------------------------------------------------------*/
static int ITMPTL_NonBlockRecvOneEvent( ptl_size_t *offset,
    ptl_size_t *size, ptl_hdr_data_t *hdr, void **user_ptr )
{
    ITMPTL_NetInfo *ni = &tmptl_netinfo;
    int found = FALSE;
    int retcode;
    ptl_event_t ev;
    ptl_event_t *pev=&ev;

    pev->type = 0;

    retcode = PtlEQGet( ni->recv_eq_h, pev );
    MYASSERT( retcode == PTL_OK || retcode == PTL_EQ_DROPPED ||
              retcode == PTL_EQ_EMPTY,
              ("Bad PtlEQGet %d %s\n", retcode, ptl_err_str[retcode]) );

    MYASSERT( retcode == PTL_EQ_EMPTY || pev->ni_fail_type == PTL_NI_OK,
              ("Bad PtlEQGet fail_type %u\n", pev->ni_fail_type) );

    if( retcode == PTL_EQ_DROPPED )
    {
        /*Deal with having lost some event due to event queue being full*/
        MYASSERT( 0, ("Event queue overflow to be completed\n") );
    }
    else if( retcode == PTL_EQ_EMPTY )
    {
        *offset = 0;
        *size = 0;
        *hdr = 0;
        *user_ptr = 0;
        found = FALSE;
    }
    else if( pev->type == PTL_EVENT_PUT_END )
    {
        *offset = pev->offset;
        *size = pev->mlength;        
        *hdr = pev->hdr_data;
        *user_ptr = pev->md.user_ptr;

        ni->total_recvs++;
        found = TRUE;
    }
    else
    {
        MYASSERT( 0, ("Unexpected event %d\n", pev->type) );
    }

    return found;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
typedef struct{ TMMesg msg; } RELEM;
void *sbuf, *rbuf;
RELEM *svmesgs, *ssmesgs, *rvmesgs, *rsmesgs;
ptl_handle_md_t *psend_mdh;
ptl_match_bits_t matchbits, ignbits;
struct { int sv, ss, rv, rs; } arrsz;
struct { long sv, ss, rv, rs; } batchcounts, totcounts;
int tabidx;
/*----------------------------------------------------------------------------*/
void TMPTL_Init(int myid, int maxvsend, int maxssend, int maxvrecv,int maxsrecv)
{
    int i = 0;

    /*Remember the configuration*/
    arrsz.sv = maxvsend; arrsz.ss = maxssend;
    arrsz.rv = maxvrecv; arrsz.rs = maxsrecv;

    /*Portals-specific config constants*/
    matchbits = 0xFF; /*XXX Arbitrary*/
    ignbits = 0xFFFFFFFFFFFFFF00;
    tabidx = PORTALS_TABLE_INDEX;

    int sbufbytesz = (arrsz.sv+arrsz.ss) * sizeof(RELEM);
    sbuf = (void*)calloc( (arrsz.sv+arrsz.ss), sizeof(RELEM) );
    svmesgs = ((RELEM*)sbuf) + 0;
    ssmesgs = ((RELEM*)sbuf) + arrsz.sv;

    int rbufbytesz = (arrsz.rv+arrsz.rs)*4 * sizeof(RELEM);
    rbuf = (void*)calloc( (arrsz.rv+arrsz.rs)*4, sizeof(RELEM) );
    rvmesgs = ((RELEM*)rbuf) + 0;
    rsmesgs = ((RELEM*)rbuf) + (4*arrsz.rv);

    /*Initialize portals, with event queues (four events per mesg)*/
    ITMPTL_Setup( tabidx, (arrsz.sv+arrsz.ss)*4, (arrsz.rv+arrsz.rs)*4 );

    /*Set up recv (one event queue (& one memory descriptor) for all recvs)*/
    ITMPTL_PrepareToRecv( matchbits, ignbits, rbuf, rbufbytesz, NULL );

    /*Set up sends*/
    psend_mdh = (ptl_handle_md_t *)calloc( 1, sizeof(ptl_handle_md_t) );
    ITMPTL_PrepareToSend(psend_mdh, sbuf, (arrsz.sv+arrsz.ss)*sizeof(RELEM), 0);
}

/*----------------------------------------------------------------------------*/
void TMPTL_Finish( int myid )
{
    ITMPTL_Teardown( FALSE );
}

/*----------------------------------------------------------------------------*/
/*Given rslot w/in recv value block corr. to ssn&trial, find corr. value mesg*/
#define RVSLOT2PTR( _ptr, _ssn, _trial, _rslot ) do{ \
        int _slotoff = (((_ssn)%2)*2 + ((_trial)%2))*arrsz.rv; \
        (_ptr) = &rvmesgs[_slotoff+(_rslot)]; \
    }while(0)
/*Given value mesg, find ssn&trial it belongs to, and rslot w/in that block*/
#define RVPTR2SLOT( _ptr, _ssn, _trial, _rslot ) do{ \
        int _slotoff = ((_ptr) - rvmesgs); \
        int _blocknum = (_slotoff/arrsz.rv); \
        MYASSERT( 0 <= _blocknum && _blocknum < 4, ("Sanity %d",_blocknum) ); \
        (_ssn)   = _blocknum/2; \
        (_trial) = _blocknum%2; \
        (_rslot) = _slotoff - ((_ssn)*2 + (_trial))*arrsz.rv; \
    }while(0)
/*Given rslot w/in recv start block corr. to ssn&trial, find corr. value mesg*/
#define RSSLOT2PTR( _ptr, _ssn, _trial, _rslot ) do{ \
        int _slotoff = (((_ssn)%2)*2 + ((_trial)%2))*arrsz.rs; \
        (_ptr) = &rsmesgs[_slotoff+(_rslot)]; \
    }while(0)
/*Given start mesg, find ssn&trial it belongs to, and rslot w/in that block*/
#define RSPTR2SLOT( _ptr, _ssn, _trial, _rslot ) do{ \
        int _slotoff = ((_ptr) - rsmesgs); \
        int _blocknum = (_slotoff/arrsz.rs); \
        MYASSERT( 0 <= _blocknum && _blocknum < 4, ("Sanity %d",_blocknum) ); \
        (_ssn)   = _blocknum/2; \
        (_trial) = _blocknum%2; \
        (_rslot) = _slotoff - ((_ssn)*2 + (_trial))*arrsz.rs; \
    }while(0)
/*----------------------------------------------------------------------------*/
typedef void (*TMPTL_CallBackFuncType)( const TMMesg *pmsg );
int TMPTL_RecvMesgs( int myid, TMPTL_CallBackFuncType enqcb )
{
    int nrecd = 0;
    int got = TRUE;
    ptl_size_t offset = 0, size = 0;
    ptl_hdr_data_t hdr;
    void *user_ptr = 0;
    while( (got=ITMPTL_NonBlockRecvOneEvent(&offset, &size, &hdr, &user_ptr)) )
    {
        RELEM *relem = 0;
        TMMesg *tmmsg = 0;
        long offset_nslots = (offset/sizeof(RELEM));
        int isval = (offset_nslots < (arrsz.rv*4));
        int ssn = 0, trial = 0, rslot = 0;

        MYASSERT( offset%sizeof(RELEM) == 0, ("Fractional TM message?!") );

        if( isval ) RVPTR2SLOT( rvmesgs+offset_nslots, ssn, trial, rslot );
        else RSPTR2SLOT( rsmesgs+(offset_nslots-arrsz.rv*4), ssn, trial, rslot);

        relem = isval ? &rvmesgs[offset_nslots] :
                        &rsmesgs[offset_nslots-arrsz.rv*4];
        tmmsg = &relem->msg;

        MYASSERT( isval == (tmmsg->type == RM_VALUE_MSG), ("") );
        MYASSERT( ssn == tmmsg->ssn%2,
                  ("Circular sanity check of ssn: %d %ld", ssn, tmmsg->ssn) );
        MYASSERT( trial == tmmsg->trial%2,
                  ("Circular sanity check of trial: %d %ld",trial,tmmsg->trial));
        MYASSERT( myid == tmmsg->to_pe,
                 ("Circular sanity check of recv PE: %d %ld",myid,tmmsg->to_pe));

        (*enqcb)( tmmsg ); /*Submit to TM*/
        nrecd++;

        (isval ? batchcounts.rv++ : batchcounts.rs++);
        (isval ? totcounts.rv++ : totcounts.rs++);
    }
    return nrecd;
}

/*----------------------------------------------------------------------------*/
void TMPTL_SendMesg( int myid, const TMMesg *msg, int rslot )
{
    int isval = (msg->type == RM_VALUE_MSG);
    ptl_handle_md_t *loc_mdh = psend_mdh;
    ptl_size_t loc_offset = 0;
    ptl_match_bits_t mbits = matchbits;
    int blockslot = ((msg->ssn%2)*2+(msg->trial%2))*(isval? arrsz.rv :arrsz.rs);
    int globslot = (isval ? 0 : (arrsz.rv*4)) + blockslot + (rslot<0?0:rslot);
    ptl_size_t rem_offset = globslot * sizeof(RELEM);
    ptl_size_t nbytes = sizeof(RELEM);
    int destpe = msg->to_pe;
    ptl_hdr_data_t hdr = 0;

    MYASSERT( myid != msg->to_pe,("Can't send to self: %d %ld",myid,msg->to_pe));
    MYASSERT( myid == msg->from_pe,
              ("Circular sanity check of send PE: %d %ld", myid, msg->from_pe) );
    MYASSERT( batchcounts.sv < arrsz.sv, ("%ld %d", batchcounts.sv, arrsz.sv) );
    MYASSERT( batchcounts.ss < arrsz.ss, ("%ld %d", batchcounts.ss, arrsz.ss) );
    loc_offset = sizeof(RELEM)*
                 ((isval ? batchcounts.sv : arrsz.sv+batchcounts.ss));

    ((RELEM*)(((char*)sbuf)+loc_offset))->msg = *msg;
    ITMPTL_Send( loc_mdh, loc_offset, mbits, rem_offset, nbytes, destpe, hdr );

    (isval ? batchcounts.sv++ : batchcounts.ss++);
    (isval ? totcounts.sv++ : totcounts.ss++);
}

/*----------------------------------------------------------------------------*/
void TMPTL_FlushSends( int myid )
{
    batchcounts.sv = 0;
    batchcounts.ss = 0;
    batchcounts.rv = 0;
    batchcounts.rs = 0;

    ITMPTL_FinishAllPendingSends();
}

/*----------------------------------------------------------------------------*/
