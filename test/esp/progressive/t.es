let s = new Socket
s.connect('127.0.0.1:4100')
s.write('GET /progressive HTTP/1.1\r\n\r\n')
print("WRITTEN")
let response = new ByteArray
while ((n = s.read(response, -1)) != null) {
    // print('RESPONSE', response)
}
s.close()
