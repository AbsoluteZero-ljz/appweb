/*
    fastProgram.c - Test FastCGI program

    Copyright (c) All Rights Reserved. See details at the end of the file.

    Usage:
        fastProgram [switches]
            -a                  Output the args (used for ISINDEX queries)
            -b bytes            Output content "bytes" long
            -e                  Output the environment
            -h lines            Output header "lines" long
            -l location         Output "location" header
            -n                  Non-parsed-header ouput
            -p                  Ouput the post data
            -q                  Ouput the query data
            -s status           Output "status" header
            default             Output args, env and query

        Alternatively, pass the arguments as an environment variable HTTP_SWITCHES="-a -e -q"
 */

/********************************** Includes **********************************/

#include "fcgiapp.h"

#define _CRT_SECURE_NO_WARNINGS 1
#ifndef _VSB_CONFIG_FILE
    #define _VSB_CONFIG_FILE "vsbConfig.h"
#endif
#if _WIN32 || WINCE
/* Work-around to allow the windows 7.* SDK to be used with VS 2014 */
#if _MSC_VER >= 1700
    #define SAL_SUPP_H
    #define SPECSTRING_SUPP_H
#endif
#endif

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if _WIN32 || WINCE
#include <fcntl.h>
#include <io.h>
#include <windows.h>

    #define access   _access
    #define close    _close
    #define fileno   _fileno
    #define fstat    _fstat
    #define getpid   _getpid
    #define open     _open
    #define putenv   _putenv
    #define read     _read
    #define stat     _stat
    #define umask    _umask
    #define unlink   _unlink
    #define write    _write
    #define strdup   _strdup
    #define lseek    _lseek
    #define getcwd   _getcwd
    #define chdir    _chdir
    #define strnset  _strnset
    #define chmod    _chmod

    #define mkdir(a,b)  _mkdir(a)
    #define rmdir(a)    _rmdir(a)
    typedef int ssize_t;
#else
#include <unistd.h>

#endif

/*********************************** Locals ***********************************/

#define MAX_ARGV 64

static char     *argvList[MAX_ARGV];
static int      getArgv(int *argc, char ***argv, int originalArgc, char **originalArgv);
static int      hasError;
static int      numPostKeys;
static int      numQueryKeys;
static int      originalArgc;
static char     **originalArgv;
static int      outputArgs, outputEnv, outputPost, outputQuery;
static int      outputLines, outputHeaderLines, responseStatus;
static char     *outputLocation;
static char     *postBuf;
static size_t   postBufLen;
static char     **postKeys;
static char     *queryBuf;
static size_t   queryLen;
static char     **queryKeys;
static char     *responseMsg;
static int      timeout;

static FCGX_ParamArray fast_envp;
static FCGX_Stream     *fast_in, *fast_out, *fast_err;

/***************************** Forward Declarations ***************************/

static void     error(char *fmt, ...);
static void     descape(char *src);
static char     hex2Char(char *s);
static int      getVars(char ***cgiKeys, char *buf, size_t len);
static int      getPostData(char **buf, size_t *len);
static int      getQueryString(char **buf, size_t *len);
static void     printEnv(char **env);
static void     printQuery();
static void     printPost(char *buf, size_t len);
static char     *safeGetenv(char *key);

/******************************************************************************/
/*
    Test program entry point
 */
