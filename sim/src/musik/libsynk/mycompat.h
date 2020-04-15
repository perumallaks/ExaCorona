/*---------------------------------------------------------------------------*/
/* Author(s): Kalyan S. Perumalla */
/*---------------------------------------------------------------------------*/
#ifndef __MYCOMPAT_H
#define __MYCOMPAT_H

//XXX Forget about heterogeneous platforms - April 2020
#define ntohl(_) _
#define htonl(_) _
#define ntohs(_) _
#define htons(_) _

/*---------------------------------------------------------------------------*/
#if defined(_WIN32) || defined(_WIN32_WCE)
    #define PLATFORM_WIN 1
#endif

#ifdef _WIN32_WCE
    #define PLATFORM_WINCE 1
#endif

/*---------------------------------------------------------------------------*/
#if 1 /*XT3, Blue Gene, etc.*/
# if 0
    #define time(_pt)     (*(_pt)=0)
    /*        #define sleep(_s)     Sleep((_s)*1000)*/
    /*        #define strdup(_s)    strcpy(((char*)malloc(strlen(_s)+1)),_s)*/
    #define perror(_s)    ((void)0)
    #define system(_s)    ((int)-1)
    #define ntohl(_x) (long)(_x)
    #define htonl(_x) (long)(_x)
    #define ntohs(_x) (short)(_x)
    #define htons(_x) (short)(_x)
    #define Sleep(_x) -1
# endif
    /*        char *getenv( const char *s );*/
    /*        int putenv( char *s ); */
    /*#define SHM_AVAILABLE 0
    #define TCP_AVAILABLE 0*/
  #if MPI_AVAILABLE /*Use MP_Wtime?*/
    #include <mpi.h>
    typedef double TIMER_TYPE;
    #define TIMER_NOW(_t) (_t=MPI_Wtime())
    #define TIMER_SECONDS(_t) ((double)(_t))
  #else
    #include <sys/time.h>
    typedef struct timeval TIMER_TYPE;
    #define TIMER_NOW(_t) gettimeofday(&_t,NULL)
    #define TIMER_SECONDS(_t) ((double)(_t).tv_sec + (_t).tv_usec*1e-6)
  #endif
    typedef char BOOLEAN;
#elif PLATFORM_WIN
    #if !PLATFORM_WINCE
        #include <time.h>
        #define sleep(_s) \
            do{clock_t c=clock()+(_s)*1000;while(c>clock()){}}while(0)
    #else
        #include <winbase.h>
        #define time(_pt)     (*(_pt)=0)
        #define sleep(_s)     Sleep((_s)*1000)
        #define strdup(_s)    strcpy(((char*)malloc(strlen(_s)+1)),_s)
        #define perror(_s)    ((void)0)
        #define system(_s)    ((int)-1)
        char *getenv( const char *s );
        int putenv( char *s );
    #endif

    #include <process.h>
    #define getpid() _getpid()

    #include <winsock.h>
	#if PLATFORM_WINCE
        typedef time_t TIMER_TYPE;
        #define TIMER_NOW(_t) time(&_t)
        #define TIMER_SECONDS(_t) (_t)
	#else
        typedef struct { LARGE_INTEGER cntr, freq; } TIMER_TYPE;
        #define TIMER_NOW(_t) if(1) { \
		    QueryPerformanceFrequency(&((_t).freq)); \
			QueryPerformanceCounter(&((_t).cntr)); } else
        #define TIMER_SECONDS(_t) ((_t).cntr.QuadPart*1.0/(_t).freq.QuadPart)
	#endif
    #define SIGPIPE SIGSEGV
    #define SIGHUP SIGSEGV
    #define SHM_AVAILABLE 0
    #ifndef TCP_AVAILABLE
        #define TCP_AVAILABLE 1
    #endif
#else
    #include <netinet/in.h>
    #include <sys/time.h>
    #include <unistd.h>
    typedef struct timeval TIMER_TYPE;
    #define TIMER_NOW(_t) gettimeofday(&_t,NULL)
    #define TIMER_SECONDS(_t) ((double)(_t).tv_sec + (_t).tv_usec*1e-6)
    typedef char BOOLEAN;
    #ifndef SHM_AVAILABLE
        #define SHM_AVAILABLE 1
    #endif
    #ifndef TCP_AVAILABLE
        #define TCP_AVAILABLE 1
    #endif
#endif

/*---------------------------------------------------------------------------*/
#if !NODEBUG
  #if PLATFORM_WINCE
    /*Application should define the callback to handle the failed assertion*/
    void assert_callback( const TCHAR *fname, int line, const TCHAR *condition);
    #define MYASSERT( _cond, _act ) \
       do{if( !(_cond) ) { printf _act; printf("\n"); \
          assert_callback( TEXT(__FILE__), __LINE__, TEXT(#_cond) );}}while(0)
    #define assert( _cond ) MYASSERT( _cond, ("Failed assertion") )
  #else
    #include <assert.h>
    #define MYASSERT( _cond, _act ) \
       do{if( !(_cond) ) { printf _act; printf("\n"); assert( _cond );}}while(0)
  #endif
#else /*!NODEBUG*/
    #define MYASSERT( _cond, _act ) ((void)0)
#endif

extern BOOLEAN _printlscfg;
extern long _printlsdbgmaxfmid;
#define LSCFGLD(_cfgstr,_ldval,_notes) if(_printlscfg){ \
    printf("CONFIG-PARAMETER: %s %ld #!Musik: \"%s\"\n",_cfgstr,_ldval,_notes);\
  }else
#define LSCFGLF(_cfgstr,_lfval,_notes) if(_printlscfg){ \
    printf("CONFIG-PARAMETER: %s %lf #!Musik: \"%s\"\n",_cfgstr,_lfval,_notes);\
  }else
#define LSCFGST(_cfgstr,_stval,_notes) if(_printlscfg){ \
    printf("CONFIG-PARAMETER: %s %s #!Musik: \"%s\"\n",_cfgstr,_stval,_notes);\
  }else

/*---------------------------------------------------------------------------*/
int redirect_stdout( const char *filename );
long get_memusage( void );

/*---------------------------------------------------------------------------*/
#endif /* __MYCOMPAT_H */
