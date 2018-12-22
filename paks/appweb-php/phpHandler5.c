/*

    phpHandler.c - Appweb PHP handler

    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "appweb.h"

#if ME_COM_PHP

#if ME_WIN_LIKE
    /*
        Workaround for VS 2005 and PHP headers. Need to include before PHP headers include it.
     */
    #if _MSC_VER >= 1400
        #include    <sys/utime.h>
    #endif
    #undef  WIN32
    #define WIN32 1
    #define WINNT 1
    #define TIME_H
    #undef _WIN32_WINNT
    #undef chdir
    #undef popen
    #undef pclose
    #undef strcasecmp
    #undef strncasecmp
    #define PHP_WIN32 1
    #define ZEND_WIN32 1

    /*
        WARNING: If you compile PHP with --debug, then you MUST re-run appweb configure which will set ME_PHP_DEBUG
        Unfortunately, PHP does not set ZEND_DEBUG in any of their headers (Ugh!)
     */
    #if ME_PHP_DEBUG
    #define ZEND_DEBUG 1
    #endif
#endif

    #define ZTS 1
    #undef ulong
    #undef ZEND_API
    #undef _res /* Defined by linux /usr/include/resolv.h */
    #undef HAVE_SOCKLEN_T

    /*
        Indent headers to side-step make depend if PHP is not enabled
     */
    #include <main/php.h>
    #include <main/php_globals.h>
    #include <main/php_variables.h>
    #include <Zend/zend_modules.h>
    #include <main/SAPI.h>
#ifdef PHP_WIN32
    #include <win32/time.h>
    #include <win32/signal.h>
    #include <process.h>
#else
    #include <main/build-defs.h>
#endif
    #include <Zend/zend.h>
    #include <Zend/zend_extensions.h>
    #include <main/php_ini.h>
    #include <main/php_globals.h>
    #include <main/php_main.h>

#if PHP_MAJOR_VERSION < 7
/********************************** Defines ***********************************/

#if ME_MAJOR_VERSION < 8
    /*
        Support legacy appweb (4-7) by converting back from HttpStream to HttpConn
     */
    #undef HttpConn
    #undef conn
    #define stream conn
    #define HttpStream HttpConn
#endif

typedef struct MaPhp {
    zval    *var_array;             /* Track var array */
} MaPhp;

static void                    ***tsrm_ls;
static php_core_globals        *core_globals;
static sapi_globals_struct     *sapi_globals;
static zend_llist              global_vars;
static zend_compiler_globals   *compiler_globals;
static zend_executor_globals   *executor_globals;

/****************************** Forward Declarations **********************/

static void flushOutput(void *context);
static int initializePhp(Http *http);
static void logMessage(char *message TSRMLS_DC);
static char *mapHyphen(char *str);
static int readBodyData(char *buffer, uint len TSRMLS_DC);
static char *readCookies(TSRMLS_D);
static void registerServerVars(zval *varArray TSRMLS_DC);
static int startup(sapi_module_struct *sapiModule);
static int sendHeaders(sapi_headers_struct *sapiHeaders TSRMLS_DC);
static int writeBlock(cchar *str, uint len TSRMLS_DC);

#if PHP_MAJOR_VERSION >=5 && PHP_MINOR_VERSION >= 3
    static int writeHeader(sapi_header_struct *sapiHeader, sapi_header_op_enum op, sapi_headers_struct *sapiHeaders TSRMLS_DC);
#else
    static int  writeHeader(sapi_header_struct *sapiHeader, sapi_headers_struct *sapiHeaders TSRMLS_DC);
#endif

/************************************ Locals **********************************/
/*
    PHP Module Interface
 */
static sapi_module_struct phpSapiBlock = {
    ME_NAME,                        /* Sapi name */
    ME_TITLE,                       /* Full name */
    startup,                        /* Start routine */
    php_module_shutdown_wrapper,    /* Stop routine  */
    0,                              /* Activate */
    0,                              /* Deactivate */
    writeBlock,                     /* Write */
    flushOutput,                    /* Flush */
    0,                              /* Getstat */
    0,                              /* Getenv */
    php_error,                      /* Errors */
    writeHeader,                    /* Write headers */
    sendHeaders,                    /* Send headers */
    0,                              /* Send single header */
    readBodyData,                   /* Read body data */
    readCookies,                    /* Read session cookies */
    registerServerVars,             /* Define request variables */
    logMessage,                     /* Emit a log message */
    NULL,                           /* Get time */
    0,                              /* Terminate process */
    0,                              /* Unblock */
    STANDARD_SAPI_MODULE_PROPERTIES
};

