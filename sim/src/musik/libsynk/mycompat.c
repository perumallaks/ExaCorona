/*----------------------------------------------------------------------------*/
/* Author(s): Kalyan S. Perumalla */
/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mycompat.h"

BOOLEAN _printlscfg = 0;
long _printlsdbgmaxfmid = 99999999;

/*----------------------------------------------------------------------------*/
#if PLATFORM_WINCE
#define MAXENVS 64
static char *enames[MAXENVS];
static char *evals[MAXENVS];
static int nenvs = 0;
static int find( const char *s )
{
    int i = 0;
    for( i = 0; i < nenvs; i++ ) if( !strcmp( enames[i], s ) ) return i;
    return -1;
}
char *getenv( const char *s )
{
    int i = 0;
    char *match = 0;
    if( (i=find(s)) >= 0 ) match=evals[i];
    return match;
}
int putenv( char *s )
{
    int status = 0, i = 0;
    char *name = s, *val = strchr(s,'=');
    if(!val) val = ""; else {*val=0; val++;}
    i = find( name );
    if( i >= 0 )
    {
        evals[i] = val;
    }
    else
    {
        if( nenvs < MAXENVS )
        {
            enames[nenvs] = name;
            evals[nenvs++] = val;
        }
        else
        {
            status = -1;
        }
    }
    return status;
}
void assert_callback( const TCHAR *fname, int linenum, const TCHAR *condition )
{
    HWND hWnd = NULL;
    TCHAR linestr[100]; _itow(linenum,linestr,10);
    MessageBox( hWnd, condition, TEXT("Failed assertion"),
           MB_OK | MB_ICONSTOP | MB_APPLMODAL | MB_SETFOREGROUND | MB_TOPMOST );
    MessageBox( hWnd, fname, TEXT("Assertion file"),
           MB_OK | MB_ICONSTOP | MB_APPLMODAL | MB_SETFOREGROUND | MB_TOPMOST );
    MessageBox( hWnd, linestr, TEXT("Assertion line"),
           MB_OK | MB_ICONSTOP | MB_APPLMODAL | MB_SETFOREGROUND | MB_TOPMOST );
    exit(1);
}

#endif

/*---------------------------------------------------------------------------*/
/* Utility functions.                                                        */
/* (a) A way to redirect stdout to a file, for easier debugging.             */
/* (b) To query for current memory usage level by the process.               */
/*---------------------------------------------------------------------------*/
#include <fcntl.h>
#include <sys/stat.h>
#if PLATFORM_WIN
    #include <io.h>
    /*#include <iostream>*/
    using namespace std;
    int redirect_stdout( const char *filename )
    {
        FILE *stream = 0;
        int fd = 0;

        if( !filename ) return 1;

        if( (fd = _open( filename,
                _O_WRONLY | _O_CREAT,
                _S_IREAD | _S_IWRITE )) < 0 )
        {
            fprintf(stdout, "Cannot _open( %s )\n", filename); fflush(stdout);
            return 1;
        }
        if( (stream = _fdopen( fd, "w" )) == NULL )
        {
            fprintf(stdout, "Cannot _fdopen( %d )\n", fd); fflush(stdout);
            _close( fd );
            return 1;
        }
        *stdout = *stream;
        fprintf(stdout, "Redirected stdout to %s\n", filename); fflush(stdout);
        return 0;
    }
    long get_memusage( void ) { return 0; } /*Not implemented yet*/
#else
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <sys/resource.h>
    int redirect_stdout( const char *filename )
    {
        if( !filename ) return 1;

        return freopen( filename, "w", stdout ) ? 0 : 1;
    }
    long get_memusage( void )
    {
        long membytes = 0;
        pid_t pid = getpid();
        char buf[4096], *s;
        FILE *fp = 0;
        sprintf(buf, "/proc/%d/stat", (int)pid);
        if( (fp = fopen(buf, "r")) ) /*Based on code shared by Riley*/
        {
            int i = 0;
            char *retval = 0;
            retval = fgets(buf, sizeof(buf), fp);
            fclose(fp);
            strtok(buf, " ");
            for(i = 1; i < 23; i++) s=strtok(0, " ");
            membytes = atol(s);
        }

        if( membytes <= 0 ) //Try getrusage
        {
            struct rusage ru;
            if( getrusage( RUSAGE_SELF, &ru ) == 0 )
            {
                membytes = ru.ru_maxrss * 1024; //On AIX, rss is in kilobytes
                                                //On Solaris, it is in pagesize
            }
        }

        #if 0 //Non-linux
        if( membytes <= 0 )
        {
            int fd = 0;
            struct prpsinfo prps;
            sprintf(buf, "/proc/%d", pid);
            if( (fd = open(buf, O_RDONLY, 0)) >= 0 &&
                ioctl(fd, PIOCPSINFO, &prps) == 0 )
            {
                membytes = (prps.pr_rssize * getpagesize());
            }
        }
        #endif //Non-linux
        return membytes;
    }
#endif

/*----------------------------------------------------------------------------*/
