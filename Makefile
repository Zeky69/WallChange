CC = gcc
CFLAGS = -O2
LDFLAGS = 

TARGET_SERVER = server
SRC_SERVER = server.c mongoose.c cJSON.c

all: $(TARGET_SERVER)

$(TARGET_SERVER): mongoose.c cJSON.c $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) $(SRC_SERVER) $(LDFLAGS)

clean:
	rm -f $(TARGET_SERVER) mongoose.c mongoose.h cJSON.c cJSON.h
