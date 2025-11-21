CC = gcc
CFLAGS = -O2
LDFLAGS = 

TARGET_CLIENT = wallchange
SRC_CLIENT = main.c mongoose.c cJSON.c

all: $(TARGET_CLIENT)

$(TARGET_CLIENT): mongoose.c cJSON.c $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LDFLAGS)

clean:
	rm -f $(TARGET_CLIENT) mongoose.c mongoose.h cJSON.c cJSON.h

re: clean all
