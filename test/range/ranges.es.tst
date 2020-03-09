/*
    ranges.tst - Ranged get tests
 */

const HTTP = tget('TM_HTTP') || "127.0.0.1:4100"
let http: Http = new Http

/*
//  Get first 5 bytes
http.setHeader("Range", "bytes=0-4")
http.get(HTTP + "/100K.txt")
ttrue(http.status == 206)
ttrue(http.response == "01234")
http.close()


//  Get last 5 bytes
http.setHeader("Range", "bytes=-5")
http.get(HTTP + "/100K.txt")
ttrue(http.status == 206)
ttrue(http.response.trim() == "MENT")
http.close()
*/


//  Get from specific position till the end
http.setHeader("Range", "bytes=117000-")
http.get(HTTP + "/100K.txt")
print("STATUS", http.status)
ttrue(http.status == 206)
ttrue(http.response.trim() == "END OF DOCUMENT")
http.close()


/*
//  Multiple ranges
http.setHeader("Range", "bytes=0-5,25-30,-5")
http.get(HTTP + "/100K.txt")
ttrue(http.status == 206)
ttrue(http.response.contains("Content-Range: bytes 0-5/117016"))
ttrue(http.response.contains("Content-Range: bytes 25-30/117016"))
ttrue(http.response.contains("Content-Range: bytes 117011-117015/117016"))
ttrue(http.response.contains("012345"))
ttrue(http.response.contains("567890"))
ttrue(http.response.contains("MENT"))
http.close()
*/
