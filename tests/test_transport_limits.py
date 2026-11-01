"""Exercise the live receiver's bounds without sending media or touching files."""
import socket
import sys
import time

host="127.0.0.1"
port=int(sys.argv[1]) if len(sys.argv)>1 else 7000

def rejected(headers):
    with socket.create_connection((host,port),timeout=2) as s:
        s.sendall(headers)
        s.settimeout(2)
        try:
            assert s.recv(1)==b"", "malformed control request was accepted"
        except ConnectionResetError:
            pass

for value in (b"999999999",b"-1",b"+1",b"42junk",b"18446744073709551616"):
    rejected(b"POST /pair-verify RTSP/1.0\r\nContent-Length: "+value+b"\r\n\r\n")
rejected(b"POST /pair-verify RTSP/1.0\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n")
rejected(b"GET /info RTSP/1.0\r\nX-Large: "+b"a"*66000)
clients=[socket.create_connection((host,port),timeout=2) for _ in range(8)]
time.sleep(.15)
try:
    with socket.create_connection((host,port),timeout=2) as extra:
        extra.settimeout(2)
        try:
            assert extra.recv(1)==b"", "connection cap was not enforced"
        except ConnectionResetError:
            pass
finally:
    for client in clients: client.close()
time.sleep(.2)
with socket.create_connection((host,port),timeout=2) as s:
    s.sendall(b"GET /info RTSP/1.0\r\nCSeq: 1\r\n\r\n")
    answer=s.recv(16384)
    assert b"200 OK" in answer, "receiver did not recover after rejected traffic"
print("9 control-size, parsing, connection-cap and recovery checks passed")