int main(int argc, char **argv, char **envp)
{
    char            *cp, *method;
    int             l, i, err;

    err = 0;
    // sleep(30);

    outputArgs = outputQuery = outputEnv = outputPost = 0;
    outputLines = outputHeaderLines = responseStatus = 0;
    outputLocation = 0;
    responseMsg = 0;
    hasError = 0;
    timeout = 0;
    queryBuf = 0;
    queryLen = 0;
    numQueryKeys = numPostKeys = 0;

    originalArgc = argc;
    originalArgv = argv;

    if (getArgv(&argc, &argv, originalArgc, originalArgv) < 0) {
        error("Cannot read FAST input");
    }
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            continue;
        }
        for (cp = &argv[i][1]; *cp; cp++) {
            switch (*cp) {
            case 'a':
                outputArgs++;
                break;

            case 'b':
                if (++i >= argc) {
                    err = __LINE__;
                } else {
                    outputLines = atoi(argv[i]);
                }
                break;

            case 'e':
                outputEnv++;
                break;

            case 'h':
                if (++i >= argc) {
                    err = __LINE__;
                } else {
                    outputHeaderLines = atoi(argv[i]);
                }
                break;

            case 'l':
                if (++i >= argc) {
                    err = __LINE__;
                } else {
                    outputLocation = argv[i];
                    if (responseStatus == 0) {
                        responseStatus = 302;
                    }
                }
                break;

            case 'p':
                outputPost++;
                break;

            case 'q':
                outputQuery++;
                break;

            case 's':
                if (++i >= argc) {
                    err = __LINE__;
                } else {
                    responseStatus = atoi(argv[i]);
                }
                break;

            case 't':
                if (++i >= argc) {
                    err = __LINE__;
                } else {
                    timeout = atoi(argv[i]);
                }
                break;

            default:
                err = __LINE__;
                break;
            }
        }
    }
    if (err) {
        FCGX_FPrintF(fast_err, "usage: fastProgram -aenp [-b bytes] [-h lines]\n"
            "\t[-l location] [-s status] [-t timeout]\n"
            "\tor set the HTTP_SWITCHES environment variable\n");
        FCGX_FPrintF(fast_err, "Error at fastProgram:%d\n", __LINE__);
        exit(255);
    }

    while (FCGX_Accept(&fast_in, &fast_out, &fast_err, &fast_envp) >= 0) {
        if ((method = FCGX_GetParam("REQUEST_METHOD", fast_envp)) != 0 && strcmp(method, "POST") == 0) {
            if (getPostData(&postBuf, &postBufLen) < 0) {
                error("Cannot read FAST input");
            }
            if (strcmp(safeGetenv("CONTENT_TYPE"), "application/x-www-form-urlencoded") == 0) {
                numPostKeys = getVars(&postKeys, postBuf, postBufLen);
            }
        }

        if (hasError) {
            FCGX_FPrintF(fast_out, "HTTP/1.0 %d %s\r\n\r\n", responseStatus, responseMsg);
            FCGX_FPrintF(fast_out, "<HTML><BODY><p>Error: %d -- %s</p></BODY></HTML>\r\n", responseStatus, responseMsg);
            FCGX_FPrintF(fast_err, "fastProgram: ERROR: %s\n", responseMsg);
            exit(2);
        }

#if KEEP
        if (nonParsedHeader) {
            if (responseStatus == 0) {
                FCGX_FPrintF(fast_out, "HTTP/1.0 200 OK\r\n");
            } else {
                FCGX_FPrintF(fast_out, "HTTP/1.0 %d %s\r\n", responseStatus, responseMsg ? responseMsg: "");
            }
            FCGX_FPrintF(fast_out, "Connection: close\r\n");
            FCGX_FPrintF(fast_out, "X-FAST-CustomHeader: Any value at all\r\n");
        }
#endif
        FCGX_FPrintF(fast_out, "Content-Type: %s\r\n", "text/html");

        if (outputHeaderLines) {
            for (i = 0; i < outputHeaderLines; i++) {
                FCGX_FPrintF(fast_out, "X-FAST-%d: A loooooooooooooooooooooooong string\r\n", i);
            }
        }
        if (outputLocation) {
            FCGX_FPrintF(fast_out, "Location: %s\r\n", outputLocation);
        }
        if (responseStatus) {
            FCGX_FPrintF(fast_out, "Status: %d\r\n", responseStatus);
        }
        FCGX_FPrintF(fast_out, "\r\n");

        if ((outputLines + outputArgs + outputEnv + outputQuery + outputPost + outputLocation + responseStatus) == 0) {
            outputArgs++;
            outputEnv++;
            outputQuery++;
            outputPost++;
        }
        if (outputLines) {
            for (l = 0; l < outputLines; l++) {
                FCGX_FPrintF(fast_out, "%010d\n", l);
            }

        } else {
            FCGX_FPrintF(fast_out, "<HTML><TITLE>fastProgram: Output</TITLE><BODY>\r\n");
            if (outputArgs) {
    #if _WIN32
                FCGX_FPrintF(fast_out, "<P>CommandLine: %s</P>\r\n", GetCommandLine());
    #endif
                FCGX_FPrintF(fast_out, "<H2>Args</H2>\r\n");
                for (i = 0; i < argc; i++) {
                    FCGX_FPrintF(fast_out, "<P>ARG[%d]=%s</P>\r\n", i, argv[i]);
                }
            }
            printEnv(fast_envp);
            if (outputQuery) {
                printQuery();
            }
            if (outputPost) {
                printPost(postBuf, postBufLen);
            }
            FCGX_FPrintF(fast_out, "</BODY></HTML>\r\n");
        }
        FCGX_FFlush(fast_err);
        FCGX_FFlush(fast_out);
    }
    return 0;
}


