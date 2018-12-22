/*
 *  thread.tst - Multithreaded test of the Appweb
 */

if (false && test.multithread) {
    let command = locate("testAppweb") + " --host " + session["host"] + " --name mpr.api.c --iterations 5 " + 
        test.mapVerbosity(-2)

    for each (threadCount in [2, 4, 8, 16]) {
        testCmdNoCapture(command + "--threads " + threadCount)
    }
} else {
    test.skip("Run if multithreaded")
}
