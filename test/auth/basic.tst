/*
 *  basic.tst - Basic authentication tests
 */

const HTTP = session["main"]
let http: Http = new Http

http.setCredentials("anybody", "PASSWORD WONT MATTER")
http.get(HTTP + "/index.html")
assert(http.code == 200)

/* Any valid user */
http.setCredentials("joshua", "pass1")
http.get(HTTP + "/basic/basic.html")
assert(http.code == 200)

/* Must be rejected */
http.setCredentials("joshua", "WRONG PASSWORD")
http.get(HTTP + "/basic/basic.html")
assert(http.code == 401)

/* Group access */
http.setCredentials("mary", "pass2")
http.get(HTTP + "/basic/group/group.html")
assert(http.code == 200)

/* Must be rejected - Joshua is not in group */
http.setCredentials("joshua", "pass1")
http.get(HTTP + "/basic/group/group.html")
assert(http.code == 401)

/* User access - Joshua is the required member */
http.setCredentials("joshua", "pass1")
http.get(HTTP + "/basic/user/user.html")
assert(http.code == 200)

/* Must be rejected - Mary is not in group */
http.setCredentials("mary", "pass1")
http.get(HTTP + "/basic/user/user.html")
assert(http.code == 401)

if (test.os == "WIN") {
    /* Case won't matter */
    http.setCredentials("joshua", "pass1")
    http.get(HTTP + "/baSIC/BASic.hTMl")
    assert(http.code == 200)
}
