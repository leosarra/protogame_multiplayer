#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>  // htons() and inet_addr()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>
#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "client_op.h"
#include "server_op.h"
#include "so_game_protocol.h"
#include "linked_list.h"

pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
int connectivity=1;
int exchangeUpdate=1;
int cleanGarbage=1;
int hasUsers=0;
ListHead* users;
uint16_t  port_number_no;

typedef struct {
    int client_desc;
    Image* elevation_texture;
    Image* vehicle_texture;
    Image* surface_texture;
} tcp_args;

typedef struct {
    tcp_args args;
    int id_client; //I will use the socket as ID
} auth_args;

void handle_signal(int signal){
    // Find out which signal we're handling
    switch (signal) {
        case SIGHUP:
            break;
        case SIGINT:
            connectivity=0;
            exchangeUpdate=0;
            cleanGarbage=0;
            break;
        default:
            fprintf(stderr, "Caught wrong signal: %d\n", signal);
            return;
    }
}

void sendDisconnect(int socket_udp, struct sockaddr_in client_addr){
    char buf_send[BUFFERSIZE];
    PacketHeader ph;
    ph.type=PostDisconnect;
    IdPacket* ip=(IdPacket*)malloc(sizeof(IdPacket));
    ip->id=-1;
    ip->header=ph;
    int size=Packet_serialize(buf_send,&(ip->header));
    int ret=sendto(socket_udp, buf_send, size, 0, (struct sockaddr*) &client_addr, (socklen_t) sizeof(client_addr));
    debug_print("[UDP_Receiver] Sent PostDisconnect packet of %d bytes to unrecognized user \n",ret);
}


int UDP_Handler(int socket_udp,char* buf_rcv,struct sockaddr_in client_addr){
    PacketHeader* ph=(PacketHeader*)buf_rcv;
    switch(ph->type){
        case(VehicleUpdate):{
            VehicleUpdatePacket* vup=(VehicleUpdatePacket*)Packet_deserialize(buf_rcv, ph->size);
            pthread_mutex_lock(&mutex);
            ListItem* client = List_find_by_id(users, vup->id);
            if(client == NULL) {
                debug_print("[UDP_Handler] Can't find the user to apply the update \n");
                Packet_free(&vup->header);
                printf("Non ho trovato l'ID %d \n",vup->id);
                sendDisconnect(socket_udp,client_addr);
                pthread_mutex_unlock(&mutex);
                return -1;
            }
            client->x=vup->x;
            client->y=vup->y;
            client->theta=vup->theta;
            client->rotational_force=vup->rotational_force;
            client->translational_force=vup->translational_force;
            client->user_addr=client_addr;
            client->isAddrReady=1;
            client->last_update_time=vup->time;
            pthread_mutex_unlock(&mutex);
            Packet_free(&vup->header);
            fprintf(stdout,"[UDP_Receiver] Applied VehicleUpdatePacket of %d bytes from id %d... \n",ph->size,vup->id);
            return 0;
        }
        default: return -1;

    }
}

