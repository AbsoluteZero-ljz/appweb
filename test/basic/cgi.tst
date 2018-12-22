/*
 *  cgi.tst - CGI tests
 */

const HTTP = session["main"]
let http: Http = new Http

/* Suport routines */

function contains(pat): Void {
    if (!http.response.contains(pat)) {
        print("Response is missing: " + pat)
        print("Response:\n" + http.response)
    }
    assert(http.response.contains(pat))
}

function keyword(pat: String): String {
    pat.replace(/\//, "\\/").replace(/\[/, "\\[")
    let reg = RegExp(".*" + pat + "=([^<]*).*", "s")
    return http.response.replace(reg, "$1")
}

function match(key: String, value: String): Void {
    if (keyword(key) != value) {
        print("MISMATCH key = " + key + " expected = " + value + " actual = " + keyword(key))
    }
    assert(keyword(key) == value)
}


/* Tests */

function forms() {
    //  Test various forms to invoke cgi programs
    http.get(HTTP + "/cgi-bin/cgiProgram")
    assert(http.code == 200)
    contains("cgiProgram: Output")

    http.get(HTTP + "/cgiProgram.cgi")
    assert(http.code == 200)
    contains("cgiProgram: Output")

    if (test.os == "WIN") {
        http.get(HTTP + "/cgi-bin/cgiProgram.exe")
        assert(http.code == 200)
        contains("cgiProgram: Output")

        //  Test test.bat (should auto-add .bat)
        http.get(HTTP + "/cgi-bin/test")
        assert(http.code == 200)
        contains("cgiProgram: Output")

        //  Test test.bat
        http.get(HTTP + "/cgi-bin/test.bat")
        assert(http.code == 200)
        contains("cgiProgram: Output")
    }

    # Test #!cgiProgram
    http.get(HTTP + "/cgi-bin/testScript")
    assert(http.code == 200)
    contains("cgiProgram: Output")
}

function alias() {
    http.get(HTTP + "/MyScripts/cgiProgram")
    assert(http.code == 200)
    contains("cgiProgram: Output")
    match("SCRIPT_NAME", "/MyScripts/cgiProgram")
    match("PATH_INFO", "")
    match("PATH_TRANSLATED", "")

    http.get(HTTP + "/YourScripts/cgiProgram.cgi")
    assert(http.code == 200)
    contains("cgiProgram: Output")
}

function extraPath() {
    http.get(HTTP + "/cgiProgram.cgi")
    assert(http.code == 200)
    match("PATH_INFO", "")
    match("PATH_TRANSLATED", "")

    http.get(HTTP + "/cgiProgram.cgi/extra/path")
    match("PATH_INFO", "/extra/path")
    let scriptFilename = keyword("SCRIPT_FILENAME")
    let path = Path(scriptFilename).dirname.join("extra/path")
    let translated = keyword("PATH_TRANSLATED")
    assert(path == translated)
}

function query() {
    http.get(HTTP + "/cgiProgram.cgi/extra/path?a=b&c=d&e=f")
    match("SCRIPT_NAME", "/cgiProgram.cgi")
    match("PATH_INFO", "/extra/path")
    contains("QVAR a=b")
    contains("QVAR c=d")

    http.get(HTTP + "/cgiProgram.cgi?a+b+c")
    match("QUERY_STRING", "a+b+c")
    contains("QVAR a b c")

    //
    //  Query string vars should not be turned into variables for GETs
    //
    http.get(HTTP + "/cgi-bin/cgiProgram/extra/path?var1=a+a&var2=b%20b&var3=c")
    match("SCRIPT_NAME", "/cgi-bin/cgiProgram")
    match("QUERY_STRING", "var1=a+a&var2=b%20b&var3=c")
    match("QVAR var1", "a a")
    match("QVAR var2", "b b")
    match("QVAR var3", "c")

    //
    //  Post data should be turned into variables
    //
    http.form(HTTP + "/cgi-bin/cgiProgram/extra/path?var1=a+a&var2=b%20b&var3=c", 
        { name: "Peter", address: "777+Mulberry+Lane" })
    match("QUERY_STRING", "var1=a+a&var2=b%20b&var3=c")
    match("QVAR var1", "a a")
    match("QVAR var2", "b b")
    match("QVAR var3", "c")
    match("PVAR name", "Peter")
    match("PVAR address", "777 Mulberry Lane")
}

function args() {
    //
    //  Note: args are split at '+' characters and are then shell character encoded. Typical use is "?a+b+c+d
    //
    http.get(HTTP + "/cgi-bin/cgiProgram")
    assert(keyword("ARG[0]").contains("cgiProgram"))
    assert(!http.response.contains("ARG[1]"))

    http.get(HTTP + "/cgi-bin/cgiProgram/extra/path")
    assert(keyword("ARG[0]").contains("cgiProgram"))
    assert(!http.response.contains("ARG[1]"))

    http.get(HTTP + "/cgi-bin/cgiProgram/extra/path?a+b+c")
    match("QUERY_STRING", "a+b+c")
    assert(keyword("ARG[0]").contains("cgiProgram"))
    match("ARG.1.", "a")
    match("ARG.2.", "b")
    match("ARG.3.", "c")

    http.get(HTTP + "/cgi-bin/cgiProgram?var1=a+a&var2=b%20b&var3=c")
    match("QUERY_STRING", "var1=a+a&var2=b%20b&var3=c")
}

function encoding() {
    http.get(HTTP + "/cgi-bin/cgi%20Program/extra%20long/a/../path|/a/..?var%201=value%201")
    match("QUERY_STRING", "var%201=value%201")
    match("SCRIPT_NAME", "/cgi-bin/cgi Program")
    match("QVAR var 1", "value 1")
    match("PATH_INFO", "/extra long/path|/")

    let scriptFilename = keyword("SCRIPT_FILENAME")
    let path = Path(scriptFilename).dirname.join("extra long/path|")
    let translated = keyword("PATH_TRANSLATED")
    assert(path == translated)
}

function status() {
    let http = new Http
    http.addHeader("SWITCHES", "-s%20711")
    http.get(HTTP + "/cgi-bin/cgiProgram")
    assert(http.code == 711)
    http.close()
}

function location() {
    let http = new Http
    http.addHeader("SWITCHES", "-l%20/index.html")
    http.followRedirects = false
    http.get(HTTP + "/cgi-bin/cgiProgram")
    assert(http.code == 302)
    http.close()
}

//  Non-parsed header
function nph() {
    let http = new Http
    http.addHeader("SWITCHES", "-n")
    http.get(HTTP + "/cgi-bin/nph-cgiProgram")
    assert(http.code == 200)
    assert(http.response.startsWith("HTTP/1.0"))
    assert(http.response.contains("X-CGI-CustomHeader: Any value at all"))
}

function quoting() {
    http.get(HTTP + "/cgi-bin/testScript?a+b+c")
    match("QUERY_STRING", "a+b+c")
    match("QVAR a b c", "")

    http.get(HTTP + "/cgi-bin/testScript?a=1&b=2&c=3")
    match("QUERY_STRING", "a=1&b=2&c=3")
    match("QVAR a", "1")
    match("QVAR b", "2")
    match("QVAR c", "3")

    http.get(HTTP + "/cgi-bin/testScript?a%20a=1%201+b%20b=2%202")
    match("QUERY_STRING", "a%20a=1%201+b%20b=2%202")
    match("QVAR a a", "1 1 b b=2 2")

    http.get(HTTP + "/cgi-bin/testScript?a%20a=1%201&b%20b=2%202")
    match("QUERY_STRING", "a%20a=1%201&b%20b=2%202")
    match("QVAR a a", "1 1")
    match("QVAR b b", "2 2")

    http.get(HTTP + "/cgi-bin/testScript?a|b+c>d+e?f+g>h+i'j+k\"l+m%20n")
    match("ARG.2.", "a\\|b")
    match("ARG.3.", "c\\>d")
    match("ARG.4.", "e\\?f")
    match("ARG.5.", "g\\>h")
    match("ARG.6.", "i\\'j")

    if (test.os == "WIN") {
        match("ARG.7.", "k\"l")
    } else {
        match("ARG.7.", "k\\\"l")
    }
    match("ARG.8.", "m n")
    match("QUERY_STRING", "a|b+c>d+e?f+g>h+i'j+k\"l+m%20n")
    assert(http.response.contains("QVAR a|b c>d e?f g>h i'j k\"l m n"))
}

forms()
alias()
extraPath()
query()
args()
encoding()
status()
location()
nph()
quoting()
