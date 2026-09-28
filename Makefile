# Variables
CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk4 libcurl json-glib-1.0` -Iincludes
LDFLAGS = `pkg-config --libs gtk4 libcurl json-glib-1.0` -lgvc -lcgraph -lm

SRC_DIR = src
OBJ_DIR = obj
INC_DIR = includes
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

TARGET = $(BIN_DIR)/cDockerStats.app

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
