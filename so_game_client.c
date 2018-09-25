
#include <GL/glut.h>
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

int id, s;
int current = 1;		//**
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle
PlayersList * players;

sem_t world_started_sem;

    void * reciver(void * args) {

    int slen, ret;
    char buf[1000000];
    struct sockaddr_in si_other = *((struct sockaddr_in *) args);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(si_other.sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(si_other.sin_port); // port number is an unsigned short

    ret = sem_wait(&world_started_sem);
    ERROR_HELPER(ret, "[RECIVER THREAD] Error sem wait");

    printf("[RECIVER THREAD] Start reciving update\n");
    while(1) {

        ret = recvfrom(s, buf, sizeof(buf), 0, &si_other, &slen);
        ERROR_HELPER(ret, "[RECIVER THREAD] Error recive");

        WorldUpdatePacket * wp = Packet_deserialize(buf, ret);
        ClientUpdate * cu = wp->updates;
        int i, n = wp->num_vehicles;

        if(n < current_players) {

            //Some player quit
            current_players = n;
            Player * p = players->first;
            while(p != NULL) {

                for(i = 0; i < n; i++) {
                    if(p->id == cu[i].id) break;
                }

                if(i == n) {

                    Vehicle * to_detach = World_getVehicle(&world, p->id);
                    World_detachVehicle(&world, to_detach);
                    Vehicle_destroy(to_detach);
                    free(to_detach);
                    printf("[RECIVER THREAD] Player ID %d quit the game\n", p->id);
                    player_list_delete(players, p->id);
                    break;
                }

                if(p != NULL) p = p->next;

            }

        }

        for(i = 0; i < n; i++) {
            Vehicle * v = World_getVehicle(&world, cu[i].id);
            if(v == 0 || v == NULL) break;
            v->x = cu[i].x;
            v->y = cu[i].y;
            v->theta = cu[i].theta;


        }

        World_update(&world);
    }
}

void * new_player_listener(void * args) {

    int socket_desc = *((int *) args);
    int recieved_bytes, ret;
    char buf[1000000];

    //Listening for new players
    ret = sem_wait(&world_started_sem);
    ERROR_HELPER(ret, "[NEW PLAYER THREAD] Error sem wait");

    while(1) {

        recv_packet_TCP(socket_desc, buf);
        ImagePacket * imagePacket = Packet_deserialize(buf, ((PacketHeader *) buf)->size);
        Vehicle * newvehicle=(Vehicle*) malloc(sizeof(Vehicle));
        Vehicle_init(newvehicle, &world, imagePacket->id, imagePacket->image);
        World_addVehicle(&world, newvehicle);
        current_players += 1;
        player_list_insert(players, imagePacket->id, NULL);

        printf("[NEW PLAYER THREAD] Player %d added to the world\n", imagePacket->id);

    }

}

void * update_sender(void * args) {

    char buf[1000000];
    int ret;

    struct sockaddr_in * si_other = (struct sockaddr_in *) args;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(si_other->sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(si_other->sin_port); // port number is an unsigned short


    ret = sem_wait(&world_started_sem);
    ERROR_HELPER(ret, "[UPDATER THREAD] Error sem wait");

    printf("[UPDATER THREAD] Start sending update\n");

    while(1) {
        PacketHeader packetHeader;
        VehicleUpdatePacket  vehicleUpdatePacket;
        packetHeader.type = VehicleUpdate;
        vehicleUpdatePacket.header = packetHeader;
        vehicleUpdatePacket.id = id;
        vehicleUpdatePacket.rotational_force = vehicle->rotational_force_update;
        vehicleUpdatePacket.translational_force = vehicle->translational_force_update;
        int buf_size = Packet_serialize(buf, &(vehicleUpdatePacket.header));
        ret = sendto(s, buf, buf_size, 0, (struct sockaddr*) si_other, sizeof(*si_other));
        ERROR_HELPER(ret, "[UPDATER THREAD] Error sending updates");

        usleep(30000);
    }


}


/*
void keyPressed(unsigned char key, int x, int y)
{
  switch(key){
  case 27:
    glutDestroyWindow(window);
    exit(0);
  case ' ':
    vehicle->translational_force_update = 0;
    vehicle->rotational_force_update = 0;
    break;
  case '+':
    viewer.zoom *= 1.1f;
    break;
  case '-':
    viewer.zoom /= 1.1f;
    break;
  case '1':
    viewer.view_type = Inside;
    break;
  case '2':
    viewer.view_type = Outside;
    break;
  case '3':
    viewer.view_type = Global;
    break;
  }
}


void specialInput(int key, int x, int y) {
  switch(key){
  case GLUT_KEY_UP:
    vehicle->translational_force_update += 0.1;
    break;
  case GLUT_KEY_DOWN:
    vehicle->translational_force_update -= 0.1;
    break;
  case GLUT_KEY_LEFT:
    vehicle->rotational_force_update += 0.1;
    break;
  case GLUT_KEY_RIGHT:
    vehicle->rotational_force_update -= 0.1;
    break;
  case GLUT_KEY_PAGE_UP:
    viewer.camera_z+=0.1;
    break;
  case GLUT_KEY_PAGE_DOWN:
    viewer.camera_z-=0.1;
    break;
  }
}


void display(void) {
  WorldViewer_draw(&viewer);
}


void reshape(int width, int height) {
  WorldViewer_reshapeViewport(&viewer, width, height);
}

void idle(void) {
  World_update(&world);
  usleep(30000);
  glutPostRedisplay();

  // decay the commands
  vehicle->translational_force_update *= 0.999;
  vehicle->rotational_force_update *= 0.7;
}
*/



int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  Image* my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
    exit(-1);
  }

  Image* my_texture_for_server;
  // todo: connect to the server
  //   -get ad id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  // these come from the server
  int my_id;

  /*
  int ret, recieved_bytes;

  // variables for handling a socket
  int socket_desc;
  struct sockaddr_in server_addr = {0}; // some fields are required to be filled with 0

  // create a socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(socket_desc, "[MAIN] Could not create socket");

  // set up parameters for the connection
  server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
  server_addr.sin_family      = AF_INET;
  server_addr.sin_port        = htons(SERVER_PORT); // don't forget about network byte order!

  // initiate a connection on the socket
    ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
    ERROR_HELPER(ret, "[MAIN] Could not create connection");

    char buf[1000000];

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(server_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(server_addr.sin_port); // port number is an unsigned short
    printf("[MAIN] Start session with %s on port %d\n", client_ip, client_port);

    PacketHeader packetHeader;
    packetHeader.type = GetId;
    IdPacket * idPacket = malloc(sizeof(IdPacket));
    idPacket->id = -1;
    idPacket->header = packetHeader;
    int buf_size = Packet_serialize(buf, &(idPacket->header));


  */
  Image* map_elevation;
  Image* map_texture;
  Image* my_texture_from_server;

  // construct the world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, my_id, my_texture_from_server);
  World_addVehicle(&world, v);

  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  return 0;
}
