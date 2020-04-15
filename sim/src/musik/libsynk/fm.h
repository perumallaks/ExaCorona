/*---------------------------------------------------------------------------*/
/* Portable mixed communication FM implementation.                           */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#ifndef __MIX_FM_H
#define __MIX_FM_H

/*---------------------------------------------------------------------------*/
#include "mycompat.h"

#define TIMER_DIFF(_t1, _t2) (TIMER_SECONDS(_t1)-TIMER_SECONDS(_t2))

/*---------------------------------------------------------------------------*/
#define FM_CONTINUE 1
#define FM_ABORT 0
#define FM_ANY_ID -1

/*---------------------------------------------------------------------------*/
typedef unsigned long ULONG;

/*---------------------------------------------------------------------------*/
#ifndef FALSE
    #define FALSE 0
    #define TRUE  1
#endif

/*---------------------------------------------------------------------------*/
typedef void *FM_stream;

/*---------------------------------------------------------------------------*/
typedef int FM_handler(FM_stream *, unsigned);

/*---------------------------------------------------------------------------*/
void FM_pre_init( int *pac, char ***av );/*Appl MUST call PreInit if using MPI*/
void FM_initialize(const char *stdout_fname);
void FM_finalize(void);
FM_stream *FM_begin_message(ULONG, ULONG, ULONG);
void FM_send_piece(FM_stream *, void *, ULONG);
void FM_end_message(FM_stream *);
void FM_receive(void *, FM_stream *, unsigned int);
int FM_extract(unsigned int maxbytes);
ULONG FM_register_handler(ULONG , FM_handler *);
void FM_set_parameter(int, int);
int FM_debug_level(int);

/*---------------------------------------------------------------------------*/
extern ULONG FM_nodeid;
extern ULONG FM_numnodes;

/*---------------------------------------------------------------------------*/
#if MPI_AVAILABLE
  #define MAX_PE 512000 /*CUSTOMIZE*/
#elif TCP_AVAILABLE
  #define MAX_PE 128 /*CUSTOMIZE*/
#else
  #define MAX_PE 1024 /*CUSTOMIZE*/
#endif
#define FM_MAX_HANDLERS 64 /*CUSTOMIZE*/
extern FM_handler **FM_handler_table;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int nproc;
    int my_index; /* 0 <= x < nproc */
    char *node_names[MAX_PE]; /* 0..nproc-1 */
} NodeInfo;
void FM_getenv_nodeinfo( NodeInfo *s );
void FM_setenv_nodenames( const NodeInfo *s );
void FM_freeenv_nodenames( NodeInfo *s );
void FM_process_nodeinfo( NodeInfo *s );

/*---------------------------------------------------------------------------*/
/* Transport type support                                                    */
/*---------------------------------------------------------------------------*/
typedef int FM_Transport;
#define FM_TRANSPORT_RELIABLE 1
#define FM_TRANSPORT_UNRELIABLE 2

int FM_IsTransportSupported( FM_Transport t );
FM_Transport FM_GetTransport( void );
int FM_SetTransport( FM_Transport t );
void FM_RemovePeer(unsigned senderID);

/*---------------------------------------------------------------------------*/
void FML_FMInit(const char *stdout_fname);
long FML_RegisterHandler(unsigned int *, FM_handler);
void FML_Barrier(void);
void FML_FinalBarrier(void);

/*---------------------------------------------------------------------------*/
#endif /* __MIX_FM_H */
