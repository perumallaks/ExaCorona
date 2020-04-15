/*---------------------------------------------------------------------------*/
/* Portable TCP FM-like implementation.                                      */
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "mycompat.h"

/*---------------------------------------------------------------------------*/
#if !TCP_AVAILABLE /*XT3, Blue Gene, etc*/
    #define DELFILE_CMD "rm"
    #define FILESEPARATOR "\\"
    #define MAXHOSTNAMELEN 1000
    #define MAXPATHLEN 1000
    #define SSIZE_MAX 32768
    #define NO_SOCKET 0
    #define SOCK_CMP(a,b) (0)
    #define AF_INET 0
    #define SOCK_STREAM 0
    #define SOL_SOCKET 0
    #define SO_RCVBUF 0
    #define INADDR_ANY 0
    #define setsockopt(_x,_y,_z,_a,_b) (int)-1
    #define getsockopt(_x,_y,_z,_a,_b) (int)-1
    #define inet_addr(_x) 0
    struct hostent {char **h_addr_list; char *h_addr; int h_length;};
    static struct hostent *gethostbyname(const char *_x) { return 0; } /*XXX*/
    struct iovec { char *iov_base; int iov_len; };
    struct linger { int l_onoff, l_linger; };
    #define bind(_x,_y,_z) (int)-1
    #define connect(_x,_y,_z) (int)-1
    #define accept(_x,_y,_z) (int)-1
    #define select(_x,_y,_z, _a, _b) (int)-1
    #define listen(_x,_y) (int)-1
    #define socket(_x,_y,_z) (int)-1
    #define SOCKET_READ(s,b,l) (int)-1
    #define SOCKET_WRITE(s,b,l) (int)-1
    #define SOCKET_CLOSE(s) (int)-1
    struct _xyz {long s_addr;};
    struct sockaddr_in {short sin_port, sin_family; struct _xyz sin_addr;};
    typedef int SOCKET;
    static int writen( SOCKET fd, char *buf, int n ); /*Forward declaration*/
    static int SOCKET_WRITEV(SOCKET s,const struct iovec *iov, int iovcount)
    {
        return 0;
    }
    static void SOCKET_INIT(void)
    {
    }
    static void SOCKET_CLEANUP(void)
    {
    }
    typedef unsigned short u_short;/*XXX*/

    #define FMFD_SET(a,b) ((void)0)
    #define FMFD_ISSET(a,b) 0
    #define FMFD_ZERO(a) ((void)0)
    typedef int FMFDSet;

#elif PLATFORM_WIN
    #include <winsock.h>
    #define DELFILE_CMD "del"
    #define FILESEPARATOR "\\"
    #define MAXHOSTNAMELEN 1000
    #define MAXPATHLEN 1000
    #define SSIZE_MAX 32768
    #define NO_SOCKET INVALID_SOCKET
    #define SOCK_CMP(a,b) (0)
    struct iovec { char *iov_base; int iov_len; };
    #define SOCKET_READ(s,b,l) recv(s,b,l,0)
    #define SOCKET_WRITE(s,b,l) send(s,b,l,0)
    #define SOCKET_CLOSE(s) closesocket(s)
    static int writen( SOCKET fd, char *buf, int n ); /*Forward declaration*/
    static int SOCKET_WRITEV(SOCKET s,const struct iovec *iov, int iovcount)
    {
	int i=0, totwritten=0;
	for(i=0; i<iovcount; i++)
	{
	    int nw = writen(s, iov[i].iov_base, iov[i].iov_len);
	    if(nw<0) break;
	    totwritten += nw;
	}
        return totwritten;
    }
    static void SOCKET_INIT(void)
    {
        WORD vreq = MAKEWORD(2,2); WSADATA wd;
	int rcode = WSAStartup(vreq,&wd);
	int ver_min_high = 1, ver_min_low = 1;
	MYASSERT( rcode == 0, ("%d",rcode) );
	MYASSERT( HIBYTE(wd.wVersion)>ver_min_high ||
	          (HIBYTE(wd.wVersion)==ver_min_high &&
	           LOBYTE(wd.wVersion)>=ver_min_low),
	          ("%d.%d",wd.wVersion,wd.wVersion) );
    }
    static void SOCKET_CLEANUP(void)
    {
        int rcode = WSACleanup();
	MYASSERT( rcode == 0, ("%d",rcode) );
    }

    #define FMFD_SET(a,b) FD_SET(a,b)
    #define FMFD_ISSET(a,b) FD_ISSET(a,b)
    #define FMFD_ZERO(a) FD_ZERO(a)
    typedef fd_set FMFDSet;