/********************************** Code ***************************/
/*
    Open handler for a new request
 */
static int openPhp(HttpQueue *q)
{
    /*
        PHP will buffer all input. i.e. does not stream. The normal Limits still apply.
     */
    q->max = q->pair->max = MAXINT;
    httpTrimExtraPath(q->stream);
    httpMapFile(q->stream);
    if (!q->stage->stageData) {
        if (initializePhp(q->stream->http) < 0) {
            httpError(q->stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "PHP initialization failed");
        }
        q->stage->stageData = mprAlloc(1);
    }
    q->queueData = mprAllocObj(MaPhp, NULL);
    q->pair->queueData = q->queueData;
    return 0;
}


/*
    Run the request. This is invoked when all the input data has been received and buffered.
    This routine completely services the request.
 */
static void readyPhp(HttpQueue *q)
{
    HttpStream          *stream;
    HttpRx              *rx;
    HttpTx              *tx;
    MaPhp               *php;
    FILE                *fp;
    cchar               *value;
    char                shebang[ME_MAX_PATH];
    zend_file_handle    file_handle;

    TSRMLS_FETCH();

    stream = q->stream;
    rx = stream->rx;
    tx = stream->tx;
    if ((php = q->queueData) == 0) {
        return;
    }
    /*
        Set the request context
     */
    zend_first_try {
        php->var_array = 0;
        SG(server_context) = stream;
        if (stream->username) {
            SG(request_info).auth_user = estrdup(stream->username);
        }
        if (stream->password) {
            SG(request_info).auth_password = estrdup(stream->password);
        }
        if ((value = httpGetHeader(stream, "Authorization")) != 0) {
            SG(request_info).auth_digest = estrdup(value);
        }
        SG(request_info).content_type = rx->mimeType;
        SG(request_info).path_translated = (char*) tx->filename;
        SG(request_info).content_length = (long) (ssize) rx->length;
        SG(sapi_headers).http_response_code = HTTP_CODE_OK;
        SG(request_info).query_string = (char*) rx->parsedUri->query;
        SG(request_info).request_method = rx->method;
        SG(request_info).request_uri = (char*) rx->uri;

        /*
            Workaround on MAC OS X where the SIGPROF is given to the wrong thread
         */
        PG(max_input_time) = -1;
        EG(timeout_seconds) = 0;

        /* The readBodyData callback may be invoked during startup */
        php_request_startup(TSRMLS_C);
        CG(zend_lineno) = 0;

    } zend_catch {
        mprLog("error php", 0, "Cannot start request");
        zend_try {
            php_request_shutdown(0);
        } zend_end_try();
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR, "PHP initialization failed");
        return;
    } zend_end_try();

    /*
        Execute the script file
     */
    file_handle.filename = tx->filename;
    file_handle.free_filename = 0;
    file_handle.opened_path = 0;

#if LOAD_FROM_FILE
    file_handle.type = ZEND_HANDLE_FILENAME;
#else
    file_handle.type = ZEND_HANDLE_FP;
    if ((fp = fopen(tx->filename, "r")) == NULL) {
        if (rx->referrer) {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot open document: %s from %s", tx->filename, rx->referrer);
        } else {
            httpError(stream, HTTP_CODE_NOT_FOUND, "Cannot open document: %s", tx->filename);
        }
        return;
    }
    /*
        Check for shebang and skip
     */
    file_handle.handle.fp = fp;
    shebang[0] = '\0';
    if (fgets(shebang, sizeof(shebang), file_handle.handle.fp)) {}
    if (shebang[0] != '#' || shebang[1] != '!') {
        fseek(fp, 0L, SEEK_SET);
    }
#endif
    zend_try {
        php_execute_script(&file_handle TSRMLS_CC);
        if (!SG(headers_sent)) {
            sapi_send_headers(TSRMLS_C);
        }
    } zend_catch {
        php_request_shutdown(NULL);
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR,  "PHP script execution failed");
        return;
    } zend_end_try();

    zend_try {
        php_request_shutdown(NULL);
        SG(server_context) = NULL;
    } zend_catch {
        httpError(stream, HTTP_CODE_INTERNAL_SERVER_ERROR,  "PHP script shutdown failed");
    } zend_end_try();

    httpFinalize(stream);
}

 /*************************** PHP Support Functions ***************************/