int TCP_Handler(int socket_desc,char* buf_rcv,Image* texture_map,Image* elevation_map,int id,int* isActive){
    PacketHeader* header=(PacketHeader*)buf_rcv;
    if(header->type==GetId){
        char buf_send[BUFFERSIZE];
        IdPacket* response=(IdPacket*)malloc(sizeof(IdPacket));
        PacketHeader ph;
        ph.type=GetId;
        response->header=ph;
        response->id=id;
        int msg_len=Packet_serialize(buf_send,&(response->header));
        debug_print("[Send ID] bytes written in the buffer: %d\n", msg_len);
        int ret=0;
        int bytes_sent=0;
        while(bytes_sent<msg_len){
			ret=send(socket_desc,buf_send+bytes_sent,msg_len-bytes_sent,0);
			if (ret==-1 && errno==EINTR) continue;
			ERROR_HELPER(ret,"Errore invio");
			if (ret==0) break;
			bytes_sent+=ret;
        }
        Packet_free(&(response->header));
        debug_print("[Send ID] Sent %d bytes \n",bytes_sent);
        return 0;
    }
    else if(header->type==GetTexture){
        debug_print("Sono qui");
        char buf_send[BUFFERSIZE];
        ImagePacket* image_request = (ImagePacket*)buf_rcv;
        printf("ID DELLA RICHIESTA IMMAGINE %d \n",image_request->id);
        if(image_request->id>0){
            debug_print("Dentro l'if");
            char buf_send[BUFFERSIZE];
            ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
            PacketHeader im_head;
            im_head.type=PostTexture;
            pthread_mutex_lock(&mutex);
            ListItem* el=List_find_by_id(users,image_request->id);
            if (el==NULL) {
                pthread_mutex_unlock(&mutex);
                return -1;
            }
            image_packet->image=el->v_texture;
            pthread_mutex_unlock(&mutex);
            image_packet->header=im_head;
            int msg_len= Packet_serialize(buf_send, &image_packet->header);
            debug_print("[Send Vehicle Texture] bytes written in the buffer: %d\n", msg_len);
            int bytes_sent=0;
            int ret=0;
            while(bytes_sent<msg_len){
                ret=send(socket_desc,buf_send+bytes_sent,msg_len-bytes_sent,0);
                if (ret==-1 && errno==EINTR) continue;
                ERROR_HELPER(ret,"Can't send map texture over TCP");
                bytes_sent+=ret;
            }

            free(image_packet);
            debug_print("[Send Vehicle Texture] Sent %d bytes \n",bytes_sent);
            return 0;
        }
        ImagePacket* image_packet =(ImagePacket*)malloc(sizeof(ImagePacket));
        PacketHeader im_head;
        im_head.type=PostTexture;
        image_packet->image=texture_map;
        image_packet->header=im_head;
        int msg_len= Packet_serialize(buf_send, &image_packet->header);
        debug_print("[Send Map Texture] bytes written in the buffer: %d\n", msg_len);
        int bytes_sent=0;
        int ret=0;
        while(bytes_sent<msg_len){
			ret=send(socket_desc,buf_send+bytes_sent,msg_len-bytes_sent,0);
			if (ret==-1 && errno==EINTR) continue;
			ERROR_HELPER(ret,"Can't send map texture over TCP");
			if (ret==0) break;
			bytes_sent+=ret;
            }
        free(image_packet);
        debug_print("[Send Map Texture] Sent %d bytes \n",bytes_sent);
        return 0;
    }

    else if(header->type==GetElevation){
        char buf_send[BUFFERSIZE];
        ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
        PacketHeader im_head;
        im_head.type=PostElevation;
        image_packet->image=elevation_map;
        image_packet->header=im_head;
        int msg_len= Packet_serialize(buf_send, &image_packet->header);
        printf("[Send Map Elevation] bytes written in the buffer: %d\n", msg_len);
        int bytes_sent=0;
        int ret=0;
        while(bytes_sent<msg_len){
			ret=send(socket_desc,buf_send+bytes_sent,msg_len-bytes_sent,0);
			if (ret==-1 && errno==EINTR) continue;
			ERROR_HELPER(ret,"Can't send map elevation over TCP");
			if (ret==0) break;
			bytes_sent+=ret;
            }
        free(image_packet);
        debug_print("[Send Map Elevation] Sent %d bytes \n",bytes_sent);
        return 0;
    }
    else if(header->type==PostTexture){
        ImagePacket* deserialized_packet = (ImagePacket*)Packet_deserialize(buf_rcv, header->size);
        Image* user_texture=deserialized_packet->image;

        pthread_mutex_lock(&mutex);
        ListItem* user= List_find_by_id(users,id);
        if (user==NULL){
            debug_print("[Set Texture] User not found \n");
            pthread_mutex_unlock(&mutex);
            return -1;
        }
        user->v_texture=user_texture;
        pthread_mutex_unlock(&mutex);
        debug_print("[Set Texture] Vehicle texture applied to user with id %d \n",id);
        free(deserialized_packet);
        return 0;
    }
    else if(header->type==PostDisconnect){
        debug_print("[Notify Disconnect] User disconnected. Cleaning resources...");
        *isActive=0;
        return 0;
    }

    else {
        *isActive=0;
        printf("[TCP Handler] Unknown packet. Cleaning resources...\n");
        return -1;
    }
}

