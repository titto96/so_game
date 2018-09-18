
// #include <GL/glut.h> // not needed here
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>  // htons()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>
#include <semaphore.h>

#include "common.h"

#include "so_game_protocol.h"
#include "player_list.h"
//#include "function.h"


int num_id = 1, num_players = 0;		//cambiare nome(**)
char free_id[MAX_PLAYERS] = { 1 };		//**
long timestamp = 0;
World w;

sem_t sem_players_list_TCP, sem_players_list_UDP;

typedef struct {

    int socket_desc;
    int socket_udp;
    struct sockaddr_in * client_addr;
    struct sockaddr_in * client_addr_udp;
    Image * SurfaceTexture;
    Image * ElevationTexture;
    int id;
    PlayersList * Players;

} TCP_thread_args;				//**


void * TCP_thread(void * arg) {			//**

    TCP_session_thread_args * args = (TCP_session_thread_args *)arg;
    int socket_udp = args->socket_udp;
    int socket_desc = args->socket_desc;
    int id = args->id;
    struct sockaddr_in * client_addr = args->client_addr;
    int client_addr_len = sizeof(*client_addr);
    Image * surfaceTexture = args->SurfaceTexture;
    Image * elevationTexture = args->ElevationTexture;
    PlayersList * players = args->Players;

    int ret, recv_bytes, send_bytes;
    char buf[1000000];

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client_addr->sin_port); // port number is an unsigned short
    printf("[PLAYER HANDLER THREAD ID %d] Start session with %s on port %d\n", id, client_ip, client_port);

    // read message from client
    while ((recv_bytes = recv(socket_desc, buf, sizeof(buf), 0)) < 0)
    {
        if (errno == EINTR)
            continue;
        ERROR_HELPER(-1, "[PLAYER HANDLER THREAD] Cannot read from socket");
    }
    IdPacket* deserialized_packet = (IdPacket*)Packet_deserialize(buf, sizeof(buf));
    if(deserialized_packet->id == -1) printf("[PLAYER HANDLER THREAD ID %d] Request recived.\n", id);


    //Send id to client
    PacketHeader packetHeader;
    IdPacket idPacket;
    packetHeader.type = GetId;
    idPacket.id = id;
    idPacket.header = packetHeader;
    int buf_size = Packet_serialize(buf, &(idPacket.header));
    while((ret = send(socket_desc, buf, buf_size, 0)) < 0) {
        if (errno == EINTR) continue;
        ERROR_HELPER(-1, "[PLAYER HANDLER THREAD] Cannot write to socket");
    }
    printf("[PLAYER HANDLER THREAD] ID sended %d\n", id);

    //Recive texture from client
    TCP_recive_packet(socket_desc, buf);		//**
    printf("[PLAYER HANDLER THREAD ID %d] Recived texture from player ID %d, bytes %d\n", id, id, ((PacketHeader *) buf)->size);
    ImagePacket * packetTexture = (ImagePacket *) Packet_deserialize(buf, ((PacketHeader *) buf)->size);
    Image * playerTexture = packetTexture->image;

    //Insert player in list
    player_list_insert(players, id, playerTexture);
    Vehicle * vehicle=(Vehicle*) malloc(sizeof(Vehicle));
    Vehicle_init(vehicle, &w, id, playerTexture);
    World_addVehicle(&w, vehicle);

    //Send SurfaceTexture to Client
    packetHeader.type = PostTexture;
    ImagePacket imagePacket;
    imagePacket.header = packetHeader;
    imagePacket.id = 0;
    imagePacket.image = surfaceTexture;
    buf_size = Packet_serialize(buf, &(imagePacket.header));
    while((ret = send(socket_desc, buf, buf_size, 0)) < 0) {
        if (errno == EINTR) continue;
        ERROR_HELPER(-1, "[PLAYER HANDLER THREAD] Cannot write to socket");
    }
    printf("[PLAYER HANDLER THREAD ID %d] SurfaceTexture sent to id %d\n", id, id);


    //Send ElevationTexture to Client
    packetHeader.type = PostElevation;
    imagePacket.header = packetHeader;
    imagePacket.id = 0;
    imagePacket.image = elevationTexture;
    buf_size = Packet_serialize(buf, &(imagePacket.header));
    while((ret = send(socket_desc, buf, buf_size, 0)) < 0) {
        if (errno == EINTR) continue;
        ERROR_HELPER(-1, "[PLAYER HANDLER THREAD] Cannot write to socket");
    }
    printf("[PLAYER HANDLER THREAD ID %d] ElevationTexture sent to ID %d\n", id, id);



    while(1) {

        int i = 0;

        //ret = sem_wait(&sem_players_list_TCP);
        //ERROR_HELPER(ret, "[PLAYER HANDLER THREAD] Error sem wait");

        if(player_list_find(players, id) == NULL) {
            printf("[PLAYER HANDLER THREAD ID %d] Player ID %d quit the game\n", id, id);
            printf("[PLAYER HANDLER THREAD ID %d] Exiting\n", id);
            close(socket_desc);
            pthread_exit(NULL);
        }

        Player * p = players->first;
        ImagePacket imagePacket;

        while(p != NULL) {


            if(p->new[id] == 1 && p->id != id) {


                p->new[id] = 0;
                packetHeader.type = PostTexture;
                imagePacket.id = p->id;
                imagePacket.image = p->texture;
                imagePacket.header = packetHeader;
                buf_size = Packet_serialize(buf, &(imagePacket.header));
                printf("[PLAYER HANDLER THREAD ID %d] Sending texture of %d to %d\n", id, p->id, id);
                while((ret = send(socket_desc, buf, buf_size, MSG_NOSIGNAL)) < 0) {
                    if(ret <= 0) {
                        printf("[PLAYER HANDLER THREAD ID %d] Player %d quit the game\n", id, id);
                        printf("[PLAYER HANDLER THREAD ID %d] Exiting\n", id, id);
                        close(socket_desc);
                        pthread_exit(NULL);
                    }
                    if(errno == EINTR) continue;
                }

            }
            p = p->next;

        }
        //ret = sem_post(&sem_players_list_TCP);
        //ERROR_HELPER(ret, "[PLAYER HANDLER THREAD] Error sem wait");


        usleep(2000000);


    }

    // close socket
    ret = close(socket_desc);
    ERROR_HELPER(ret, "[PLAYER HANDLER THREAD] Cannot close socket for incoming connection");

    printf("[PLAYER HANDLER THREAD] exiting\n");
    pthread_exit(NULL);

}





int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    exit(-1);
  }
  char* elevation_filename=argv[1];
  char* texture_filename=argv[2];
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }


  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ", vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  // not needed here
  //   // construct the world
  // World_init(&world, surface_elevation, surface_texture,  0.5, 0.5, 0.5);

  // // create a vehicle
  // vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  // Vehicle_init(vehicle, &world, 0, vehicle_texture);

  // // add it to the world
  // World_addVehicle(&world, vehicle);


  
  // // initialize GL
  // glutInit(&argc, argv);
  // glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
  // glutCreateWindow("main");

  // // set the callbacks
  // glutDisplayFunc(display);
  // glutIdleFunc(idle);
  // glutSpecialFunc(specialInput);
  // glutKeyboardFunc(keyPressed);
  // glutReshapeFunc(reshape);
  
  // WorldViewer_init(&viewer, &world, vehicle);

  
  // // run the main GL loop
  // glutMainLoop();

  // // check out the images not needed anymore
  // Image_free(vehicle_texture);
  // Image_free(surface_texture);
  // Image_free(surface_elevation);

  // // cleanup
  // World_destroy(&world);
  return 0;             
}