/*
    Flush write data back to the client
 */
static void flushOutput(void *server_context)
{
    HttpStream    *stream;

    stream = (HttpStream*) server_context;
    if (stream) {
        httpServiceQueues(stream, HTTP_NON_BLOCK);
    }
}


static int writeBlock(cchar *str, uint len TSRMLS_DC)
{
    HttpStream  *stream;
    ssize       written;

    stream = (HttpStream*) SG(server_context);
    if (stream == 0) {
        return -1;
    }
    written = httpWriteBlock(stream->writeq, str, len, HTTP_BUFFER);
    if (written <= 0) {
        php_handle_aborted_connection();
    }
    return written;
}


static void registerServerVars(zval *track_vars_array TSRMLS_DC)
{
    HttpStream  *stream;
    HttpRx      *rx;
    MaPhp       *php;
    MprKey      *kp;
    MprJson     *param;
    char        *key;
    int         index;

    stream = (HttpStream*) SG(server_context);
    if (stream == 0) {
        return;
    }
    rx = stream->rx;
    php_import_environment_variables(track_vars_array TSRMLS_CC);

    php = httpGetQueueData(stream);
    assert(php);
    php->var_array = track_vars_array;

    httpCreateCGIParams(stream);

    /*
        Set from three collections: HTTP Headers, Server Vars and Form Params
     */
    if (rx->headers) {
        for (ITERATE_KEYS(rx->headers, kp)) {
            if (kp->data) {
                key = mapHyphen(sjoin("HTTP_", supper(kp->key), NULL));
                php_register_variable(key, (char*) kp->data, php->var_array TSRMLS_CC);
            }
        }
    }
    if (rx->svars) {
        for (ITERATE_KEYS(rx->svars, kp)) {
            if (kp->data) {
                php_register_variable(kp->key, (char*) kp->data, php->var_array TSRMLS_CC);
            }
        }
    }
    if (rx->params) {
        for (ITERATE_JSON(rx->params, param, index)) {
            php_register_variable(supper(param->name), (char*) param->value, php->var_array TSRMLS_CC);
        }
    }
    if (SG(request_info).request_uri) {
        php_register_variable("PHP_SELF", SG(request_info).request_uri,  track_vars_array TSRMLS_CC);
    }
    php_register_variable("HTTPS", (stream->secure) ? "on" : "",  track_vars_array TSRMLS_CC);
}


static void logMessage(char *message TSRMLS_DC)
{
    mprLog("info php", 3, "%s", message);
}


static char *readCookies(TSRMLS_D)
{
    HttpStream    *stream;

    stream = (HttpStream*) SG(server_context);
    return (char*) stream->rx->cookie;
}


static int sendHeaders(sapi_headers_struct *phpHeaders TSRMLS_DC)
{
    HttpStream    *stream;

    stream = (HttpStream*) SG(server_context);
    if (stream->tx->status == HTTP_CODE_OK) {
        /* Preserve non-ok status that may be set if using a PHP ErrorDocument */
        httpSetStatus(stream, phpHeaders->http_response_code);
    }
    httpSetContentType(stream, phpHeaders->mimetype);
    return SAPI_HEADER_SENT_SUCCESSFULLY;
}


#if PHP_MAJOR_VERSION >=5 && PHP_MINOR_VERSION >= 3
static int writeHeader(sapi_header_struct *sapiHeader, sapi_header_op_enum op, sapi_headers_struct *sapiHeaders TSRMLS_DC)
#else
static int writeHeader(sapi_header_struct *sapiHeader, sapi_headers_struct *sapi_headers TSRMLS_DC)
#endif
{
    HttpStream  *stream;
    char        *key, *value;

    stream = (HttpStream*) SG(server_context);

    key = sclone(sapiHeader->header);
    if ((value = strchr(key, ':')) == 0) {
        return -1;
    }
    *value++ = '\0';
    while (!isalnum((uchar) *value) && *value) {
        value++;
    }
#if PHP_MAJOR_VERSION >=5 && PHP_MINOR_VERSION >= 3
    switch(op) {
        case SAPI_HEADER_DELETE_ALL:
            //  FUTURE - not supported
            return 0;

        case SAPI_HEADER_DELETE:
            //  FUTURE - not supported
            return 0;

        case SAPI_HEADER_REPLACE:
            httpSetHeaderString(stream, key, value);
            return SAPI_HEADER_ADD;

        case SAPI_HEADER_ADD:
            httpAppendHeaderString(stream, key, value);
            return SAPI_HEADER_ADD;

        default:
            return 0;
    }
#else
    bool allowMultiple = 0;
    allowMultiple = !sapiHeader->replace;
    if (allowMultiple) {
        httpAppendHeaderString(stream, key, value);
    } else {
        httpSetHeaderString(stream, key, value);
    }
    return SAPI_HEADER_ADD;
#endif
}


