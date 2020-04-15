/*----------------------------------------------------------------------------*/
/* Portals support for FM.                                                    */
/* Author(s): Kalyan S. Perumalla */
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
      sprintf(tmp, "%d", FMMPI_nodeid); \
      setenv("PTL_MY_RID", tmp, 1); \
      (_rc) = PtlSetRank(PTL_INVALID_HANDLE, -1, -1); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlSetRank: %s\n", PtlErrorStr((_rc)))); \
      (_rc) = PtlNIInit(PTL_IFACE_DEFAULT, FMMPI_nodeid, NULL, \
          &(_plim), &((_ni)->nihdl)); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlNIInit: %s\n", PtlErrorStr((_rc)))); \
      (_rc) = PtlGetRank((_ni)->nihdl, &rank, &size); \
      MYASSERT((_rc) == PTL_OK, ("Bad PtlGetRank: %s\n", PtlErrorStr((_rc)))); \
      printf("fm id: %d of %d, ptl_rank %u ptl_size %u\n", FMMPI_nodeid, \
          FMMPI_numnodes, rank, size); \
      MYASSERT(rank == FMMPI_nodeid && size == FMMPI_numnodes, \
          ("rank and size mismatch to FM_*")); \
      (_rc) = PtlGetId( (_ni)->nihdl, &(_ni)->id ); \
      MYASSERT( (_rc) ==PTL_OK, ("Bad PtlGetID %d\n", (_rc)) ); \
      MYASSERT(FMMPI_nodeid == (_ni)->id.pid, ("pid to nodeid mismatch")); \
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
    ptl_process_id_t id, *map;
    ptl_handle_eq_t send_eq_h, recv_eq_h;
    ptl_match_bits_t matchbits, ignbits;
    int send_eq_size, recv_eq_size;
    long pending_sends, total_sends, total_recvs;
} IFMPTL_NetInfo;
static IFMPTL_NetInfo fmptl_netinfo;

/*----------------------------------------------------------------------------*/
typedef struct fmptl_md
{
    int bufid;
    int bufsz;
    char *buf;
    ptl_md_t md;
    ptl_handle_md_t pmdh;
    struct fmptl_md *prev, *next;
} FMPTL_MD;
#define FMPTL_MD_LIST_ADD( _list, _md ) do { \
        MYASSERT( !(_md)->prev && !(_md)->next, \
                  ("%p %p",(_md)->prev,(_md)->next) ); \
        (_md)->prev = 0; \
        (_md)->next = (_list); \
        if(_list) (_list)->prev = _md; \
        (_list) = _md; \
    }while(0)
#define FMPTL_MD_LIST_DETACH( _list, _md ) do { \
        if( (_md)->prev ) (_md)->prev->next = (_md)->next; \
        if( (_md)->next ) (_md)->next->prev = (_md)->prev; \
        if( (_list) == (_md) ) (_list) = (_md)->next; \
        (_md)->next = 0; \
        (_md)->prev = 0; \
    }while(0)
typedef FMPTL_MD FMPTL_SendMD;
typedef FMPTL_MD FMPTL_RecvMD;
static FMPTL_SendMD *smd_free;
static FMPTL_RecvMD *rmd_free;
static long nsmd, nrmd;

