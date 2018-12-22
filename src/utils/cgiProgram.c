/*
 *  cgiProgram.c - Test CGI program
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 *
 *  Usage:
 *      cgiProgram [switches]
 *          -a                  Output the args (used for ISINDEX queries)
 *          -b bytes            Output content "bytes" long                 
 *          -e                  Output the environment 
 *          -h lines            Output header "lines" long
 *          -l location         Output "location" header
 *          -n                  Non-parsed-header ouput
 *          -p                  Ouput the post data
 *          -q                  Ouput the query data
 *          -s status           Output "status" header
 *          default             Output args, env and query
 *
 *      Alternatively, pass the arguments as an environment variable HTTP_SWITCHES="-a -e -q"
 */

/********************************** Includes **********************************/

#include "mpr.h"

/*********************************** Locals ***********************************/

#define MAX_ARGV    64

static char     *argvList[MAX_ARGV];
static int      getArgv(Mpr *mpr, int *argc, char ***argv, int originalArgc, char **originalArgv);
static int      hasError;
static Mpr      *mpr;
static int      nonParsedHeader;
static int      numPostKeys;
static int      numQueryKeys;
static int      originalArgc;
static char     **originalArgv;
static int      outputArgs, outputEnv, outputPost, outputQuery;
static int      outputBytes, outputHeaderLines, responseStatus;
static char     *outputLocation;
static MprBuf   *postBuf;
static char     **postKeys;
static char     *queryBuf;
static int      queryLen;
static char     **queryKeys;
static char     *responseMsg;
static int      timeout;

/***************************** Forward Declarations ***************************/

static void     printQuery();
static void     printPost(MprBuf *buf);
static int      getVars(MprCtx ctx, char ***cgiKeys, char *buf, int buflen);
static int      getPostData(MprCtx ctx, MprBuf *buf);
static int      getQueryString(Mpr *mpr, char **buf, int *buflen);
static void     descape(char *src);
static char     hex2Char(char *s); 
static char     *safeGetenv(char *key);
static void     error(Mpr *mpr, char *fmt, ...);

#if !VXWORKS && !WINCE
static void     printEnv(char **env);
#endif

/******************************************************************************/
/*
 *  Test program entry point
 */