/*
    If there is a HTTP_SWITCHES argument in the query string, examine that instead of the original argv
 */
static int getArgv(int *pargc, char ***pargv, int originalArgc, char **originalArgv)
{
    static char sbuf[1024];
    char        *switches, *next;
    int         i;

    *pargc = 0;
    if (getQueryString(&queryBuf, &queryLen) < 0) {
        return -1;
    }
    numQueryKeys = getVars(&queryKeys, queryBuf, queryLen);

    switches = 0;
    for (i = 0; i < numQueryKeys; i += 2) {
        if (strcmp(queryKeys[i], "HTTP_SWITCHES") == 0) {
            switches = queryKeys[i+1];
            break;
        }
    }

    if (switches == 0) {
        switches = FCGX_GetParam("HTTP_SWITCHES", fast_envp);
    }
    if (switches) {
        strncpy(sbuf, switches, sizeof(sbuf) - 1);
        descape(sbuf);
        next = strtok(sbuf, " \t\n");
        i = 1;
        for (i = 1; next && i < (MAX_ARGV - 1); i++) {
            argvList[i] = strdup(next);
            next = strtok(0, " \t\n");
        }
        argvList[0] = originalArgv[0];
        *pargv = argvList;
        *pargc = i;

    } else {
        *pargc = originalArgc;
        *pargv = originalArgv;
    }
    return 0;
}


static void printEnv(char **envp)
{
    FCGX_FPrintF(fast_out, "<H2>Environment Variables</H2>\r\n");
    FCGX_FPrintF(fast_out, "<P>AUTH_TYPE=%s</P>\r\n", safeGetenv("AUTH_TYPE"));
    FCGX_FPrintF(fast_out, "<P>CONTENT_LENGTH=%s</P>\r\n", safeGetenv("CONTENT_LENGTH"));
    FCGX_FPrintF(fast_out, "<P>CONTENT_TYPE=%s</P>\r\n", safeGetenv("CONTENT_TYPE"));
    FCGX_FPrintF(fast_out, "<P>DOCUMENT_ROOT=%s</P>\r\n", safeGetenv("DOCUMENT_ROOT"));
    FCGX_FPrintF(fast_out, "<P>GATEWAY_INTERFACE=%s</P>\r\n", safeGetenv("GATEWAY_INTERFACE"));
    FCGX_FPrintF(fast_out, "<P>HTTP_ACCEPT=%s</P>\r\n", safeGetenv("HTTP_ACCEPT"));
    FCGX_FPrintF(fast_out, "<P>HTTP_CONNECTION=%s</P>\r\n", safeGetenv("HTTP_CONNECTION"));
    FCGX_FPrintF(fast_out, "<P>HTTP_HOST=%s</P>\r\n", safeGetenv("HTTP_HOST"));
    FCGX_FPrintF(fast_out, "<P>HTTP_USER_AGENT=%s</P>\r\n", safeGetenv("HTTP_USER_AGENT"));
    FCGX_FPrintF(fast_out, "<P>PATH_INFO=%s</P>\r\n", safeGetenv("PATH_INFO"));
    FCGX_FPrintF(fast_out, "<P>PATH_TRANSLATED=%s</P>\r\n", safeGetenv("PATH_TRANSLATED"));
    FCGX_FPrintF(fast_out, "<P>QUERY_STRING=%s</P>\r\n", safeGetenv("QUERY_STRING"));
    FCGX_FPrintF(fast_out, "<P>REMOTE_ADDR=%s</P>\r\n", safeGetenv("REMOTE_ADDR"));
    FCGX_FPrintF(fast_out, "<P>REQUEST_METHOD=%s</P>\r\n", safeGetenv("REQUEST_METHOD"));
    FCGX_FPrintF(fast_out, "<P>REQUEST_URI=%s</P>\r\n", safeGetenv("REQUEST_URI"));
    FCGX_FPrintF(fast_out, "<P>REMOTE_USER=%s</P>\r\n", safeGetenv("REMOTE_USER"));
    FCGX_FPrintF(fast_out, "<P>SCRIPT_NAME=%s</P>\r\n", safeGetenv("SCRIPT_NAME"));
    FCGX_FPrintF(fast_out, "<P>SCRIPT_FILENAME=%s</P>\r\n", safeGetenv("SCRIPT_FILENAME"));
    FCGX_FPrintF(fast_out, "<P>SERVER_ADDR=%s</P>\r\n", safeGetenv("SERVER_ADDR"));
    FCGX_FPrintF(fast_out, "<P>SERVER_NAME=%s</P>\r\n", safeGetenv("SERVER_NAME"));
    FCGX_FPrintF(fast_out, "<P>SERVER_PORT=%s</P>\r\n", safeGetenv("SERVER_PORT"));
    FCGX_FPrintF(fast_out, "<P>SERVER_PROTOCOL=%s</P>\r\n", safeGetenv("SERVER_PROTOCOL"));
    FCGX_FPrintF(fast_out, "<P>SERVER_SOFTWARE=%s</P>\r\n", safeGetenv("SERVER_SOFTWARE"));

    /*
        This is not supported on VxWorks as you cannot get "envp" in main()
     */
    FCGX_FPrintF(fast_out, "\r\n<H2>All Defined Environment Variables</H2>\r\n");
    if (envp) {
        char    *p;
        int     i;
        for (i = 0, p = envp[0]; envp[i]; i++) {
            p = envp[i];
            FCGX_FPrintF(fast_out, "<P>%s</P>\r\n", p);
        }
    }
    FCGX_FPrintF(fast_out, "\r\n");
}


