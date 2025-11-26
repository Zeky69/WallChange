CC = gcc
CFLAGS = -O2 -DMG_TLS=2 -Iinclude -Isrc/common
LDFLAGS = -lssl -lcrypto

TARGET_CLIENT = wallchange
TARGET_SERVER = server

SRC_COMMON = src/common/mongoose.c src/common/cJSON.c
SRC_CLIENT = src/client/main.c src/client/utils.c src/client/wallpaper.c src/client/updater.c src/client/network.c $(SRC_COMMON)
SRC_SERVER = src/server/main.c $(SRC_COMMON)

all: $(TARGET_CLIENT) $(TARGET_SERVER)

$(TARGET_CLIENT): $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LDFLAGS)

$(TARGET_SERVER): $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) $(SRC_SERVER) $(LDFLAGS)

src/common/mongoose.c:
	@mkdir -p src/common
	@echo "Téléchargement de Mongoose..."
	curl -s -o src/common/mongoose.c https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
	curl -s -o src/common/mongoose.h https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h

src/common/cJSON.c:
	@mkdir -p src/common
	@echo "Téléchargement de cJSON..."
	curl -s -o src/common/cJSON.c https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
	curl -s -o src/common/cJSON.h https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h


	
clean:
	rm -f $(TARGET_CLIENT) $(TARGET_SERVER)

re: clean all
