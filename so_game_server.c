
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
#include "function.h"


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
    ERROR_HELPER(ret, "[PLAYER HANDLER THREAD] Can't close socket for incoming connection");

    printf("[PLAYER HANDLER THREAD] exiting\n");
    pthread_exit(NULL);

}




void * delete_players_thread(void * args) {

    PlayersList * players = (PlayersList *) args;
    int ret;

    while(1) {

        Player * p = players->first;

        ret = sem_wait(&sem_players_list_UDP);
        ERROR_HELPER(ret, "[DELETE PLAYER THREAD] Error sem wait");
        //ret = sem_wait(&sem_players_list_TCP);
        //ERROR_HELPER(ret, "[DELETE PLAYER THREAD] Error sem wait");

        while(p != NULL) {

            if(p->last_packet_timestamp < timestamp - 1200) {

                Player * pd = players->first;
                while(pd != NULL) {

                    pd->new[p->id] = 1;
                    pd = pd->next;
                }

                Vehicle * to_remove = World_getVehicle(&w, p->id);
                int tmpid = p->id;
                World_detachVehicle(&w, to_remove);
                Vehicle_destroy(to_remove);
                free(to_remove);
                player_list_delete(players, p->id);
                free_id[tmpid] = 1;
                players_counter -= 1;
                printf("[DELETE PLAYER THREAD] Player %d quit game, players are now %d\n", tmpid, players->n);
                break;

            }

            if(p != NULL) p = p->next;

        }

        ret = sem_post(&sem_players_list_UDP);
        ERROR_HELPER(ret, "[DELETE PLAYER THREAD] Error sem wait");
        //ret = sem_post(&sem_players_list_TCP);
        //ERROR_HELPER(ret, "[DELETE PLAYER THREAD] Error sem wait");

        usleep(2000000);

    }

}

void * sender_thread_func(void * args) {		//**
	TCP_session_thread_args * arg = (TCP_session_thread_args *) args:
	int socket_udp = arg->socket_udp;
	PlayersList * players = arg->Players;
	char buf[100000];
	PacketHeader packetHeader;
	printf("[UPDATE SENDER THREAD] Start sending updates\n");
	int ret, buf_size;
	while(1) {

		Player * p = players->first;
		ClientUpdate * updates = malloc(sizeof(ClientUpdate)*(players->n));
		int i = 0;
		ret = sem_wait(&sem_players_list_UDP);
		ERROR_HELPER(ret, "[UPDATE SENDER THREAD] Error sem wait");
		while(p != NULL) {
		
		Vehicle * v = World_getVehicle(&w, p->id);

            updates[i].id = p->id;
            updates[i].x = v->x;
            updates[i].y = v->y;
            updates[i++].theta = v->theta;

            p = p->next;

        }


        p = players->first;
        packetHeader.type = WorldUpdate;
        WorldUpdatePacket worldUpdatePacket;
        worldUpdatePacket.header = packetHeader;
        worldUpdatePacket.num_vehicles = players->n;
        worldUpdatePacket.updates = updates;
        buf_size = Packet_serialize(buf, &(worldUpdatePacket.header));

        while(p != NULL) {


        	struct sockaddr_in client_addr = p->client_addr_udp;
                ret = sendto(socket_udp, buf, buf_size, 0, (struct sockaddr*) &client_addr, sizeof(client_addr));
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
                uint16_t client_port = ntohs(client_addr.sin_port); // port number is an unsigned short

                p = p->next;

        }

        ret = sem_post(&sem_players_list_UDP);
        ERROR_HELPER(ret, "[UPDATE SENDER THREAD] Error sem wait");
        free(updates);

        usleep(30000);

    }

    printf("[UPDATE SENDER THREAD] Exiting\n");

    close(socket_udp);
    free(args);
    pthread_exit(NULL);


}	



void * reciver_thread_func(void * args) {		//**

    TCP_session_thread_args * arg = (TCP_session_thread_args *) args;
    int socket_udp = arg->socket_udp;
    PlayersList * players = arg->Players;
    int slen, ret;
    struct sockaddr_in client_addr_udp;
    char buf[100000];
    printf("[UPDATE RECIVER TREAD] Start recivng updates\n");
    while(1) {

        ret = recvfrom(socket_udp, buf, sizeof(buf), 0, (struct sockaddr *) &client_addr_udp, &slen);
        ERROR_HELPER(ret, "[UPDATE RECIVER THREAD] Error recive");
        VehicleUpdatePacket * vehicleUpdatePacket = Packet_deserialize(buf, ret);
        Player * p = player_list_find(players, vehicleUpdatePacket->id);
        Vehicle * v = World_getVehicle(&w, p->id);
        if(v != 0) {
            v->translational_force_update = vehicleUpdatePacket->translational_force;
            v->rotational_force_update = vehicleUpdatePacket->rotational_force;
        }
        timestamp = (timestamp + 1) % 2000000;
        p->last_packet_timestamp = timestamp;
        p->client_addr_udp = client_addr_udp;
        World_update(&w);

    }

    printf("[UPDATE RECIVER THREAD] Exiting\n");

    close(socket_udp);
    free(args);
    pthread_exit(NULL);

}