#else
    #include <limits.h>
    #include <sys/param.h>
    #include <sys/types.h>
    #include <sys/uio.h>
    #include <sys/time.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    typedef int SOCKET;
    #define NO_SOCKET -1
    #define DELFILE_CMD "rm"
    #define FILESEPARATOR "/"
    #define SOCK_CMP(a,b) ((a)-(b))
    #define SOCKET_READ(s,b,l) read(s,b,l)
    #define SOCKET_WRITE(s,b,l) write(s,b,l)
    #define SOCKET_CLOSE(s) close(s)
    #define SOCKET_WRITEV(s,v,c) writev(s,v,c)
    #define SOCKET_INIT() (void)0
    #define SOCKET_CLEANUP() (void)0

    #define FMFD_SET(a,b) FD_SET(a,b)
    #define FMFD_ISSET(a,b) FD_ISSET(a,b)
    #define FMFD_ZERO(a) FD_ZERO(a)
    typedef fd_set FMFDSet;
#endif

#include "fmtcp.h"

/*---------------------------------------------------------------------------*/
static int tcpfmdbg = 0;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int src_id; /*Original sender's caller-specific ID*/
    int dest_id; /*Original receiver's caller-specific ID*/
    int src_pe;
    int dest_pe;
    int handler;
    int npieces; /*Including this header piece; hence always >=1*/
    int piecelen[TCPMAXPIECES]; /*Byte length of each piece*/
    int totbytes;/*#bytes in all pieces combined; hence always >= sizeof(hdr)*/
} TCPMsgHeaderPiece;

/*---------------------------------------------------------------------------*/
typedef struct
{
    struct iovec pieces[TCPMAXPIECES];
    TCPMsgHeaderPiece hdr;
} TCPSendMsg;

/*---------------------------------------------------------------------------*/
typedef struct
{
    TCPMsgHeaderPiece hdr;
    int npieces_recd;
    int nbytes_recd;
} TCPRecvMsg;

/*---------------------------------------------------------------------------*/
typedef struct
{
    SOCKET sockfd;
    char hostname[MAXHOSTNAMELEN+1];
    struct sockaddr_in addr;
    int port;
} TCPPeer;

/*---------------------------------------------------------------------------*/
typedef struct
{
    int nodeid;
    int numnodes;
    TCPPeer peer[TCPMAXPE];
    TCPSendMsg send_msg;
    TCPRecvMsg recv_msg;
} TCPState;

/*---------------------------------------------------------------------------*/
int TCP_nodeid;
int TCP_numnodes;
static TCPCallback *fmcb = 0;
static TCPState tcps, *tcp = &tcps;
static char port_fname[MAXPATHLEN];

/*------------------------------------------------------------------------*/
unsigned long dot_to_ulong( const char *s )
{
    unsigned long n;
    if( !s || !strlen(s) ) return 0UL;
    n = inet_addr( s );
    if( n == (unsigned long)(-1) ) return 0UL;
    return ntohl(n);
}
static struct hostent *mygethostbyname( const char *hname )
{
    int i = 0, dotted = 1;
    for(i=0; i<strlen(hname); i++)
        if(!(isdigit(hname[i]) || hname[i]=='.')) { dotted = 0; break; }
    if( !dotted ) { return gethostbyname(hname); }
    else
    {
        static struct hostent hent;
	static char *addrlist[2];
	static unsigned long addr;
	memset( &hent, 0, sizeof(hent) );
	addr = dot_to_ulong(hname);
	addr = htonl(addr);
	addrlist[0] = (char *)&addr;
	addrlist[1] = 0;
	hent.h_addr_list = addrlist;
	hent.h_length = sizeof(addr);
#if !NODEBUG
if(tcpfmdbg>=3){printf("Detected dotted-IP \"%s\"\n", hname); fflush(stdout);}
#endif
	return &hent;
    }
}
#define gethostbyname mygethostbyname
/*------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
static void config( char *hostnames[] )
{
    int i = 0;
    char *estr = 0;

    estr = getenv("FMTCP_DEBUG");
    tcpfmdbg = estr ? atoi(estr) : 1;
#if !NODEBUG
if(tcpfmdbg>=1){printf("FMTCP_DEBUG=%d\n",tcpfmdbg);fflush(stdout);}
#endif

    for( i = 0; i < tcp->numnodes; i++ )
    {
        strcpy( tcp->peer[i].hostname, hostnames[i] );
#if !NODEBUG
if(tcpfmdbg>=2){printf("TCPFM:host[%d]=\"%s\"\n",i,hostnames[i]);}
#endif
    }
}

/*---------------------------------------------------------------------------*/
static int readn( SOCKET fd, char *buf, int n )
{
    long nleft, nread;

    nleft = n;
    while( nleft > 0 )
    {
        nread = SOCKET_READ( fd, buf, nleft );
        if( nread < 0 )
        {
	    break;
        }
        else if( nread == 0 )
        {
	    break;
        }
        else
        {
            nleft -= nread;
            buf += nread;
        }
    }

    return (n - nleft);
}