void* tcp_flow(void* args){
    tcp_args* arg=(tcp_args*)args;
    int sock_fd=arg->client_desc;
    pthread_mutex_lock(&mutex);
    ListItem* user=malloc(sizeof(ListItem));
    user->v_texture = NULL;
    user->creation_time=time(NULL);
    user->id=sock_fd;
    user->prev_x=-1;
    user->prev_y=-1;
    List_insert(users, 0, user);
    hasUsers=1;
    pthread_mutex_unlock(&mutex);

    int ph_len=sizeof(PacketHeader);
    int isActive=1;
    int count=0;
    while (connectivity && isActive){
        int msg_len=0;
        char buf_rcv[BUFFERSIZE];
        while(msg_len<ph_len){
            int ret=recv(sock_fd, buf_rcv+msg_len, ph_len-msg_len, 0);
            if (ret==-1 && errno == EINTR) continue;
            ERROR_HELPER(msg_len, "Cannot read from socket");
            msg_len+=ret;
            }
        PacketHeader* header=(PacketHeader*)buf_rcv;
        int size_remaining=header->size-ph_len;
        msg_len=0;
        while(msg_len<size_remaining){
            int ret=recv(sock_fd, buf_rcv+msg_len+ph_len, size_remaining-msg_len, 0);
            if (ret==-1 && errno == EINTR) continue;
            ERROR_HELPER(msg_len, "Cannot read from socket");
            msg_len+=ret;
        }
        //printf("Read %d bytes da socket TCP \n",msg_len+ph_len);
        int ret=TCP_Handler(sock_fd,buf_rcv,arg->surface_texture,arg->elevation_texture,arg->client_desc,&isActive);
        ERROR_HELPER(ret,"TCP Handler failed");
        count++;
    }
    pthread_mutex_lock(&mutex);
    ListItem* el=List_detach(users,user);
    if(el==NULL) goto END;
    Image* user_texture=el->v_texture;
    if(user_texture!=NULL) Image_free(user_texture);
    free(el);
    END:pthread_mutex_unlock(&mutex);
    fprintf(stdout,"Done \n");
    pthread_exit(NULL);
}

void* udp_receiver(void* args){
    int socket_udp=*(int*)args;
    while(connectivity && exchangeUpdate){
        if(!hasUsers){
            sleep(1);
            continue;
        }

        char buf_recv[BUFFERSIZE];
        struct sockaddr_in client_addr = {0};
        socklen_t addrlen= sizeof(struct sockaddr_in);
        int bytes_read=recvfrom(socket_udp,buf_recv,BUFFERSIZE,0, (struct sockaddr*)&client_addr,&addrlen);
        if(bytes_read==-1)  continue;
		if(bytes_read == 0) continue;
		int ret = UDP_Handler(socket_udp,buf_recv,client_addr);
        if (ret==-1) debug_print("[UDP_Receiver] UDP Handler couldn't manage to apply the VehicleUpdate \n");
    }
    pthread_exit(NULL);
}

void* udp_sender(void* args){
    int socket_udp=*(int*)args;
    while(connectivity && exchangeUpdate){
        if(!hasUsers){
            sleep(1);
            continue;
        }
        char buf_send[BUFFERSIZE];

        PacketHeader ph;
        ph.type=WorldUpdate;
        WorldUpdatePacket* wup=(WorldUpdatePacket*)malloc(sizeof(WorldUpdatePacket));
        wup->header=ph;
        pthread_mutex_lock(&mutex);
        int n;
        ListItem* client= users->first;
        for(n=0;client!=NULL;client=client->next){
            if(client->isAddrReady) n++;
        }
        wup->num_vehicles=n;
        fprintf(stdout,"[UDP_Sender] Creating WorldUpdatePacket containing info about %d users \n",n);
        wup->updates=(ClientUpdate*)malloc(sizeof(ClientUpdate)*n);
        client= users->first;
        for(int i=0;client!=NULL;i++){
            if(!(client->isAddrReady)) continue;
            ClientUpdate* cup= &(wup->updates[i]);
            cup->y=client->y;
            cup->x=client->x;
            cup->theta=client->theta;
            cup->id=client->id;
            cup->rotational_force=client->rotational_force;
            cup->translational_force=client->translational_force;
            printf("--- Veicolo con id: %d x: %f y:%f z:%f rf:%f tf:%f --- \n",cup->id,cup->x,cup->y,cup->theta,cup->rotational_force,cup->translational_force);
            client = client->next;
        }

        int size=Packet_serialize(buf_send,&wup->header);
        if(size==0 || size==-1){
            pthread_mutex_unlock(&mutex);
            sleep(1);
            continue;
			}
        client=users->first;
        while(client!=NULL){
            if(client->isAddrReady==1){
                    int ret = sendto(socket_udp, buf_send, size, 0, (struct sockaddr*) &client->user_addr, (socklen_t) sizeof(client->user_addr));
                    debug_print("[UDP_Send] Sent WorldUpdate of %d bytes to client with id %d \n",ret,client->id);
                }
            client=client->next;
            }
        fprintf(stdout,"[UDP_Send] WorldUpdatePacket sent to each client \n");
        pthread_mutex_unlock(&mutex);
        sleep(1);
    }
    pthread_exit(NULL);
}