int main(int argc, char **argv) {

	if (argc<3) {
	   printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
	   exit(-1);
	}
	 
	Image * surfaceTexture = Image_load(argv[1]);
	Image * elevationTexture = Image_load(argv[2]);

    	int ret;
	int socket_desc, client_desc;

	World_init(&w, surfaceTexture, elevationTexture,  0.5, 0.5, 0.5);

	memset(free_id, 1, MAX_PLAYERS*sizeof(char));


	// some fields are required to be filled with 0
	struct sockaddr_in server_addr = {0};

	// we will reuse it for accept()
	int sockaddr_len = sizeof(struct sockaddr_in); 

	// initialize socket for listening
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);

	// we want to accept connections from any interface
	server_addr.sin_addr.s_addr = INADDR_ANY; 
	server_addr.sin_family = AF_INET;
	// don't forget about network byte order!
	server_addr.sin_port = htons(SERVER_PORT); 


	int reuseaddr = 1;		//**
	ret = setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));

	// bind address to socket
	ret = bind(socket_desc, (struct sockaddr *)&server_addr, sockaddr_len);

	// start listening
	ret = listen(socket_desc, MAX_CONN_QUEUE);

	// we allocate client_addr dynamically and initialize it to zero
	struct sockaddr_in *client_addr = calloc(1, sizeof(struct sockaddr_in));


	ret = sem_init(&sem_players_list_UDP, NULL, 1);
	ERROR_HELPER(ret, "Error sem init");


	PlayersList * players = players_list_new();
	int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in si_me = { 0 };
	ERROR_HELPER(udp_socket, "[MAIN] Cannot open udp socket");
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(SERVER_PORT);
	si_me.sin_addr.s_addr = INADDR_ANY;

	int reuseaddr_udp = 1;		//**
	ret = setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_udp, sizeof(reuseaddr_opt_udp));

	ret = bind(udp_socket, &si_me, sizeof(si_me));
	ERROR_HELPER(ret, "[MAIN] Error bind");
	printf("[MAIN] Opened UDP socket\n");

	TCP_session_thread_args * urt_arg = malloc(sizeof(TCP_session_thread_args));
	urt_arg->socket_udp = udp_socket;
	urt_arg->Players = players;
	pthread_t urt;
	pthread_create(&urt, NULL, update_reciver_thread_func, (void *) urt_arg);
	pthread_detach(urt);

	TCP_session_thread_args * ust_arg = malloc(sizeof(TCP_session_thread_args));
	ust_arg->socket_udp = udp_socket;
	ust_arg->Players = players;
	pthread_t ust;
	pthread_create(&ust, NULL, update_sender_thread_func, (void *) ust_arg);
	pthread_detach(ust);

	pthread_t pdt;
	pthread_create(&pdt, NULL, delete_players_thread, (void *) players);
	pthread_detach(pdt);

	
	//Start Listening for new players
  	while(1) {

		while(players_counter == MAX_PLAYERS);

        	client_desc = accept(socket_desc, (struct sockaddr *)client_addr, (socklen_t *)&sockaddr_len);
        	if (client_desc == -1 && errno == EINTR) continue;

        	pthread_t session_init_thread;

        	//Put arguments for the new thread into a buffer
        	TCP_session_thread_args * sit_arg = malloc(sizeof(TCP_session_thread_args));


        	//sit thread arguments
        	sit_arg->socket_desc = client_desc;
        	sit_arg->socket_udp = udp_socket;
        	sit_arg->client_addr = client_addr;
        	sit_arg->SurfaceTexture = surfaceTexture;
        	sit_arg->ElevationTexture = elevationTexture;
      		sit_arg->Players = players;

        	int i = 0;
       		while(free_id[i] == 0) {
            	i++;
            	
		if(i == MAX_PLAYERS) break;
        	}
        	
		if(i == MAX_PLAYERS) {
            	printf("[MAIN] Players limit reached\n");
            	continue;
        	}

        	free_id[i] = 0;
        	sit_arg->id = i;
        	id_counter++;
        	players_counter++;

        	printf("[MAIN] Creating session with ID %d\n", i);

        	ret = pthread_create(&session_init_thread, NULL, TCP_session_thread, (void *)sit_arg);
        	ret = pthread_detach(session_init_thread);


        	client_addr = calloc(1, sizeof(struct sockaddr_in));


    	}

    	printf("[MAIN] EXITING\n");

   	return 0;
}





/*
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
}*/

