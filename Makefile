CC      = cc
CFLAGS  = -std=c23 -Wall -Wextra -Wpedantic -Ivendor -DTB_IMPL
LDFLAGS = 
TARGET  = bansakako
SRC     = src/main.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) vendor/termbox2.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
