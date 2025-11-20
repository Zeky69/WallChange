CC = gcc
CFLAGS = -O2
LDFLAGS = 

TARGET_CLIENT = wallchange
TARGET_SERVER = server
SRC_CLIENT = main.c mongoose.c cJSON.c
SRC_SERVER = server.c mongoose.c cJSON.c

all: $(TARGET_CLIENT) $(TARGET_SERVER)

$(TARGET_CLIENT): mongoose.c cJSON.c $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LDFLAGS)

$(TARGET_SERVER): mongoose.c cJSON.c $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) $(SRC_SERVER) $(LDFLAGS)

mongoose.c:
	@echo "Téléchargement de Mongoose..."
	curl -s -o mongoose.c https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
	curl -s -o mongoose.h https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h

cJSON.c:
	@echo "Téléchargement de cJSON..."
	curl -s -o cJSON.c https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
	curl -s -o cJSON.h https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h

clean:
	rm -f $(TARGET_CLIENT) $(TARGET_SERVER) mongoose.c mongoose.h cJSON.c cJSON.h