/*----------------------------------------------------------------------------*/
static void IFMPTL_Setup( int tabidx, int sendeqsz, int recveqsz )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
    int retval, retcode;
    int num_interfaces;
    ptl_ni_limits_t ptl_limits;

    ni->tabidx = tabidx;

    ni->send_eq_size = sendeqsz;
    ni->recv_eq_size = recveqsz;

    ni->pending_sends = 0;
    ni->total_sends = 0;
    ni->total_recvs = 0;

    ni->matchbits = 0xEF; /*XXX CUSTOMIZE*/
    ni->ignbits = 0xFFFFFFFFFFFFFF00; /*XXX CUSTOMIZE*/

    retcode = PtlInit( &num_interfaces );
    MYASSERT( retcode == PTL_OK, ("Bad PtlInit %d\n", retcode) );

    PTLINITPIDMAP( ni, ptl_limits, retcode, retval );

    retcode = PtlEQAlloc( ni->nihdl, ni->send_eq_size, NULL, &ni->send_eq_h );
    MYASSERT( retcode == PTL_OK, ("Bad PtlEQAlloc %d\n", retcode ) );

    retcode = PtlEQAlloc( ni->nihdl, ni->recv_eq_size, NULL, &ni->recv_eq_h );
    MYASSERT( retcode == PTL_OK, ("Bad PtlEQAlloc %d\n", retcode ) );
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_PrepareToSend( ptl_handle_md_t *md_h,
    void *buf2send, int size2send, void *identifier )
{
    const IFMPTL_NetInfo *ni = &fmptl_netinfo;
    int retcode;
    ptl_md_t md;

    MYASSERT( md_h, ("") );
    MYASSERT( buf2send, ("") );
    MYASSERT( size2send > 0, ("%d",size2send) );

    md.start = buf2send;
    md.length = size2send;
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
static void IFMPTL_Send( ptl_handle_md_t *local_mdh,
    ptl_size_t loc_offset, ptl_size_t rem_offset,
    ptl_size_t nbytes, int dest, ptl_hdr_data_t hdr )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
    int retcode = 0;
    ptl_process_id_t ptldest;

#if !NODEBUG
if(mpifmdbg>=6){printf("%d: IFMPTL_Send bufsz=%d pmdh=%p locoff=%d\n",FMMPI_nodeid,nbytes,local_mdh,loc_offset);fflush(stdout);}
#endif

    PE2PTLID( ni, dest, ptldest, retcode );
    retcode = PtlPutRegion(
                    *local_mdh,
                    loc_offset,
                    nbytes,
                    PTL_NOACK_REQ,
                    ptldest,
                    ni->tabidx,
                    0,  /*no access ctl*/
                    ni->matchbits,
                    rem_offset,
                    hdr
                    );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlPutRegion %d %s\n", retcode, ptl_err_str[retcode] ) );
    ni->pending_sends++;
    ni->total_sends++;
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_FreeSendHandle( ptl_handle_md_t *mdh )
{
    int retcode = PtlMDUnlink( *mdh );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMDUnlink %d %s", retcode, ptl_err_str[retcode]) );
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_WaitForPendingSends( int max2wait )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
    int retcode = 0, nwaited = 0;
    ptl_event_t ev;
    ptl_event_t *pev=&ev;

    pev->type = 0;
    while( (nwaited < max2wait) && (ni->pending_sends > 0) )
    {
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: WaitForPendingSends %ld\n",FMMPI_nodeid,ni->pending_sends);fflush(stdout);}
#endif
        retcode = PtlEQWait( ni->send_eq_h, pev );
        nwaited++;
        MYASSERT( retcode == PTL_OK,
                  ("Bad PtlEQWait %d %s\n", retcode, ptl_err_str[retcode]) ); 

        MYASSERT( pev->ni_fail_type == PTL_NI_OK,
                  ("Bad fail type %d\n", pev->ni_fail_type) ); 

        if( pev->type == PTL_EVENT_SEND_END )
        {
            FMPTL_SendMD *smd = (FMPTL_SendMD *)pev->md.user_ptr;
            FMPTL_MD_LIST_ADD( smd_free, smd );
            ni->pending_sends--;
        }
        else
        {
            MYASSERT( 0, ("Unknown event %d\n", pev->type) );
        }
    }
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_FinishAllPendingSends( void )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: FinishAllPendingSends %ld\n",FMMPI_nodeid,ni->pending_sends);fflush(stdout);}
#endif
    while( ni->pending_sends > 0 )
    {
        IFMPTL_WaitForPendingSends( ni->pending_sends );
    }
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: FinishAllPendingSends %ld done\n",FMMPI_nodeid,ni->pending_sends);fflush(stdout);}
#endif
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_ServiceSendEventQ( void )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;

    for(; ni->pending_sends > 0;)
    {
        int retcode = 0;
        ptl_event_t ev;
        ptl_event_t *pev=&ev;
        pev->type = 0;

        retcode = PtlEQGet( ni->send_eq_h, pev );
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
            break;
        }
        else if( pev->type == PTL_EVENT_SEND_END )
        {
            FMPTL_SendMD *smd = (FMPTL_SendMD *)pev->md.user_ptr;
            FMPTL_MD_LIST_ADD( smd_free, smd );
            MYASSERT( ni->pending_sends > 0, ("%ld", ni->pending_sends) );
            ni->pending_sends--;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: ServiceSendEventQ reclaimed SMDID %d pending %ld\n",FMMPI_nodeid,smd->bufid,ni->pending_sends);fflush(stdout);}
#endif
        }
        else
        {
            MYASSERT( 0, ("Unexpected event %d\n", pev->type) );
        }
    }
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_PrepareRecvMD( ptl_md_t *pmd,
    void *buf2recv, int size2recv, void *identifier )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
    pmd->user_ptr  = identifier;
    pmd->start     = buf2recv;
    pmd->length    = size2recv;
    pmd->eq_handle = ni->recv_eq_h;
    pmd->max_size  = 0;
    pmd->threshold = PTL_MD_THRESH_INF;
    pmd->options   = PTL_MD_OP_GET | PTL_MD_OP_PUT |
                     PTL_MD_EVENT_START_DISABLE; /*and, no manage-remote*/
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_PrepareToRecv( ptl_handle_md_t *md_h,
    void *buf2recv, int size2recv, void *identifier, ptl_md_t *pmd )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
    int retcode;
    ptl_handle_me_t me_h;
    ptl_process_id_t match_id;

    match_id.nid = PTL_NID_ANY;
    match_id.pid = PTL_PID_ANY;

    /*Attach ME to table*/
    retcode = PtlMEAttach( ni->nihdl, ni->tabidx, match_id,
                           ni->matchbits, ni->ignbits,
                           PTL_UNLINK, PTL_INS_AFTER, &me_h );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMEAttach %d %s\n", retcode, ptl_err_str[retcode]) );

    /*Attach MD to this ME*/
    IFMPTL_PrepareRecvMD( pmd, buf2recv, size2recv, identifier );
    retcode = PtlMDAttach( me_h, *pmd, PTL_UNLINK, md_h );
    MYASSERT( retcode == PTL_OK,
              ("Bad PtlMDAttach %d %s\n", retcode, ptl_err_str[retcode]) );
}