static void printQuery()
{
    int     i;

    if (numQueryKeys == 0) {
        FCGX_FPrintF(fast_out, "<H2>No Query String Found</H2>\r\n");
    } else {
        FCGX_FPrintF(fast_out, "<H2>Decoded Query String Variables</H2>\r\n");
        for (i = 0; i < (numQueryKeys * 2); i += 2) {
            if (queryKeys[i+1] == 0) {
                FCGX_FPrintF(fast_out, "<p>QVAR %s=</p>\r\n", queryKeys[i]);
            } else {
                FCGX_FPrintF(fast_out, "<p>QVAR %s=%s</p>\r\n", queryKeys[i], queryKeys[i+1]);
            }
        }
    }
    FCGX_FPrintF(fast_out, "\r\n");
}


static void printPost(char *buf, size_t len)
{
    int     i;

    if (numPostKeys) {
        FCGX_FPrintF(fast_out, "<H2>Decoded Post Variables</H2>\r\n");
        for (i = 0; i < (numPostKeys * 2); i += 2) {
            FCGX_FPrintF(fast_out, "<p>PVAR %s=%s</p>\r\n", postKeys[i], postKeys[i+1]);
        }

    } else if (buf) {
        if (len < (50 * 1000)) {
            FCGX_FPrintF(fast_out, "<H2>Post Data %d bytes found (data below)</H2>\r\n", (int) len);
            FCGX_FFlush(fast_out);
            if (write(1, buf, (int) len) != len) {}
        } else {
            FCGX_FPrintF(fast_out, "<H2>Post Data %d bytes found</H2>\r\n", (int) len);
        }

    } else {
        FCGX_FPrintF(fast_out, "<H2>No Post Data Found</H2>\r\n");
    }
    FCGX_FPrintF(fast_out, "\r\n");
}


static int getQueryString(char **buf, size_t *buflen)
{
    *buflen = 0;
    *buf = 0;

    if (FCGX_GetParam("QUERY_STRING", fast_envp) == 0) {
        *buf = "";
        *buflen = 0;
    } else {
        *buf = FCGX_GetParam("QUERY_STRING", fast_envp);
        *buflen = (int) strlen(*buf);
    }
    return 0;
}


