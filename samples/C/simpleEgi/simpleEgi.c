/*!
 *	@file 	simpleEgi.c
 *	@brief 	Demonstrate the use of the Embedded Gateway Interface (EGI) 
 *			in a simple multi-threaded application.
 */
/*************************************************************************/
/*
 *	Copyright (c) Embedthis Software LLC, 2003-2009. All Rights Reserved.
 *	The latest version of this code is available at http://www.mbedthis.com
 *
 *	This software is open source; you can redistribute it and/or modify it 
 *	under the terms of the GNU General Public License as published by the 
 *	Free Software Foundation; either version 2 of the License, or (at your 
 *	option) any later version.
 *
 *	This program is distributed WITHOUT ANY WARRANTY; without even the 
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *	See the GNU General Public License for more details at:
 *	http://www.mbedthis.com/downloads/gplLicense.html
 *	
 *	This General Public License does NOT permit incorporating this software 
 *	into proprietary programs. If you are unable to comply with the GPL, a 
 *	commercial license for this software and support services are available
 *	from Embedthis Software at http://www.mbedthis.com
 */
/******************************* Includes *****************************/

#define		UNSAFE_FUNCTIONS_OK 1

#include	"appweb/appweb.h"

/********************************* Code *******************************/
#if BLD_FEATURE_C_API_MODULE
/*
 *	This method is run when the EGI form is called from the web
 *	page. Rq is the request context. URI is the bare URL minus query.
 *	Query is the string after a "?" in the URL. Post data is posted
 *	HTTP form data.
 */

static void myEgi(MaRequest *rq, char *script, char *uri, char *query, 
	char *postData, int postLen)
{
	maWriteStr(rq, "<HTML><TITLE>simpleEgi</TITLE><BODY>\r\n");
	maWriteFmt(rq, "<p>Name: %s</p>\n", 
		maGetVar(rq, MA_FORM_OBJ, "name", "-"));
	maWriteFmt(rq, "<p>Address: %s</p>\n", 
		maGetVar(rq, MA_FORM_OBJ, "address", "-"));
	maWriteFmt(rq, "</BODY></HTML>\r\n");

#if POSSIBLE
	//
	//	Useful things to do in egi forms
	//
	maSetResponseCode(rq, 200);
	maSetContentType(rq, "text/html");
	maSetHeaderFlags(rq, MPR_HTTP_DONT_CACHE);
	maRedirect(rq, "/myURl");
	maRequestError(rq, 409, "My message : %d", 5);
#endif
}

/**********************************************************************/

int main(int argc, char** argv)
{
	MaHttp		*http;		/* For the http service inside our app */
	MaServer	*server;	/* For a HTTP server */

	/*
	 *	Initialize the run-time and give our app a name "simpleEgi"
	 */
	mprCreateMpr("simpleEgi");

#if BLD_FEATURE_LOG
	/*
	 *	Do the following two statements only if you want debug trace
	 */
	mprAddLogFileListener();
	mprSetLogSpec("stdout:4");
#endif

	/*
	 *	Start run-time services
	 */
	mprStartMpr(0);

	/*
	 *	Create the HTTP and server objects. Give the server a name 
	 *	"default" and define "." as the default serverRoot, ie. the 
	 *	directory with the server configuration files.
	 */
	http = maCreateHttp();
	server = maCreateServer(http, "default", ".");
	
	/*
	 *	Activate the handlers. Only needed when linking statically.
	 */
	mprEgiInit(0);
	mprCopyInit(0);

	/*
	 *	Configure the server based on the directives in 
	 *	simpleEgi.conf.
	 */
	if (maConfigureServer(server, "simpleEgi.conf") < 0) {
		fprintf(stderr, 
			"Can't configure the server. Error on line %d\n", 
			maGetConfigErrorLine(server));
		exit(2);
	}

	/*
	 *	Define our EGI form
	 */
	maDefineEgiForm("/myEgi.egi", myEgi);
	
	/*
	 *	Start serving pages. After this we are live.
	 */
	if (maStartServers(http) < 0) {
		fprintf(stderr, "Can't start the server\n");
		exit(2);
	}

	/*
	 *	Service events. This call will block until the server is exited
	 *	Call mprTerminate() at any time to instruct the server to exit.
	 *	The -1 is a timeout on the block. Useful if you use 
	 *	MPR_LOOP_ONCE and have a polling event loop.
	 */
	mprServiceEvents(MPR_LOOP_FOREVER, -1);

	/*
	 *	Stop all HTTP services
	 */
	maStopServers(http);

	/*
	 *	Delete the server and http objects
	 */
	maDeleteServer(server);
	maDeleteHttp(http);

	/*
	 *	Stop and delete the run-time services
	 */
	mprStopMpr();
	mprDeleteMpr();

	return 0;
}

/**********************************************************************/
#else /* BLD_FEATURE_C_API_MODULE */

int main()
{
	fprintf(stderr, "BLD_FEATURE_C_API_MODULE is not defined in config.h\n");
	exit(2);
}
#endif /* BLD_FEATURE_C_API_MODULE */

//
// Local variables:
// tab-width: 4
// c-basic-offset: 4
// End:
// vim: sw=4 ts=4 
//
