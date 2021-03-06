#include "function.h"
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "common.h"


void TCP_recive_packet(int socket_desc, char buf[]){
	
    int recived_bytes = 0, left_bytes = 8, packetsize, ret;
    char * tmp = buf;
    while(left_bytes > 0) {
        while((ret = recv(socket_desc, tmp + recived_bytes, left_bytes, 0)) < 0) {
            if (errno == EINTR)
                continue;
            ERROR_HELPER(-1, "Can't read from socket");
        }
        left_bytes -= ret;
        recived_bytes += ret;
    }

    //PacketHeader recived

    left_bytes = ((PacketHeader *) buf)->size - 8;
    recived_bytes = 0;
    while(left_bytes > 0) {
        while((ret = recv(socket_desc, tmp + 8 + recived_bytes, left_bytes, 0)) < 0) {
            if (errno == EINTR)
                continue;
            ERROR_HELPER(-1, "Can't read from socket");
        }
        left_bytes -= ret;
        recived_bytes += ret;
    }

    //Full packet recived

}
