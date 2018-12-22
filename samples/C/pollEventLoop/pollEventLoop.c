/*!
 *	@file 	pollEventLoop.c
 *	@brief 	Embed the AppWeb server in a simple single-threaded C
 *			application that uses a polling event loop.
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

#if BLD_FEATURE_C_API_MODULE
/************************** Forward Declarations **********************/

static void eventLoop();

/********************************* Code *******************************/

int main(int argc, char** argv)
{
	MaHttp		*http;		/* For the http service inside our app */
	MaServer	*server;	/* For a HTTP server */

	/*
	 *	Initialize the run-time and give our app a name 
	 *	"pollEventLoop"
	 */
	mprCreateMpr("pollEventLoop");

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
	 *	Activate the copy handler. Only needed when linking statically.
 	 */
	mprCopyInit(0);

	/*
	 *	Configure the server based on the directives in 
	 *	pollEventLoop.conf.
	 */
	if (maConfigureServer(server, "pollEventLoop.conf") < 0) {
		fprintf(stderr, 
			"Can't configure the server. Error on line %d\n", 
			maGetConfigErrorLine(server));
		exit(2);
	}

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
	 */
	eventLoop();

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
/*
 *	Sample main event loop using polling. This demonstrates how to 
 *	create a polling event loop.
 */

static void eventLoop()
{
	int		timeout, till;

	/*
	 *	We will nap for 50 milliseconds to avoid busy waiting
	 */
	timeout = 50;

	while (!mprIsExiting()) {

		if (mprRunTimers() > 0) {
			till = 0;
		} else {
			till = mprGetIdleTime();
		}

		/*
		 *	This will run tasks if maxThreads == 0 (single threaded). If 
		 *	multithreaded, the thread pool will run tasks
		 */
		if (mprRunTasks() > 0) {		/* Returns > 0 if more work to do */
			till = 0;
		}

		/*
		 *	Do some work here
		 */
		

		/*
		 *	Now service any pending I/O
		 */
		mprServiceEvents(1, min(timeout, till));
	}
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