#if VXWORKS || WINCE
MAIN(cgiProgramMain, int argc, char *argv[])
#else
int main(int argc, char *argv[], char *envp[])
#endif
{
    char    *cp, *method;
    int     i, j, err;

    err = 0;
    outputArgs = outputQuery = outputEnv = outputPost = 0;
    outputBytes = outputHeaderLines = responseStatus = 0;
    outputLocation = 0;
    nonParsedHeader = 0;
    responseMsg = 0;
    hasError = 0;
    timeout = 0;
    queryBuf = 0;
    queryLen = 0;
    numQueryKeys = numPostKeys = 0;

    originalArgc = argc;
    originalArgv = argv;

    mpr = mprCreate(argc, argv, NULL);

#if _WIN32 && !WINCE
    _setmode(0, O_BINARY);
    _setmode(1, O_BINARY);
    _setmode(2, O_BINARY);
#endif

    if (strncmp(mprGetPathBase(mpr, argv[0]), "nph-", 4) == 0) {
        nonParsedHeader++;
    }
    if (getArgv(mpr, &argc, &argv, originalArgc, originalArgv) < 0) {
        error(mpr, "Can't read CGI input");
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
                    outputBytes = atoi(argv[i]);
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
                    nonParsedHeader++;
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

            case 'n':
                nonParsedHeader++;
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
        mprError(mpr, "usage: cgiProgram -aenp [-b bytes] [-h lines]\n"
            "\t[-l location] [-s status] [-t timeout]\n"
            "\tor set the HTTP_SWITCHES environment variable\n");
        mprError(mpr, "Error at cgiProgram:%d\n", __LINE__);
        exit(255);
    }

    method = getenv("REQUEST_METHOD") ;
    if (method == 0) {
        method = "GET";
    } else {
        if (strcmp(method, "POST") == 0) {
            postBuf = mprCreateBuf(mpr, -1, -1);
            if (getPostData(mpr, postBuf) < 0) {
                error(mpr, "Can't read CGI input");
            }
            if (strcmp(safeGetenv("CONTENT_TYPE"), "application/x-www-form-urlencoded") == 0) {
                numPostKeys = getVars(mpr, &postKeys, mprGetBufStart(postBuf), mprGetBufLength(postBuf));
            }
        }
    }

    if (hasError) {
        if (! nonParsedHeader) {
            mprPrintf(mpr, "HTTP/1.0 %d %s\r\n\r\n", responseStatus, responseMsg);
            mprPrintf(mpr, "<HTML><BODY><p>Error: %d -- %s</p></BODY></HTML>\r\n", responseStatus, responseMsg);
        }
        exit(2);
    }

    if (nonParsedHeader) {
        if (responseStatus == 0) {
            mprPrintf(mpr, "HTTP/1.0 200 OK\r\n");
        } else {
            mprPrintf(mpr, "HTTP/1.0 %d %s\r\n", responseStatus, responseMsg ? responseMsg: "");
        }
        mprPrintf(mpr, "Connection: close\r\n");
        mprPrintf(mpr, "X-CGI-CustomHeader: Any value at all\r\n");
    }

    mprPrintf(mpr, "Content-type: %s\r\n", "text/html");

    if (outputHeaderLines) {
        j = 0;
        for (i = 0; i < outputHeaderLines; i++) {
            mprPrintf(mpr, "X-CGI-%d: A loooooooooooooooooooooooong string\r\n", i);
        }
    }

    if (outputLocation) {
        mprPrintf(mpr, "Location: %s\r\n", outputLocation);
    }
    if (responseStatus) {
        mprPrintf(mpr, "Status: %d\r\n", responseStatus);
    }
    mprPrintf(mpr, "\r\n");

#if UNUSED
    mprSleep(mpr, 60000 * 2);
#endif

    if ((outputBytes + outputArgs + outputEnv + outputQuery + outputPost + outputLocation + responseStatus) == 0) {
        outputArgs++;
        outputEnv++;
        outputQuery++;
        outputPost++;
    }

    if (outputBytes) {
        for (j = 0; j < outputBytes; j++) {
            printf("%010d\n", j);
        }

    } else {
        mprPrintf(mpr, "<HTML><TITLE>cgiProgram: Output</TITLE><BODY>\r\n");
        if (outputArgs) {
#if _WIN32
            mprPrintf(mpr, "<P>CommandLine: %s</P>\r\n", GetCommandLine());
#endif
            mprPrintf(mpr, "<H2>Args</H2>\r\n");
            for (i = 0; i < argc; i++) {
                mprPrintf(mpr, "<P>ARG[%d]=%s</P>\r\n", i, argv[i]);
            }
        }
#if !VXWORKS && !WINCE
        if (outputEnv) {
            printEnv(envp);
        }
#endif
        if (outputQuery) {
            printQuery();
        }
        if (outputPost) {
            printPost(postBuf);
        }
        if (timeout) {
            mprSleep(mpr, timeout * MPR_TICKS_PER_SEC);
        }
        mprPrintf(mpr, "</BODY></HTML>\r\n");
    }
#if VXWORKS
/*
 *  VxWorks pipes need an explicit eof string
 */
    
    write(1, MPR_CMD_VXWORKS_EOF, MPR_CMD_VXWORKS_EOF_LEN);
    write(2, MPR_CMD_VXWORKS_EOF, MPR_CMD_VXWORKS_EOF_LEN);

    /*
     *  Must not call exit(0) in Vxworks as that will exit the task before the CGI handler can cleanup. Must use return 0.
     */
#endif
    return 0;
}


/*
 *  If there is a HTTP_SWITCHES argument in the query string, examine that instead of the original argv
 */
