/*
    espHandler.c -- ESP Appweb handler

    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************** Includes **********************************/

#include    "appweb.h"
#include    "esp.h"

#if ME_COM_ESP
/************************************* Code ***********************************/
/*
    EspApp /path/to/some*dir/esp.json
    EspApp [prefix="/uri/prefix"] config="/path/to/esp.json"
 */
static int espAppDirective(MaState *state, cchar *key, cchar *value)
{
    HttpRoute   *route, *saveRoute;
    MprList     *files;
    cchar       *path, *prefix;
    char        *option, *ovalue, *tok;
    int         next, rc;

    rc = 0;
    saveRoute = state->route;

    if (scontains(value, "=")) {
        path = 0;
        prefix = "/";
        for (option = maGetNextArg(sclone(value), &tok); option; option = maGetNextArg(tok, &tok)) {
            option = stok(option, " =\t,", &ovalue);
            ovalue = strim(ovalue, "\"'", MPR_TRIM_BOTH);
            if (smatch(option, "prefix")) {
                prefix = ovalue;
            } else if (smatch(option, "config")) {
                path = ovalue;
            } else {
                path = 0;
                rc = MPR_ERR_BAD_ARGS;
                mprLog("error appweb", 0, "Using deprecated EspApp arguments. Please consult documentation");
            }
        }
        if (path) {
            state->route = route = httpCreateInheritedRoute(state->route);
            route->flags |= HTTP_ROUTE_HOSTED;
            if (espInit(route, prefix, path) < 0) {
                rc = MPR_ERR_CANT_CREATE;
            } else {
                httpFinalizeRoute(route);
            }
        }
    } else {
        files = mprGlobPathFiles(".", value, 0);
        for (ITERATE_ITEMS(files, path, next)) {
            prefix = mprGetPathBase(mprGetPathDir(mprGetAbsPath(path)));
            route = httpCreateInheritedRoute(state->route);
            route->flags |= HTTP_ROUTE_HOSTED;
            if (espInit(route, prefix, path) < 0) {
                rc = MPR_ERR_CANT_CREATE;
                break;
            }
            httpFinalizeRoute(route);
        }
    }
    state->route = saveRoute;
    return rc;
}


#if UNUSED
/*
    EspUpdate on|off
 */
static int espUpdateDirective(MaState *state, cchar *key, cchar *value)
{
    bool    on;

    if (!maTokenize(state, value, "%B", &on)) {
        return MPR_ERR_BAD_SYNTAX;
    }
    httpSetRouteUpdate(state->route, on);
    return 0;
}
#endif


/*
    Loadable module configuration
 */
PUBLIC int httpEspInit(Http *http, MprModule *module)
{
    if (espOpen(module) < 0) {
        return MPR_ERR_CANT_CREATE;
    }
    maAddDirective("EspApp", espAppDirective);
#if UNUSED
    maAddDirective("EspUpdate", espUpdateDirective);
#endif
    return 0;
}
#endif /* ME_COM_ESP */

/*
    Copyright (c) Embedthis Software. All Rights Reserved.
    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.
 */
