CCOPTS= -Wall -g -Wstrict-prototypes
LIBS= -lglut -lGLU -lGL -lm -lpthread -lopenal -lalut
CC=gcc -std=gnu99
AR=ar


BINS=libso_game.a\
     so_game_server\
     so_game_client\
	test_packets_serialization\
	test_client_list
OBJS = av_framework/vec3.o\
       av_framework/surface.o\
       av_framework/image.o\
       av_framework/audio_list.o\
       av_framework/world.o\
       av_framework/world_viewer.o\
       av_framework/audio_context.o\
       game_framework/linked_list.o\
       game_framework/vehicle.o\
       game_framework/so_game_protocol.o\
       game_framework/client_list.o\
	   game_framework/message_list.o\
       client/client_op.o\
       
HEADERS=av_framework/image.h\
	game_framework/linked_list.h\
	game_framework/so_game_protocol.h\
	game_framework/vehicle.h\
	game_framework/client_list.h\
	game_framework/message_list.h\
	av_framework/surface.h\
	av_framework/vec3.h\
	av_framework/audio_list.h\
	av_framework/world.h\
	av_framework/world_viewer.h\
	av_framework/audio_context.h\
	common/common.h\
	client/client_op.h\

%.o:	%.c $(HEADERS)
	$(CC) $(CCOPTS) -c -o $@ $<

.phony: clean all

all:	$(BINS)

libso_game.a: $(OBJS)
	$(AR) -rcs $@ $^
	$(RM) $(OBJS)

so_game_client: client/so_game_client.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

so_game_server: server/so_game_server.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^ $(LIBS)

test_packets_serialization: tests/test_packets_serialization.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)

test_client_list: tests/test_client_list.c libso_game.a
	$(CC) $(CCOPTS) -Ofast -o $@ $^  $(LIBS)




