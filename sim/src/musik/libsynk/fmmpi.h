/*---------------------------------------------------------------------------*/
/* FM-like interface for MPI communication.                                  */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#ifndef __FMMPI_FM_H
#define __FMMPI_FM_H

/*---------------------------------------------------------------------------*/
#define FMMPIMAXPE 512000 /*CUSTOMIZE*/
#define FMMPIMAXPIECES 16
#define FMMPIMAXDATALEN 2048 /*XXX CUSTOMIZE*/
#define FMMPIMAXPIECELEN FMMPIMAXDATALEN

/*---------------------------------------------------------------------------*/
typedef void FMMPI_stream;

/*---------------------------------------------------------------------------*/
typedef int FMMPICallback(int, FMMPI_stream *, int, int, int);

/*---------------------------------------------------------------------------*/
void FMMPI_pre_initialize(int *, char ***);
void FMMPI_post_initialize(void);
void FMMPI_initialize(int, int, FMMPICallback *);
void FMMPI_finalize(void);
FMMPI_stream *FMMPI_begin_message(int, int, int, int, int);
void FMMPI_send_piece(FMMPI_stream *, void *, int);
void FMMPI_end_message(FMMPI_stream *);
int FMMPI_receive(void *, FMMPI_stream *, unsigned int);
int FMMPI_numpieces(FMMPI_stream *);
int FMMPI_piecelen(FMMPI_stream *, int);
int FMMPI_extract(unsigned int maxbytes);
int FMMPI_debug_level(int);

/*---------------------------------------------------------------------------*/
extern int FMMPI_nodeid;
extern int FMMPI_numnodes;

/*---------------------------------------------------------------------------*/
#endif /* __FMMPI_FM_H */
