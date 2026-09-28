# Variables
CC = gcc
CFLAGS = -Wall -Wextra -g -O2 `pkg-config --cflags gtk4 libcurl json-glib-1.0` -Iincludes
LDFLAGS = `pkg-config --libs gtk4 libcurl json-glib-1.0` -lgvc -lcgraph -lm
TEST_LDFLAGS = `pkg-config --libs glib-2.0 libcurl json-glib-1.0`

CORE_SRC_DIR = src/core
GTK_SRC_DIR = src/cDockerStats-gtk4
TEST_DIR = test
OBJ_DIR = obj
BIN_DIR = bin

CORE_SRCS = $(wildcard $(CORE_SRC_DIR)/*.c)
GTK_SRCS = $(wildcard $(GTK_SRC_DIR)/*.c)

CORE_OBJS = $(patsubst $(CORE_SRC_DIR)/%.c,$(OBJ_DIR)/core/%.o,$(CORE_SRCS))
GTK_OBJS = $(patsubst $(GTK_SRC_DIR)/%.c,$(OBJ_DIR)/gtk4/%.o,$(GTK_SRCS))

TARGET = $(BIN_DIR)/cDockerStats.app
TEST_TARGET = $(BIN_DIR)/test_core.app

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(CORE_OBJS) $(GTK_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(CORE_OBJS) $(OBJ_DIR)/test/test_core.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(TEST_LDFLAGS)

$(OBJ_DIR)/core/%.o: $(CORE_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/gtk4/%.o: $(GTK_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