static int getArgv(Mpr *mpr, int *pargc, char ***pargv, int originalArgc, char **originalArgv)
{
    char    *switches, *next, sbuf[1024];
    int     i;

    *pargc = 0;
    if (getQueryString(mpr, &queryBuf, &queryLen) < 0) {
        return -1;
    }
    numQueryKeys = getVars(mpr, &queryKeys, queryBuf, queryLen);

    switches = 0;
    for (i = 0; i < numQueryKeys; i += 2) {
        if (strcmp(queryKeys[i], "HTTP_SWITCHES") == 0) {
            switches = queryKeys[i+1];
            break;
        }
    }

    if (switches == 0) {
        switches = getenv("HTTP_SWITCHES");
    }
    if (switches) {
        strncpy(sbuf, switches, sizeof(sbuf) - 1);
        descape(sbuf);
        next = strtok(sbuf, " \t\n");
        i = 1;
        for (i = 1; next && i < (MAX_ARGV - 1); i++) {
            argvList[i] = next;
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


#if !VXWORKS && !WINCE
static void printEnv(char **envp)
{
    mprPrintf(mpr, "<H2>Environment Variables</H2>\r\n");
    mprPrintf(mpr, "<P>AUTH_TYPE=%s</P>\r\n", safeGetenv("AUTH_TYPE"));
    mprPrintf(mpr, "<P>CONTENT_LENGTH=%s</P>\r\n", safeGetenv("CONTENT_LENGTH"));
    mprPrintf(mpr, "<P>CONTENT_TYPE=%s</P>\r\n", safeGetenv("CONTENT_TYPE"));
    mprPrintf(mpr, "<P>DOCUMENT_ROOT=%s</P>\r\n", safeGetenv("DOCUMENT_ROOT"));
    mprPrintf(mpr, "<P>GATEWAY_INTERFACE=%s</P>\r\n", safeGetenv("GATEWAY_INTERFACE"));
    mprPrintf(mpr, "<P>HTTP_ACCEPT=%s</P>\r\n", safeGetenv("HTTP_ACCEPT"));
    mprPrintf(mpr, "<P>HTTP_CONNECTION=%s</P>\r\n", safeGetenv("HTTP_CONNECTION"));
    mprPrintf(mpr, "<P>HTTP_HOST=%s</P>\r\n", safeGetenv("HTTP_HOST"));
    mprPrintf(mpr, "<P>HTTP_USER_AGENT=%s</P>\r\n", safeGetenv("HTTP_USER_AGENT"));
    mprPrintf(mpr, "<P>PATH_INFO=%s</P>\r\n", safeGetenv("PATH_INFO"));
    mprPrintf(mpr, "<P>PATH_TRANSLATED=%s</P>\r\n", safeGetenv("PATH_TRANSLATED"));
    mprPrintf(mpr, "<P>QUERY_STRING=%s</P>\r\n", safeGetenv("QUERY_STRING"));
    mprPrintf(mpr, "<P>REMOTE_ADDR=%s</P>\r\n", safeGetenv("REMOTE_ADDR"));
    mprPrintf(mpr, "<P>REMOTE_HOST=%s</P>\r\n", safeGetenv("REMOTE_HOST"));
    mprPrintf(mpr, "<P>REQUEST_METHOD=%s</P>\r\n", safeGetenv("REQUEST_METHOD"));
    mprPrintf(mpr, "<P>REQUEST_URI=%s</P>\r\n", safeGetenv("REQUEST_URI"));
    mprPrintf(mpr, "<P>REMOTE_USER=%s</P>\r\n", safeGetenv("REMOTE_USER"));
    mprPrintf(mpr, "<P>SCRIPT_NAME=%s</P>\r\n", safeGetenv("SCRIPT_NAME"));
    mprPrintf(mpr, "<P>SERVER_ADDR=%s</P>\r\n", safeGetenv("SERVER_ADDR"));
    mprPrintf(mpr, "<P>SERVER_HOST=%s</P>\r\n", safeGetenv("SERVER_HOST"));
    mprPrintf(mpr, "<P>SERVER_NAME=%s</P>\r\n", safeGetenv("SERVER_NAME"));
    mprPrintf(mpr, "<P>SERVER_PORT=%s</P>\r\n", safeGetenv("SERVER_PORT"));
    mprPrintf(mpr, "<P>SERVER_PROTOCOL=%s</P>\r\n", safeGetenv("SERVER_PROTOCOL"));
    mprPrintf(mpr, "<P>SERVER_SOFTWARE=%s</P>\r\n", safeGetenv("SERVER_SOFTWARE"));
    mprPrintf(mpr, "<P>SERVER_URL=%s</P>\r\n", safeGetenv("SERVER_URL"));

    mprPrintf(mpr, "\r\n<H2>All Defined Environment Variables</H2>\r\n"); 
    if (envp) {
        char    *p;
        int     i;
        for (i = 0, p = envp[0]; envp[i]; i++) {
            p = envp[i];
            mprPrintf(mpr, "<P>%s</P>\r\n", p);
        }
    }
    mprPrintf(mpr, "\r\n");
}
#endif


static void printQuery()
{
    int     i;

    if (numQueryKeys == 0) {
        mprPrintf(mpr, "<H2>No Query String Found</H2>\r\n");
    } else {
        mprPrintf(mpr, "<H2>Decoded Query String Variables</H2>\r\n");
        for (i = 0; i < (numQueryKeys * 2); i += 2) {
            if (queryKeys[i+1] == 0) {
                mprPrintf(mpr, "<p>QVAR %s=</p>\r\n", queryKeys[i]);
            } else {
                mprPrintf(mpr, "<p>QVAR %s=%s</p>\r\n", queryKeys[i], queryKeys[i+1]);
            }
        }
    }
    mprPrintf(mpr, "\r\n");
}

 
static void printPost(MprBuf *buf)
{
    int     i;

    if (numPostKeys) {
        mprPrintf(mpr, "<H2>Decoded Post Variables</H2>\r\n");
        for (i = 0; i < (numPostKeys * 2); i += 2) {
            mprPrintf(mpr, "<p>PVAR %s=%s</p>\r\n", postKeys[i], postKeys[i+1]);
        }
    } else if (buf) {
        printf("<H2>Post Data %d bytes found (data below)</H2>\r\n", mprGetBufLength(buf));
        if (write(1, mprGetBufStart(buf), mprGetBufLength(buf)) != 0) {}
    } else {
        mprPrintf(mpr, "<H2>No Post Data Found</H2>\r\n");
    }
    mprPrintf(mpr, "\r\n");
}


static int getQueryString(Mpr *mpr, char **buf, int *buflen)
{
    *buflen = 0;
    *buf = 0;

    if (getenv("QUERY_STRING") == 0) {
        *buf = mprStrdup(mpr, "");
        *buflen = 0;
    } else {
        *buf = mprStrdup(mpr, getenv("QUERY_STRING"));
        *buflen = (int) strlen(*buf);
    }
    return 0;
}


static int getPostData(MprCtx ctx, MprBuf *buf)
{
    char    *contentLength;
    int     bytes, len, space, expected;

    if ((contentLength = getenv("CONTENT_LENGTH")) != 0) {
        len = atoi(contentLength);
        expected = len;
    } else {
        len = MAXINT;
        expected = MAXINT;
    }
    while (len > 0) {
        space = mprGetBufSpace(buf);
        if (space < MPR_BUFSIZE) {
            if (mprGrowBuf(buf, MPR_BUFSIZE) < 0) {
                error(mpr, "Couldn't allocate memory to read post data");
                return -1;
            }
        }
        space = mprGetBufSpace(buf);
        bytes = (int) read(0, mprGetBufEnd(buf), space);
        if (bytes < 0) {
            error(mpr, "Couldn't read CGI input %d", errno);
            return -1;

        } else if (bytes == 0) {
            /* EOF */
            if (contentLength && len != expected) {
                error(mpr, "Missing content data (Content-Length %s)", contentLength);
            }
            break;
        }
        mprAdjustBufEnd(buf, bytes);
        len -= bytes;
    }
    mprAddNullToBuf(buf);
    return 0;
}


static int getVars(MprCtx ctx, char ***cgiKeys, char *buf, int buflen)
{
    char    **keyList;
    char    *eq, *cp, *pp;
    int     i, keyCount;

    /*
     *  Change all plus signs back to spaces
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
     *  Crack the input into name/value pairs 
     */
    keyList = (char**) mprAlloc(ctx, (keyCount * 2) * (int) sizeof(char**));

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
        c = (*s & 0xDF) - 'A';
    } else {
        c = *s - '0';
    }
    s++;

    if (*s >= 'A') {
        c = c * 16 + ((*s & 0xDF) - 'A');
    } else {
        c = c * 16 + (*s - '0');
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

    cp = getenv(key);
    if (cp == 0) {
        return "";
    }
    return cp;
}


void error(Mpr *mpr, char *fmt, ...)
{
    va_list args;
    char    buf[4096];

    if (responseMsg == 0) {
        va_start(args, fmt);
        vsprintf(buf, fmt, args);
        responseStatus = 400;
        responseMsg = mprStrdup(mpr, buf);
        va_end(args);
    }
    hasError++;
}


#if VXWORKS
/*
 *  VxWorks link resolution
 */
int _cleanup() {
    return 0;
}
int _exit() {
    return 0;
}
#endif /* VXWORKS */

/*
 *  @copy   default
 *
 *  Copyright (c) Embedthis Software LLC, 2003-2012. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2012. All Rights Reserved.
 *
 *  This software is distributed under commercial and open source licenses.
 *  You may use the GPL open source license described below or you may acquire
 *  a commercial license from Embedthis Software. You agree to be fully bound
 *  by the terms of either license. Consult the LICENSE.TXT distributed with
 *  this software for full details.
 *
 *  This software is open source; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version. See the GNU General Public License for more
 *  details at: http://www.embedthis.com/downloads/gplLicense.html
 *
 *  This program is distributed WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  This GPL license does NOT permit incorporating this software into
 *  proprietary programs. If you are unable to comply with the GPL, you must
 *  acquire a commercial license to use this software. Commercial licenses
 *  for this software and support services are available from Embedthis
 *  Software at http://www.embedthis.com
 *
 *  Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
