PS5_HOST ?= ps5
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

ifdef PS5_PAYLOAD_SDK
	include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
	$(error PS5_PAYLOAD_SDK is undefined)
endif

TARGET := PPSA-RDX-CHEATS-V1.90.elf
SRC    := PPSA-RDX-CHEATS.c

CFLAGS := -O2 -Wl,-s -lkernel_sys -lSceSystemService -lSceSysCore -L./lib -lcurl -lpsl -lzstd -lminizip -lwolfssl -lz

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -I./include -o $@ $^

clean:
	rm -f $(TARGET)

test: $(TARGET)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^