/*----------------------------------------------------------------------------*/
static int IFMPTL_NonBlockRecvOneEvent( ptl_size_t *offset,
    ptl_size_t *size, ptl_hdr_data_t *hdr, void **pidentifier )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
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
        *pidentifier = 0;
        found = FALSE;
    }
    else if( pev->type == PTL_EVENT_PUT_END )
    {
        *offset = pev->offset;
        *size = pev->mlength;        
        *hdr = pev->hdr_data;
        *pidentifier = pev->md.user_ptr;

        ni->total_recvs++;
        found = TRUE;
    }
    else if( pev->type == PTL_EVENT_UNLINK )
    {
        MYASSERT( 0, ("Unlink event TBC\n") );
    }
    else
    {
        MYASSERT( 0, ("Unexpected event %d\n", pev->type) );
    }

    return found;
}

/*----------------------------------------------------------------------------*/
static void IFMPTL_Teardown( int finish )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: FMPTL_Teardown\n",FMMPI_nodeid);fflush(stdout);}
#endif
    IFMPTL_FinishAllPendingSends();
    if( finish )
    {
        PtlNIFini( ni->nihdl );
        PtlFini();
    }
#if !NODEBUG
if(mpifmdbg>=6){printf("%d: FMPTL_Teardown done\n",FMMPI_nodeid);fflush(stdout);}
#endif
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
FMPTL_SendMD *FMPTL_AllocateSendMD( int bufsz )
{
    FMPTL_SendMD *smd = (FMPTL_SendMD *)malloc( sizeof(FMPTL_SendMD)+bufsz );
    if( smd )
    {
        void *ident = smd;
        smd->bufid = nsmd++;
        smd->bufsz = bufsz;
        smd->buf = (char *)(smd+1);
        smd->prev = 0;
        smd->next = 0;

#if !NODEBUG
if(mpifmdbg>=6){printf("%d: AllocateSendMD bufsz=%d pmdh=%p smd=%p buf=%p\n",FMMPI_nodeid,bufsz,&smd->pmdh,smd,smd->buf);fflush(stdout);}
#endif
        IFMPTL_PrepareToSend( &smd->pmdh, smd->buf, smd->bufsz, ident );
        FMPTL_MD_LIST_ADD( smd_free, smd );
    }
    return smd;
}