/*---------------------------------------------------------------------------*/
static int writen( SOCKET fd, char *buf, int n )
{
    long nleft, nwritten;

    nleft = n;
    while( nleft > 0 )
    {
        nwritten = SOCKET_WRITE( fd, buf, nleft );
        if( nwritten < 0 )
        {
	    break;
        }
        else if( nwritten == 0 )
        {
	    break;
        }
        else
        {
            nleft -= nwritten;
            buf += nwritten;
        }
    }

    return (n - nleft);
}

/*---------------------------------------------------------------------------*/
static void make_connections( void )
{
    int i, j;
    TCPPeer *self = &tcp->peer[tcp->nodeid];
    int master_portnum = -1;

    for( i = 0; i < TCPMAXPE; i++ )
    {
	TCPPeer *peer = &tcp->peer[i];
        peer->sockfd = NO_SOCKET;
        peer->port = -1;
    }

    {
        char *home_dir = getenv("HOME");
	char *session_name = getenv("FMTCP_SESSIONNAME");
	char *master_portstr = getenv("FMTCP_MASTERPORT");
	if(!session_name) session_name = getenv("SESSIONNAME"); /*Back-compat*/
	if(!home_dir) home_dir = ".";
	if(!session_name) session_name = "fmtcp-portfile";
	if(!master_portstr) master_portstr = "-1";
        sprintf( port_fname, "%s%s%s", home_dir, FILESEPARATOR, session_name );
	master_portnum = atoi(master_portstr);
    }

    if( tcp->numnodes > 1 )
    {
	SOCKET sock;
	int port = -1, port_1st = 23456, port_last = 1000000;
	int bound = 0, listened = 0;
        struct sockaddr_in *psin = &self->addr;
        struct hostent *hent = gethostbyname(self->hostname);
        MYASSERT( hent, ("hent(%s)",self->hostname); perror("hostent") );
        sock = socket( AF_INET, SOCK_STREAM, 0 );
	MYASSERT( sock != NO_SOCKET, ("socket %d",sock); perror("socket") );

	if( tcp->nodeid == 0 && master_portnum > 0 )
	{
	    port_1st = port_last = master_portnum;
	}
        else if( getenv("FMTCP_FIRSTTRYPORT") )
        {
            port_1st = atoi(getenv("FMTCP_FIRSTTRYPORT"));
        }
#if !NODEBUG
if(tcpfmdbg>=2){printf("FMTCP %d : 1st port to try bind= %d\n",tcp->nodeid,port_1st);fflush(stdout);}
#endif

	for( port = port_1st, bound = 0; port <= port_last; port++)
	{
            memset(psin, 0, sizeof(*psin)); psin->sin_port=htons((u_short)port);
            psin->sin_family = AF_INET; psin->sin_addr.s_addr=htonl(INADDR_ANY);
            memcpy(&psin->sin_addr, hent->h_addr, hent->h_length);
            bound = (bind(sock, (struct sockaddr *)psin, sizeof(*psin) ) == 0);
	    if( bound ) break;
	}
	MYASSERT( bound, ("bind failed FMTCPID %d",tcp->nodeid); perror("bind") );
#if !NODEBUG
if(tcpfmdbg>=1){printf("Node %d %s bound to port %d\n", tcp->nodeid, self->hostname, port);fflush(stdout);}
#endif
        listened = listen(sock, 10);
	MYASSERT( listened >= 0, ("listen %d",listened); perror("listen") );

	self->port = port;
	self->sockfd = sock;

        MYASSERT( self->port >= 0, ("%d", self->port) );
        MYASSERT( self->sockfd != NO_SOCKET, ("%d", self->sockfd) );
    }

    if( tcp->nodeid == 0 )
    {
        /*Advertise master port#*/
	master_portnum = self->port;
	if( !getenv("FMTCP_NOPORTFILE") )
	{
#if !NODEBUG
if(tcpfmdbg>=2){printf("Writing port# to file %s ...", port_fname);fflush(stdout);}
#endif
	    /*Write port# to a file */
	    {
		FILE *fp = fopen( port_fname, "w" );
		MYASSERT( fp, ("TCPFM: Can't write port# to %s\n", port_fname));
		fprintf( fp, "%d\n", master_portnum ); fflush( fp );
		fclose( fp ); fp = 0;
	    }
#if !NODEBUG
if(tcpfmdbg>=2){printf("Done.\n");fflush(stdout);}
#endif
	}
    }
    else
    {
        /*Obtain master port#*/
	if( master_portnum <= 0 )
	{
	     while( master_portnum <= 0 )
	     {
		 FILE *fp = 0;

#if !NODEBUG
if(tcpfmdbg>=2){printf("Reading port# from file %s ...", port_fname);fflush(stdout);}
#endif

		 fp = fopen( port_fname, "r" );
		 if( !fp || fscanf( fp, "%d", &master_portnum ) != 1 )
		 {
#if !NODEBUG
if(tcpfmdbg>=2){printf("\nRetrying..."); fflush(stdout);}
#endif

		     sleep( 2/*sec*/ );
		 }
		 if(fp) fclose( fp );
	     }
#if !NODEBUG
if(tcpfmdbg>=2){printf("Done.\nMaster port# is %d\n", master_portnum);}
#endif
	}
	MYASSERT( master_portnum > 0, ("FMTCP master port required") );
        tcp->peer[0].port = master_portnum;
    }

    for( i = 0; i < tcp->nodeid; i++ )
    {
	SOCKET sock;
	TCPPeer *peer = &tcp->peer[i];
	int atry = 0, maxtries = 1000;
	int port = peer->port, connected = 0;
	const char *hname = peer->hostname;

	MYASSERT( port >= 0, ("Port[%d] = %d",i,port) );

        for( atry = 0; atry < maxtries; atry++ )
        {
            struct sockaddr_in *psin = &peer->addr;
            struct hostent *hent = gethostbyname(hname);
            MYASSERT( hent, ("hent[%d]%s",i,hname); perror("hostent") );
            memset(psin, 0, sizeof(*psin));
            memcpy(&psin->sin_addr, hent->h_addr, hent->h_length);
            psin->sin_family = AF_INET; psin->sin_port = htons((u_short)port);
            sock = socket(AF_INET, SOCK_STREAM, 0);
	    MYASSERT( sock!=NO_SOCKET, ("socket[%d]%s",i,hname);perror("socket"));
            connected=(connect(sock,(struct sockaddr *)psin,sizeof(*psin))==0);

	    if( connected ) break;

            perror("Retrying. connect()"); fflush(stderr);
	    SOCKET_CLOSE(sock); sock = NO_SOCKET;
	    sleep(1);
#if !NODEBUG
if(tcpfmdbg>=2){printf("Try %d\n",atry);fflush(stdout);}
#endif
        }

        MYASSERT( connected, ("Connection to %s:%d",hname,port) );

#if !NODEBUG
if(tcpfmdbg>=2){printf( "Node %d connected to node %d - %s:%d\n", tcp->nodeid, i, hname, port);fflush(stdout);}
#endif

	peer->sockfd = sock;

	{
            int nw = writen( sock, (char *)&tcp->nodeid, sizeof(tcp->nodeid) );
	    MYASSERT( nw == sizeof(tcp->nodeid), ("!") );
	}

	if( i == 0 )
	{
	    int nwritten = -1;
	    nwritten = writen( peer->sockfd,
	                       (char *)&self->port, sizeof(self->port) );
	    MYASSERT( nwritten == sizeof(self->port), ("!") );
	    for( j = 0; j < tcp->numnodes; j++ )
	    {
		int port = -1, nread = 0;
	        nread = readn(peer->sockfd, (char *)&port, sizeof(port));
		MYASSERT( nread == sizeof(port) && port>=0, ("%d %d",nread,port));
		if( j == tcp->nodeid ) MYASSERT( port == self->port, ("!") );
		else tcp->peer[j].port = port;
	    }
	}
    }

    for( i = tcp->nodeid+1; i < tcp->numnodes; i++ )
    {
	int k = -1;
	SOCKET sock;
        struct sockaddr_in clin; int clilen = sizeof(clin);

        sock = accept( self->sockfd, (struct sockaddr *)&clin, &clilen );
	MYASSERT( sock != NO_SOCKET, ("accept[%d]",i); perror("accept") );

#if !NODEBUG
if(tcpfmdbg>=2){printf("Node %d recd connection #%d\n", tcp->nodeid, i-tcp->nodeid-1);fflush(stdout);}
#endif

	{
            int nread = readn( sock, (char *)&k, sizeof(k) );
	    MYASSERT( nread == sizeof(k), ("!") );
	}

#if !NODEBUG
if(tcpfmdbg>=3){printf("Node %d recd ID %d\n", tcp->nodeid, k);fflush(stdout);}
#endif

	MYASSERT( tcp->nodeid < k && k < tcp->numnodes,
	        ("%d must lie in [%d..%d]", k, tcp->nodeid+1, tcp->numnodes-1));
	{
	    TCPPeer *peer = &tcp->peer[k];
	    MYASSERT( peer->sockfd == NO_SOCKET,
	            ("%d already connected? sockfd=%d", k, peer->sockfd) );
	    peer->sockfd = sock;
	    peer->addr = clin;
	}
    }

    if( tcp->numnodes > 1 && tcp->nodeid == 0 )
    {
        for( i = 0; i < tcp->numnodes; i++ )
	{
	    if( i != tcp->nodeid )
	    {
	        SOCKET fd = tcp->peer[i].sockfd;
	        int port = -1, nread = -1;
		nread = readn( fd, (char *)&port, sizeof(port) );
		MYASSERT( nread == sizeof(port), ("!") );
		MYASSERT( port >= 0, ("%d",port) );
		tcp->peer[i].port = port;
#if !NODEBUG
if(tcpfmdbg>=3){printf("Node 0 recd port %d from node %d\n", port, i);fflush(stdout);}
#endif
	    }
	}

        for( i = 0; i < tcp->numnodes; i++ )
	{
	    if( i != tcp->nodeid )
	    {
	        SOCKET fd = tcp->peer[i].sockfd;
	        for( j = 0; j < tcp->numnodes; j++ )
	        {
		    int port = tcp->peer[j].port, nwritten = -1;
		    MYASSERT( port >= 0, ("!") );
		    nwritten = writen( fd, (char *)&port, sizeof(port) );
		    MYASSERT( nwritten==sizeof(port),
		            ("%d %lu",nwritten,sizeof(port)) );
	        }
	    }
	}
    }

    SOCKET_CLOSE( self->sockfd ); self->sockfd = NO_SOCKET;

    for( i = 0; i < tcp->numnodes; i++ )
    {
        if( i != tcp->nodeid )
	{
	    TCPPeer *peer = &tcp->peer[i];
	    SOCKET fd = peer->sockfd;
	    int f = 1, retval = -1;
	    int rsz = 16000000; /*CUSTOMIZE*/
	    int ssz = 16000000; /*CUSTOMIZE*/
	    int defrsz = 0, newrsz = rsz, defssz = 0, newssz = ssz;
	    int optlen=sizeof(int);
	    struct linger lr;

	    retval = getsockopt( fd, SOL_SOCKET, SO_RCVBUF,
	                         (char *)&defrsz, &optlen );
	    MYASSERT( retval >= 0, ("!"); perror("getsockopt") );
	    newrsz = defrsz;
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Default SO_RCVBUF %d bytes\n",tcp->nodeid,defrsz);}
#endif
	    for( ; rsz>0 && rsz>defrsz; rsz /= 1.5 )
	    {
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Increasing SO_RCVBUF to %d bytes\n",tcp->nodeid,rsz);}
#endif
	        retval = setsockopt( fd, SOL_SOCKET, SO_RCVBUF,
	                             (const char *)&rsz, sizeof(rsz) );
	        retval = getsockopt( fd, SOL_SOCKET, SO_RCVBUF,
	                             (char *)&newrsz, &optlen );
	        MYASSERT( retval >= 0, ("!"); perror("getsockopt") );
	        if( newrsz >= rsz ) break;
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Couldn't increase SOCKET RCVBUF from %d to %d. Now set to %d.\n", tcp->nodeid, defrsz, rsz, newrsz);}
#endif
	    }
            MYASSERT( rsz > 0, ("%d", rsz) );
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Using SO_RCVBUF %d bytes\n",tcp->nodeid,newrsz);}
#endif

	    retval = getsockopt( fd, SOL_SOCKET, SO_SNDBUF,
	                         (char *)&defssz, &optlen );
	    MYASSERT( retval >= 0, ("!"); perror("getsockopt") );
	    newssz = defssz;
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Default SO_SNDBUF %d bytes\n",tcp->nodeid,defssz);}
#endif
            for( ; ssz>0 && ssz>defssz; ssz /= 1.5 )
            {
	        retval = setsockopt( fd, SOL_SOCKET, SO_SNDBUF,
	                             (const char *)&ssz, sizeof(ssz) );
	        retval = getsockopt( fd, SOL_SOCKET, SO_SNDBUF,
	                             (char *)&newssz, &optlen );
	        MYASSERT( retval >= 0, ("!"); perror("getsockopt") );
	        if( newssz >= ssz ) break;
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Couldn't increase SOCKET SNDBUF from %d to %d. Now set to %d.\n", tcp->nodeid, defssz, ssz, newssz);}
#endif
            }
            MYASSERT( ssz > 0, ("%d", ssz) );
#if !NODEBUG
if(tcpfmdbg>=4){printf("Node %d: Using SO_SNDBUF %d bytes\n",tcp->nodeid,newssz);}
#endif

	    lr.l_onoff = 1; lr.l_linger = 5;
	    retval = setsockopt( fd, SOL_SOCKET, SO_LINGER,
	                         (const char *)&lr, sizeof(lr) );
	    MYASSERT( retval >= 0, ("!"); perror("setsockopt") );

	    retval = setsockopt( fd, IPPROTO_TCP, TCP_NODELAY,
	                         (const char *)&f, sizeof(f) );
	    MYASSERT( retval >= 0, ("!"); perror("setsockopt") );
	}
    }
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
void TCP_initialize( char *hostnames[], int nodeid, int numnodes,
    TCPCallback *cb )
{
#if !NODEBUG
if(tcpfmdbg>=1){printf("TCP_initialize() started.\n");fflush(stdout);}
#endif
    MYASSERT( cb, ("Message callback required") );
    fmcb = cb;

    SOCKET_INIT();

    TCP_nodeid = tcp->nodeid = nodeid;
    TCP_numnodes = tcp->numnodes = numnodes;

#if !NODEBUG
if(tcpfmdbg>=1){printf( "TCP_nodeid=%d, TCP_numnodes=%d\n", TCP_nodeid, TCP_numnodes);fflush(stdout);}
#endif

    MYASSERT( 0 < tcp->numnodes && tcp->numnodes <= TCPMAXPE,
            ("#nodes %d must be in [1..%d]", tcp->numnodes, TCPMAXPE) );

    MYASSERT( 0 <= tcp->nodeid && tcp->nodeid < tcp->numnodes,
            ("Node %d must be in [0..%d]", tcp->nodeid, tcp->numnodes-1) );

    config( hostnames );

    if( tcp->numnodes > 1 )
    {
        make_connections();
    }
#if !NODEBUG
if(tcpfmdbg>=1){printf("TCP_initialize() done.\n");fflush(stdout);}
#endif
}

/*---------------------------------------------------------------------------*/
void TCP_finalize( void )
{
    int i = 0;
    for( i = 0; i < tcp->numnodes; i++ )
    {
        SOCKET *pfd = &tcp->peer[i].sockfd;
	if( *pfd != NO_SOCKET )
	{
	    SOCKET_CLOSE( *pfd );
	}
	*pfd = NO_SOCKET;
    }
    SOCKET_CLEANUP();
}

/*---------------------------------------------------------------------------*/
TCP_stream *TCP_begin_message( int recipient, int length, int handler,
    int src_id, int dest_id )
{
    TCPSendMsg *msg = &tcp->send_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;
    struct iovec *iov = &msg->pieces[0];

    MYASSERT( 0 <= recipient && recipient < tcp->numnodes,
            ("Bad TCP recipient ID %d must be in [0..%d]",
	     recipient, tcp->numnodes-1) );
    MYASSERT( 0 <= length && length <= SSIZE_MAX,
            ("Msg size %d must be <= %d", length, SSIZE_MAX));

    hdr->src_id = src_id;
    hdr->dest_id = dest_id;
    hdr->src_pe = tcp->nodeid;
    hdr->dest_pe = recipient;
    hdr->handler = handler;
    hdr->npieces = 1;
    hdr->piecelen[0] = sizeof(*hdr);
    hdr->totbytes = sizeof(*hdr);

    iov->iov_base = (char *)hdr;
    iov->iov_len = hdr->totbytes;

    return (TCP_stream *)msg;
}

/*---------------------------------------------------------------------------*/
void TCP_send_piece( TCP_stream *sendstream, void *buffer, int length )
{
    TCPSendMsg *msg = &tcp->send_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;

    MYASSERT( sendstream == (TCP_stream *)msg, ("!") );
    MYASSERT( buffer && 0 <= length && length <= (SSIZE_MAX-hdr->totbytes),
            ("buffer=%p, length = %d SSIZE_MAX=%d", buffer, length, SSIZE_MAX));
    MYASSERT( hdr->npieces < TCPMAXPIECES,
            ("TCP msg pieces can't exceed compiled %d pieces", TCPMAXPIECES) );
    MYASSERT( length <= TCPMAXPIECELEN,
            ("Piece len %d can't exceed compiled %d", length, TCPMAXPIECELEN));

    {
    int pn = hdr->npieces++;
    struct iovec *iov = &msg->pieces[pn];
    iov->iov_base = buffer;
    iov->iov_len = length;
    hdr->piecelen[pn] = length;
    hdr->totbytes += length;
    }
}

/*---------------------------------------------------------------------------*/
void TCP_end_message( TCP_stream *sendstream )
{
    TCPSendMsg *msg = &tcp->send_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;
    int to = hdr->dest_pe, nwritten = 0;
    SOCKET sockfd = tcp->peer[to].sockfd;

    MYASSERT( sendstream == (TCP_stream *)msg, ("!") );

    if( sockfd != NO_SOCKET )
    {
        nwritten = SOCKET_WRITEV( sockfd, msg->pieces, hdr->npieces );
        MYASSERT( nwritten == hdr->totbytes,
                ("nwritten %d must equal totbytes %d",nwritten,hdr->totbytes) );
    }
}

/*---------------------------------------------------------------------------*/
int TCP_receive( void *buffer, TCP_stream *receivestream, unsigned int length )
{
    TCPRecvMsg *msg = &tcp->recv_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;
    SOCKET *pfd = &tcp->peer[hdr->src_pe].sockfd;
    int nread = 0, pn = 0;

    MYASSERT( receivestream == msg, ("!") );

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

    if( *pfd != NO_SOCKET )
    {
        nread = readn( *pfd, buffer, length );
    }

    if( nread <= 0 )
    {
        SOCKET_CLOSE( *pfd );
	*pfd = NO_SOCKET;
    }

    msg->nbytes_recd += length;

    return nread;
}

/*---------------------------------------------------------------------------*/
static int recv_one_msg( int pe )
{
    int i = 0, nbytes = 0, nrecd = 0;
    SOCKET fd = tcp->peer[pe].sockfd;
    TCPRecvMsg *msg = &tcp->recv_msg;
    TCPMsgHeaderPiece temphdr, *hdr = &msg->hdr;

    MYASSERT( fd != NO_SOCKET, ("Ensure valid fd[%d]=%d", pe, fd) );

    msg->nbytes_recd = 0; msg->npieces_recd = 0;

    hdr->src_id = -1; hdr->dest_id = -1;
    hdr->src_pe = pe; hdr->dest_pe = tcp->nodeid;
    hdr->handler = 0; hdr->npieces = 0; hdr->totbytes = 0;

    nrecd = TCP_receive( &temphdr, msg, sizeof(temphdr) );

    if( nrecd <= 0 )
    {
	/*Do nothing*/
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

    fmcb( hdr->handler, msg, hdr->src_pe, hdr->src_id, hdr->dest_id );

    nrecd = 0;
    for( i = msg->npieces_recd; i < hdr->npieces; i++ )
    {
	char buf[TCPMAXPIECELEN];
	int len = hdr->piecelen[i];
	MYASSERT( len <= sizeof(buf), ("%d %lu",len,sizeof(buf)) );
        nrecd = TCP_receive( buf, msg, len );
	if( nrecd <= 0 ) break;
    }

    MYASSERT( nrecd <= 0 || msg->nbytes_recd == hdr->totbytes,
            ("%d, %d", msg->nbytes_recd, hdr->totbytes) );

    nbytes = msg->nbytes_recd;
    }

    return nbytes;
}

/*---------------------------------------------------------------------------*/
static int poll_all_once( unsigned int maxbytes )
{
    struct timeval tv;
    int i = 0, maxfd = 0, nready = 0, nbytes = 0;
    FMFDSet rfds;

    FMFD_ZERO( &rfds );
    for( i = 0; i < tcp->numnodes; i++ )
    {
        if( i != tcp->nodeid )
	{
	    SOCKET fd = tcp->peer[i].sockfd;
	    if( fd != NO_SOCKET )
	    {
	        FMFD_SET( fd, &rfds );
		if( SOCK_CMP(fd, maxfd) > 0 ) maxfd = fd;
	    }
	}
    }

    tv.tv_sec = tv.tv_usec = 0;
    nready = select( maxfd+1, &rfds, 0, 0, &tv );

    if( nready < 0 )
    {
        if(0){MYASSERT( 0, ("select() failed"); perror("") );}
    }
    else if( nready == 0 )
    {
    }
    else
    {
        for( i = 0; i < tcp->numnodes-1 && nbytes < maxbytes; i++ )
        {
            static int pe = 0;
            if( pe == tcp->nodeid ) pe++;
	    pe %= tcp->numnodes;
	    {
	        SOCKET fd = tcp->peer[pe].sockfd;
	        if( fd != NO_SOCKET && FMFD_ISSET( fd, &rfds ) )
		{
		    nbytes += recv_one_msg( pe );
		}
	    }
	    pe++; pe %= tcp->numnodes;
        }
    }

    return nbytes;
}

/*---------------------------------------------------------------------------*/
int TCP_extract( unsigned int maxbytes )
{
    int nbytes = 0;
    static int removed_portfile = 0;
    if( TCP_nodeid == 0 && !removed_portfile )
    {
	char cmd[MAXPATHLEN+100];
	int retval = 0;
	sprintf(cmd,"%s %s", DELFILE_CMD, port_fname);
	retval = system(cmd);
        MYASSERT( retval == 0, ("sytem() %d",retval) );
        removed_portfile = 1;
    }

    if( tcp->numnodes <= 1 ) return 0;

    while( nbytes < maxbytes )
    {
	int m = poll_all_once( maxbytes-nbytes );
	if( m <= 0 ) break;
        nbytes += m;
    }

    return nbytes;
}

/*---------------------------------------------------------------------------*/
int TCP_numpieces( TCP_stream *tcp_stream )
{
    TCPRecvMsg *msg = &tcp->recv_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;
    MYASSERT( tcp_stream == msg, ("!") );
    return hdr->npieces-1;
}

/*---------------------------------------------------------------------------*/
int TCP_piecelen( TCP_stream *tcp_stream, int i )
{
    TCPRecvMsg *msg = &tcp->recv_msg;
    TCPMsgHeaderPiece *hdr = &msg->hdr;

    MYASSERT( tcp_stream == msg, ("!") );
    MYASSERT( 0<=i && i < hdr->npieces-1, ("Only #%d pieces", hdr->npieces-1) );

    return hdr->piecelen[i+1];
}

/*---------------------------------------------------------------------------*/
int TCP_debug_level( int level )
{
    int old = tcpfmdbg;
    tcpfmdbg = level;
    return old;
}

/*---------------------------------------------------------------------------*/
