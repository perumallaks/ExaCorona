/*---------------------------------------------------------------------------*/
/* FM-like interface to Myrinet GM 1.2 calls.                                */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#ifndef __GM_FM_H
#define __GM_FM_H

/*---------------------------------------------------------------------------*/
#define GMMAXPE 32
#define GMMAXPIECES 8
#define GMMAXDATALEN 64
#define GMMAXPIECELEN GMMAXDATALEN

/*---------------------------------------------------------------------------*/
struct _GMMsg;
typedef struct _GMMsg GM_stream;

/*---------------------------------------------------------------------------*/
typedef int GMCallback(int, GM_stream *, int, int, int);

/*---------------------------------------------------------------------------*/
void GM_initialize(int, int, char *[], GMCallback *);
void GM_finalize(void);
GM_stream *GM_begin_message(int, int, int, int, int);
void GM_send_piece(GM_stream *, void *, int);
void GM_end_message(GM_stream *);
void GM_receive(void *, GM_stream *, unsigned int);
int GM_numpieces(GM_stream *);
int GM_piecelen(GM_stream *, int);
int GM_extract(unsigned int maxbytes);
int GM_debug_level(int);

/*---------------------------------------------------------------------------*/
extern int GM_nodeid;
extern int GM_numnodes;

/*---------------------------------------------------------------------------*/
#endif /* __GM_FM_H */