/*----------------------------------------------------------------------------*/
FMPTL_SendMD *FMPTL_FindOrAllocSendMD( int bufsz )
{
    FMPTL_SendMD *smd = 0, *fsmd = smd_free;
    IFMPTL_NetInfo *ni = &fmptl_netinfo;

    while( !smd && fsmd )
    {
        if( fsmd->bufsz < bufsz )
        {
            fsmd = fsmd->next;
        }
        else
        {
            smd = fsmd;
        }
    }

    if( smd )
    {
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FindOrAllocSendMD %d found preallocated SMDID %d pending %ld\n",FMMPI_nodeid,bufsz,smd->bufid,ni->pending_sends);fflush(stdout);}
#endif
    }
    else
    {
        smd = FMPTL_AllocateSendMD( bufsz );
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FindOrAllocSendMD %d allocated new SMDID %d pending %ld\n",FMMPI_nodeid,bufsz,smd->bufid,ni->pending_sends);fflush(stdout);}
#endif
    }

    MYASSERT( smd, ("\n\nOut of memory for send buffers?") );

    return smd;
}

/*----------------------------------------------------------------------------*/
FMPTL_RecvMD *FMPTL_AllocateRecvMD( int bufsz )
{
    FMPTL_RecvMD *rmd = (FMPTL_RecvMD *)malloc( sizeof(FMPTL_RecvMD)+bufsz );
    if( rmd )
    {
        void *ident = rmd;
        rmd->bufid = nrmd++;
        rmd->bufsz = bufsz;
        rmd->buf = (char *)(rmd+1);
        rmd->prev = 0;
        rmd->next = 0;
        IFMPTL_PrepareToRecv(&rmd->pmdh, rmd->buf, rmd->bufsz, ident, &rmd->md);
        FMPTL_MD_LIST_ADD( rmd_free, rmd );
    }
    return rmd;
}

/*----------------------------------------------------------------------------*/
FMPTL_RecvMD *FMPTL_FindOrAllocRecvMD( int bufsz )
{
    FMPTL_RecvMD *rmd = 0, *frmd = rmd_free;

    while( !rmd && frmd )
    {
        if( frmd->bufsz < bufsz )
        {
            frmd = frmd->next;
        }
        else
        {
            rmd = frmd;
        }
    }

    if( rmd )
    {
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FindOrAllocRecvMD %d found preallocated RMDID %d\n",FMMPI_nodeid,bufsz,rmd->bufid);fflush(stdout);}
#endif
    }
    else
    {
        rmd = FMPTL_AllocateRecvMD( bufsz );
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FindOrAllocRecvMD %d allocated new RMDID %d\n",FMMPI_nodeid,bufsz,rmd->bufid);fflush(stdout);}
#endif
    }

    MYASSERT( rmd, ("\n\nOut of memory for receive buffers?") );

    return rmd;
}

