let http: Http = new Http

http.get(":4100/fast-bin/fastProgram")
print(http.status)
http.post(":4100/fast-bin/fastProgram", "Some Data")
print(http.status)
http.post(":4100/fast-bin/fastProgram", "Some Data")
print(http.status)
http.form(":4100/fast-bin/fastProgram", {name: "John", address: "700 Park Ave"})
print(http.status)
http.close()
