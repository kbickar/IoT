import socket
import traceback

SERVER_IP = '127.0.0.1'
SERVER_PORT = 3334
MAX_UDP = 65507  # Maximum UDP size


class ServerSocket():
    clients = {}
    def __init__(self, ip, port):
        self.server_socket = socket.socket(socket.AF_INET,
                                           socket.SOCK_DGRAM)  # UDP
        self.server_socket.bind((ip, port))

    def start(self):
        while True:
            try:
                data, addr = self.server_socket.recvfrom(MAX_UDP)
                self.handle_message(data, addr)
                #print "%s sent %s" % (addr, data)
            except Exception, err:
                #traceback.print_exc()
                pass # ignore bad messages

    # handle message received as server
    def handle_message(self, data, addr):
        print data


            

if __name__ == '__main__':
    UDP_server_socket = ServerSocket("", SERVER_PORT)
    UDP_server_socket.start()
