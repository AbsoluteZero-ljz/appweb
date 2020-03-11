/*
    missing.tst - Add extension
 */

const HTTP = tget('TM_HTTP') || "127.0.0.1:4100"
let http: Http = new Http

if (thas('ME_FAST')) {
    http.get(HTTP + "/route/missing-ext/index")
    ttrue(http.status == 200)
    ttrue(http.response.contains("Hello PHP World"))
    http.close()
} else {
    tskip("php not enabled")
}