void* garbage_collector(void* args){
    debug_print("[GC] Garbage collector initialized \n");
    int socket_udp=*(int*)args;
    while(cleanGarbage){
        if(hasUsers==0) goto END;
        pthread_mutex_lock(&mutex);
        ListItem* client=users->first;
        long current_time=(long)time(NULL);
        int count=0;
        while(client!=NULL){
            long creation_time=(long)client->creation_time;
            long last_update_time=(long)client->last_update_time;
            if((client->isAddrReady==1 && (current_time-last_update_time)>15) || (client->isAddrReady!=1 && (current_time-creation_time)>15)){
                ListItem* tmp=client;
                client=client->next;
                sendDisconnect(socket_udp,tmp->user_addr);
                ListItem* del=List_detach(users,tmp);
                if (del==NULL) continue;
                Image* user_texture=del->v_texture;
                if (user_texture!=NULL) Image_free(user_texture);
                count++;
                if(users->size==0) hasUsers=0;
            }
            else if (client->isAddrReady==1) {
                int x,prev_x,y,prev_y;
                x=(int)client->x;
                y=(int)client->y;
                prev_x=(int)client->prev_x;
                prev_y=(int)client->prev_y;
                if(prev_x==-1 || prev_y==-1) {
                    client->prev_x=client->x;
                    client->prev_y=client->y;
                    client->afk_counter=0;
                    client=client->next;
                }
                else if(abs(x-prev_x)<2 && abs(y-prev_y)<2) {
                    client->afk_counter++;
                    if(client->afk_counter>=10){
                        ListItem* tmp=client;
                        client=client->next;
                        sendDisconnect(socket_udp,tmp->user_addr);
                        ListItem* del=List_detach(users,tmp);
                        if (del==NULL) continue;
                        Image* user_texture=del->v_texture;
                        if (user_texture!=NULL) Image_free(user_texture);
                        count++;
                        if(users->size==0) hasUsers=0;
                        continue;
                        }
                    client=client->next;
                    }
                else {
                    client->afk_counter=0;
                    client->prev_x=client->x;
                    client->prev_y=client->y;
                    client=client->next;
                    }
            }

            else client=client->next;
        }
        if (count>0) fprintf(stdout,"[GC] Removed %d users from the client list \n",count);
        END: pthread_mutex_unlock(&mutex);
        sleep(15);
    }
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    int ret=0;
    if (argc<4) {
        debug_print("usage: %s <elevation_image> <texture_image> <port_number>\n", argv[1]);
        exit(-1);
    }
    char* elevation_filename=argv[1];
    char* texture_filename=argv[2];
    char* vehicle_texture_filename="./images/arrow-right.ppm";
    long tmp = strtol(argv[3], NULL, 0);
    if (tmp < 1024 || tmp > 49151) {
        fprintf(stderr, "Use a port number between 1024 and 49151.\n");
        exit(EXIT_FAILURE);
        }

    // load the images
    fprintf(stdout,"[Main] loading elevation image from %s ... ", elevation_filename);
    Image* surface_elevation = Image_load(elevation_filename);
    if (surface_elevation) {
        fprintf(stdout,"Done! \n");
        }
    else {
        fprintf(stdout,"Fail! \n");
    }
    fprintf(stdout,"[Main] loading texture image from %s ... ", texture_filename);
    Image* surface_texture = Image_load(texture_filename);
    if (surface_texture) {
        fprintf(stdout,"Done! \n");
    }
    else {
        fprintf(stdout,"Fail! \n");
    }

    fprintf(stdout,"[Main] loading vehicle texture (default) from %s ... ", vehicle_texture_filename);
    Image* vehicle_texture = Image_load(vehicle_texture_filename);
    if (vehicle_texture) {
        fprintf(stdout,"Done! \n");
    }
    else {
        fprintf(stdout,"Fail! \n");
    }

    port_number_no = htons((uint16_t)tmp);

    // setup tcp socket
    debug_print("[Main] Starting TCP socket \n");

    int server_tcp = socket(AF_INET , SOCK_STREAM , 0);
    ERROR_HELPER(server_tcp, "Can't create server_tcp socket");

    struct sockaddr_in server_addr = {0};
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = port_number_no;

    int reuseaddr_opt = 1; // recover server if a crash occurs
    ret = setsockopt(server_tcp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt));
    ERROR_HELPER(ret, "Failed setsockopt() on server_tcp socket");

    int sockaddr_len = sizeof(struct sockaddr_in);
    ret = bind(server_tcp, (struct sockaddr*) &server_addr, sockaddr_len); // binding dell'indirizzo
    ERROR_HELPER(ret, "Failed bind() on server_tcp");

    ret = listen(server_tcp, 16); // flag socket as passive
    ERROR_HELPER(ret, "Failed listen() on server_desc");

    debug_print("[Main] TCP socket successfully created \n");

    //init List structure
    users = malloc(sizeof(ListHead));
	List_init(users);
    fprintf(stdout,"[Main] Initialized users list \n");

    //seting signal handlers
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sa.sa_flags = SA_RESTART;

    // Block every signal during the handler
    sigfillset(&sa.sa_mask);
    ret=sigaction(SIGHUP, &sa, NULL);
    ERROR_HELPER(ret,"Error: cannot handle SIGHUP");
    //ret=sigaction(SIGINT, &sa, NULL);
    //ERROR_HELPER(ret,"Error: cannot handle SIGINT");

    debug_print("[Main] Custom signal handlers are now enabled \n");
    //preparing 2 threads (1 for udp socket, 1 for tcp socket)

    //Creating UDP Socket

    uint16_t port_number_no_udp= htons((uint16_t)UDPPORT);

    // setup server
    int server_udp = socket(AF_INET, SOCK_DGRAM, 0);
    ERROR_HELPER(server_udp, "Can't create server_udp socket");

    struct sockaddr_in udp_server = {0};
    udp_server.sin_addr.s_addr = INADDR_ANY;
    udp_server.sin_family      = AF_INET;
    udp_server.sin_port        = port_number_no_udp;

    int reuseaddr_opt_udp = 1; // recover server if a crash occurs
    ret = setsockopt(server_udp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt_udp, sizeof(reuseaddr_opt_udp));
    ERROR_HELPER(ret, "Failed setsockopt() on server_udp socket");

    ret = bind(server_udp, (struct sockaddr*) &udp_server, sizeof(udp_server)); // binding dell'indirizzo
    ERROR_HELPER(ret, "Failed bind() on server_udp socket");

    debug_print("[Main] UDP socket created \n");
    pthread_t UDP_receiver,UDP_sender,GC_thread;
    ret = pthread_create(&UDP_receiver, NULL,udp_receiver, &server_udp);
    PTHREAD_ERROR_HELPER(ret, "pthread_create on thread tcp failed");
    ret = pthread_create(&UDP_sender, NULL,udp_sender, &server_udp);
    PTHREAD_ERROR_HELPER(ret, "pthread_create on thread tcp failed");
    ret = pthread_create(&GC_thread, NULL,garbage_collector, &server_udp);
    PTHREAD_ERROR_HELPER(ret, "pthread_create on garbace collector thread failed");

    while (connectivity) {
        struct sockaddr_in client_addr = {0};
        // Setup to accept client connection
        int client_desc = accept(server_tcp, (struct sockaddr*)&client_addr, (socklen_t*) &sockaddr_len);
        if (client_desc == -1 && errno == EINTR) continue;
        ERROR_HELPER(client_desc, "Failed accept() on server_tcp socket");

        tcp_args tcpArgs;

        tcpArgs.client_desc=client_desc;
        tcpArgs.elevation_texture = surface_elevation;
        tcpArgs.surface_texture = surface_texture;
        tcpArgs.vehicle_texture = vehicle_texture;

        pthread_t threadTCP;
        ret = pthread_create(&threadTCP, NULL,tcp_flow, &tcpArgs);
        PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread tcp failed");
        ret = pthread_detach(threadTCP);
    }

    ret=pthread_join(UDP_receiver,NULL);
    ERROR_HELPER(ret,"Join on UDP_receiver thread failed");
    ret=pthread_join(UDP_sender,NULL);
    ERROR_HELPER(ret,"Join on UDP_sender thread failed");
    ret=pthread_join(GC_thread,NULL);
    ERROR_HELPER(ret,"Join on garbage collector thread failed");

    fprintf(stdout,"[Main] Shutting down the server...");
    List_destroy(users);
    ret = close(server_tcp);
    ERROR_HELPER(ret,"Failed close() on server_tcp socket");
    ret = close(server_udp);
    ERROR_HELPER(ret,"Failed close() on server_udp socket");
    Image_free(surface_elevation);
	Image_free(surface_texture);
	Image_free(vehicle_texture);

    exit(EXIT_SUCCESS);
}
