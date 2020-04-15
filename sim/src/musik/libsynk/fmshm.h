/*---------------------------------------------------------------------------*/
/* Portable shared-memory FM-like interface using SysV shm calls.            */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#ifndef __SHM_FM_H
#define __SHM_FM_H

/*---------------------------------------------------------------------------*/
#define SHMMAXPE 8 /*CUSTOMIZE*/
#define SHMMAXPIECES 8
#define SHMMAXDATALEN 2560 /*CUSTOMIZE*/
#define SHMMAXPIECELEN SHMMAXDATALEN

/*---------------------------------------------------------------------------*/
struct _SHMMsg;
typedef struct _SHMMsg SHM_stream;

/*---------------------------------------------------------------------------*/
typedef int SHMCallback(int, SHM_stream *, int, int, int);

/*---------------------------------------------------------------------------*/
void SHM_initialize(int, int, SHMCallback *);
void SHM_finalize(void);
SHM_stream *SHM_begin_message(int, int, int, int, int);
void SHM_send_piece(SHM_stream *, void *, int);
void SHM_end_message(SHM_stream *);
void SHM_receive(void *, SHM_stream *, unsigned int);
int SHM_numpieces(SHM_stream *);
int SHM_piecelen(SHM_stream *, int);
int SHM_extract(unsigned int maxbytes);
int SHM_debug_level(int);

/*---------------------------------------------------------------------------*/
extern int SHM_nodeid;
extern int SHM_numnodes;

/*---------------------------------------------------------------------------*/
#endif /* __SHM_FM_H */