static int readBodyData(char *buffer, uint bufsize TSRMLS_DC)
{
    HttpStream  *stream;
    HttpQueue   *q;
    MprBuf      *content;
    ssize       len;

    stream = (HttpStream*) SG(server_context);
    q = stream->readq;
    if (q->first == 0) {
        return 0;
    }
    if ((content = q->first->content) == 0) {
        return 0;
    }
    len = (ssize) min(mprGetBufLength(content), (ssize) bufsize);
    if (len > 0) {
        mprMemcpy(buffer, len, mprGetBufStart(content), len);
        mprAdjustBufStart(content, len);
    }
    return len;
}


static int startup(sapi_module_struct *sapi_module)
{
    return php_module_startup(sapi_module, 0, 0);
}


static int initializePhp(Http *http)
{
    tsrm_startup(128, 1, 0, NULL);

    compiler_globals = (zend_compiler_globals*)  ts_resource(compiler_globals_id);
    executor_globals = (zend_executor_globals*)  ts_resource(executor_globals_id);
    core_globals = (php_core_globals*) ts_resource(core_globals_id);
    sapi_globals = (sapi_globals_struct*) ts_resource(sapi_globals_id);
    tsrm_ls = (void***) ts_resource(0);

#if defined(ME_COM_PHP_INI)
    phpSapiBlock.php_ini_path_override = (char*) ME_COM_PHP_INI;
#else
    phpSapiBlock.php_ini_path_override = (char*) httpGetDefaultRoute(NULL)->home;
#endif
    if (phpSapiBlock.php_ini_path_override) {
        mprLog("info php", 2, "Look for php.ini at %s", phpSapiBlock.php_ini_path_override);
    }
    sapi_startup(&phpSapiBlock);
    if (php_module_startup(&phpSapiBlock, 0, 0) == FAILURE) {
        mprLog("error php", 0, "Cannot initialize");
        return MPR_ERR_CANT_INITIALIZE;
    }
    zend_llist_init(&global_vars, sizeof(char *), 0, 0);

    zend_alter_ini_entry("register_argc_argv", 19, "0", 1, PHP_INI_SYSTEM, PHP_INI_STAGE_ACTIVATE);
    zend_alter_ini_entry("html_errors", 12, "0", 1, PHP_INI_SYSTEM, PHP_INI_STAGE_ACTIVATE);
    zend_alter_ini_entry("implicit_flush", 15, "0", 1, PHP_INI_SYSTEM, PHP_INI_STAGE_ACTIVATE);
    return 0;
}


static int finalizePhp(MprModule *mp)
{
    HttpStage   *stage;

    if ((stage = httpLookupStage("phpHandler")) == 0) {
        return 0;
    }
    if (stage->stageData) {
        phpSapiBlock.shutdown(&phpSapiBlock);
        sapi_shutdown();
#if KEEP
        /* PHP crashes by destroying the EG(persistent_list) twice. Once in zend_shutdown and once in tsrm_shutdown */
        tsrm_shutdown();
#endif
        stage->stageData = 0;
    }
    return 0;
}


static char *mapHyphen(char *str)
{
    char    *cp;

    for (cp = str; *cp; cp++) {
        if (*cp == '-') {
            *cp = '_';
        }
    }
    return str;
}


/*
    Module initialization
 */
PUBLIC int httpPhpInit(Http *http, MprModule *module)
{
    HttpStage     *handler;

    if (module) {
        mprSetModuleFinalizer(module, finalizePhp);
    }
    if ((handler = httpCreateHandler("phpHandler", module)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    handler->open = openPhp;
    handler->ready = readyPhp;
    http->phpHandler = handler;
    return 0;
}

#endif /* PHP_MAJOR_VERSION < 7 */
#endif /* ME_COM_PHP */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