/*----------------------------------------------------------------------------*/
void FMPTL_CopySendMesg( void *buf, int maxsz, const FMMPISendMsg *fmsmsg )
{
    char *cbuf = (char *)buf;
    const FMMPIMsgHeaderPiece *fmshdr = &fmsmsg->hdr;
    int i = 0, totsz = 0;
    for( i = 0; i < fmshdr->npieces; i++ )
    {
        int plen = fmsmsg->pieces[i].iov_len;
        MYASSERT( fmsmsg->pieces[i].iov_base, ("") );
        MYASSERT( totsz+plen <= maxsz, ("%d + %d <= %d",totsz,plen,maxsz) );
        memcpy( cbuf, fmsmsg->pieces[i].iov_base, plen );
        cbuf += plen;
        totsz += plen;
    }
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
void FMPTL_Init( int myid )
{
    long npe = FMMPI_numnodes;
    const char *sbcstr = getenv("FMPTL_SENDBUFCOUNT");
    const char *sbsstr = getenv("FMPTL_SENDBUFSIZE");
    const char *seqstr = getenv("FMPTL_SENDEQSIZE");

    const char *rbcstr = getenv("FMPTL_RECVBUFCOUNT");
    const char *rbsstr = getenv("FMPTL_RECVBUFSIZE");
    const char *reqstr = getenv("FMPTL_RECVEQSIZE");

    long sbufcount = !sbcstr ? 1 : atol(sbcstr);
    long sbufsize = !sbsstr ? 1024 : atol(sbsstr);
    long seqsize = !seqstr ? 10*npe : atol(seqstr);

    long rbufcount = !rbcstr ? 1 : atol(rbcstr);
    long rbufsize = !rbsstr ? 1024*1024 : atol(rbsstr);
    long reqsize = !reqstr ? 10*npe : atol(reqstr);

    int i = 0;

    LSCFGLD( "FMPTL_SENDBUFCOUNT", (long)sbufcount,
             "FM portals prealloced send buf count" );
    LSCFGLD( "FMPTL_SENDBUFSIZE", (long)sbufsize,
             "FM portals prealloced send buf size" );
    LSCFGLD( "FMPTL_SENDEQSIZE", (long)seqsize,
             "FM portals send event queue size" );
    LSCFGLD( "FMPTL_RECVBUFCOUNT", (long)rbufcount,
             "FM portals prealloced recv buf count" );
    LSCFGLD( "FMPTL_RECVBUFSIZE", (long)rbufsize,
             "FM portals prealloced recv buf size" );
    LSCFGLD( "FMPTL_RECVEQSIZE", (long)reqsize,
             "FM portals recv event queue size" );

    if( myid == 0 )
    {
        printf("FMPTL: SBUF %ld bufs x %ld bytes/buf %ld max-events\n",
               sbufcount, sbufsize, seqsize);
        printf("FMPTL: RBUF %ld bufs x %ld bytes/buf %ld max-events\n",
               rbufcount, rbufsize, reqsize);
        fflush(stdout);
    }

    IFMPTL_Setup( PORTALS_TABLE_INDEX, seqsize, reqsize );

    smd_free = 0;
    nsmd = 0;
    for( i = 0; i < sbufcount; i++ )
    {
        FMPTL_SendMD *smd = FMPTL_AllocateSendMD( sbufsize );
        MYASSERT( smd, ("Alloc %d send buf of size %ld failed",i,sbufsize) );
    }

    rmd_free = 0;
    nrmd = 0;
    for( i = 0; i < rbufcount; i++ )
    {
        FMPTL_RecvMD *rmd = FMPTL_AllocateRecvMD( rbufsize );
        MYASSERT( rmd, ("Alloc %d recv buf of size %ld failed",i,rbufsize) );
    }
}

/*----------------------------------------------------------------------------*/
void FMPTL_Finish( int myid )
{
    IFMPTL_NetInfo *ni = &fmptl_netinfo;

if(myid==0){printf("%d: FMPTL nsent= %ld nrecd= %ld nsmd= %ld nrmd= %ld\n",myid,ni->total_sends,ni->total_recvs,nsmd,nrmd);fflush(stdout);}

    IFMPTL_Teardown( FALSE );
}

/*----------------------------------------------------------------------------*/
void FMPTL_SendMesg( int myid, const FMMPISendMsg *fmsmsg )
{
    const FMMPIMsgHeaderPiece *fmshdr = &fmsmsg->hdr;
    int bufsz = fmshdr->totbytes;
    FMPTL_SendMD *smd = 0;

    IFMPTL_ServiceSendEventQ();
    smd = FMPTL_FindOrAllocSendMD( bufsz );

    MYASSERT( smd, ("Need send buffer") );

    if( smd )
    {
        ptl_size_t loc_offset = 0, rem_offset = 0;
        ptl_size_t nbytes = fmshdr->totbytes;
        int dest = fmshdr->dest_pe;
        ptl_hdr_data_t hdr = (ptl_hdr_data_t)myid;

#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FMPTL_SendMesg SMDID %d nbytes %d to %d\n",FMMPI_nodeid,smd->bufid,nbytes,dest);fflush(stdout);}
#endif

        FMPTL_MD_LIST_DETACH( smd_free, smd );
        FMPTL_CopySendMesg( smd->buf, bufsz, fmsmsg );
        IFMPTL_Send( &smd->pmdh, loc_offset, rem_offset, nbytes, dest, hdr);
    }
}

/*----------------------------------------------------------------------------*/
typedef struct
{
    FMPTL_RecvMD *rmd;
    ptl_size_t offset;
    ptl_size_t size;
    int nbytesrecd;
} FMPTLRecvMsg;
FMPTLRecvMsg rmsg;
int FMPTL_RecvMesg( int myid, FMMPIRecvMsg *fmrmsg, int *pfrom_pe, int ninloop )
{
    ptl_hdr_data_t hdr = 0;
    void *identifier = 0;
    int got = 0;

    IFMPTL_ServiceSendEventQ();

    got = IFMPTL_NonBlockRecvOneEvent( &rmsg.offset, &rmsg.size,
                                       &hdr, &identifier );
    if( got )
    {
        *pfrom_pe = (int)hdr;
        rmsg.nbytesrecd = 0;
        rmsg.rmd = (FMPTL_RecvMD *)identifier;

        MYASSERT( rmsg.rmd && rmsg.rmd->buf, ("") );
        MYASSERT( rmsg.offset+rmsg.size <= rmsg.rmd->bufsz,
                  ("%u %u %u", rmsg.offset, rmsg.size, rmsg.rmd->bufsz) );

#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FMPTL_RecvMesg RMDID %d offset %d nbytes %d from %d\n",FMMPI_nodeid,rmsg.rmd->bufid,rmsg.offset,rmsg.size,*pfrom_pe);fflush(stdout);}
#endif
    }
    else
    {
        int resetoffset = TRUE;
        MYASSERT( ninloop <= 0 || rmsg.rmd, ("") );
        if( (ninloop > 0) && resetoffset )
        {
            IFMPTL_NetInfo *ni = &fmptl_netinfo;
            int retcode = 0;
            ptl_md_t old_md, new_md;
            old_md = rmsg.rmd->md;
            new_md = old_md;
            retcode = PtlMDUpdate( rmsg.rmd->pmdh,
                                   &old_md, &new_md, ni->recv_eq_h );
            if( retcode == PTL_OK )
            {
            }
            else
            {
                /*There were new events since prev PtlEQGet()*/
                MYASSERT( retcode == PTL_MD_NO_UPDATE,
                          ("%d %s", retcode, ptl_err_str[retcode]) );
            }
        }
    }

    return got;
}

/*----------------------------------------------------------------------------*/
int FMPTL_RecvPiece( void *buffer, unsigned int length )
{
#if !NODEBUG
if(mpifmdbg>=6){printf("%d:FMPTL_RecvPiece %d bytes nbytesrecd=%d\n",FMMPI_nodeid,length,rmsg.nbytesrecd);fflush(stdout);}
#endif
    MYASSERT( rmsg.nbytesrecd + length <= rmsg.size,
              ("%d %d %d", rmsg.nbytesrecd, length, rmsg.size) );
    memcpy( buffer, rmsg.rmd->buf+rmsg.offset+rmsg.nbytesrecd, length );
    rmsg.nbytesrecd += length;
}

/*----------------------------------------------------------------------------*/
void FMPTL_FlushSends( int myid )
{
    IFMPTL_FinishAllPendingSends();
}

/*----------------------------------------------------------------------------*/
