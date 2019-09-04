import socket
import traceback
import datetime

SERVER_IP = '127.0.0.1'
SERVER_PORT = 3347
MAX_UDP = 65507  # Maximum UDP size

f = open("debug.log", "w+")

class ServerSocket():
    clients = {}
    def __init__(self, ip, port):
        self.server_socket = socket.socket(socket.AF_INET,
                                           socket.SOCK_STREAM)  # TCP
        self.server_socket.bind((ip, port))

    def start(self):
        # Listen for incoming connections
        self.server_socket.listen(1)
        while True:
            # Wait for a connection
            print  'waiting for a connection'
            connection, client_address = self.server_socket.accept()
            try:
                print 'connection from', client_address
                t = datetime.datetime.now().strftime('%y-%m-%d-%H.%M.%S')
                f = open("msg"+t+".jpg", "wb")

                # Receive the data in small chunks and retransmit it
                while True:
                    data = connection.recv(64)
                    if data:
                        #print data,
                        f.write(data)
                    else:
                        print "done"
                        break
                    
            finally:
                # Clean up the connection
                connection.close()
                f.close()
            

if __name__ == '__main__':
    UDP_server_socket = ServerSocket("", SERVER_PORT)
    UDP_server_socket.start()