static int getPostData(char **bufp, size_t *lenp)
{
    char    *contentLength, *buf;
    ssize_t bufsize, bytes, size, limit, len;

    if ((contentLength = FCGX_GetParam("CONTENT_LENGTH", fast_envp)) != 0) {
        size = atoi(contentLength);
        if (size < 0 || size >= INT_MAX) {
            error("Bad content length");
            return -1;
        }
        limit = size;
    } else {
        size = 4096;
        limit = INT_MAX;
    }
    bufsize = size + 1;
    if ((buf = malloc(bufsize)) == 0) {
        error("Could not allocate memory to read post data");
        return -1;
    }
    len = 0;

    while (len < limit) {
        if ((len + size + 1) > bufsize) {
            if ((buf = realloc(buf, len + size + 1)) == 0) {
                error("Could not allocate memory to read post data");
                return -1;
            }
            bufsize = len + size + 1;
        }
        bytes = read(0, &buf[len], (int) size);
        if (bytes < 0) {
            error("Could not read FAST input %d", errno);
            return -1;
        } else if (bytes == 0) {
            /* EOF */
#if UNUSED
            /*
                If using multipart-mime, the CONTENT_LENGTH won't match the length of the data actually received
             */
            if (contentLength && len != limit) {
                error("Missing content data (Content-Length: %s)", contentLength ? contentLength : "unspecified");
            }
#endif
            break;
        }
        len += bytes;
    }
    buf[len] = 0;
    *lenp = len;
    *bufp = buf;
    return 0;
}


static int getVars(char ***cgiKeys, char *buf, size_t buflen)
{
    char    **keyList, *eq, *cp, *pp, *newbuf;
    int     i, keyCount;

    if (buflen > 0) {
        if ((newbuf = malloc(buflen + 1)) == 0) {
            error("Cannot allocate memory");
            return 0;
        }
        strncpy(newbuf, buf, buflen);
        newbuf[buflen] = '\0';
        buf = newbuf;
    }

    /*
        Change all plus signs back to spaces
     */
    keyCount = (buflen > 0) ? 1 : 0;
    for (cp = buf; cp < &buf[buflen]; cp++) {
        if (*cp == '+') {
            *cp = ' ';
        } else if (*cp == '&') {
            keyCount++;
        }
    }
    if (keyCount == 0) {
        return 0;
    }

    /*
        Crack the input into name/value pairs
     */
    keyList = malloc((keyCount * 2) * sizeof(char**));

    i = 0;
    for (pp = strtok(buf, "&"); pp; pp = strtok(0, "&")) {
        if ((eq = strchr(pp, '=')) != 0) {
            *eq++ = '\0';
            descape(pp);
            descape(eq);
        } else {
            descape(pp);
        }
        if (i < (keyCount * 2)) {
            keyList[i++] = pp;
            keyList[i++] = eq;
        }
    }
    *cgiKeys = keyList;
    return keyCount;
}


static char hex2Char(char *s)
{
    char    c;

    if (*s >= 'A') {
        c = toupper(*s & 0xFF) - 'A' + 10;
    } else {
        c = *s - '0';
    }
    s++;

    if (*s >= 'A') {
        c = (c * 16) + (toupper(*s & 0xFF) - 'A') + 10;
    } else {
        c = (c * 16) + (toupper(*s & 0xFF) - '0');
    }
    return c;
}


static void descape(char *src)
{
    char    *dest;

    dest = src;
    while (*src) {
        if (*src == '%') {
            *dest++ = hex2Char(++src) ;
            src += 2;
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';
}


static char *safeGetenv(char *key)
{
    char    *cp;

    cp = FCGX_GetParam(key, fast_envp);
    if (cp == 0) {
        return "";
    }
    return cp;
}


static void error(char *fmt, ...)
{
    va_list args;
    char    buf[4096];

    if (responseMsg == 0) {
        va_start(args, fmt);
        FCGX_FPrintF(fast_out, buf, fmt, args);
        responseStatus = 400;
        responseMsg = strdup(buf);
        va_end(args);
    }
    hasError++;
}


/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
