CC      = gcc
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra -Wpedantic -fstack-protector-strong -Iinclude
LDLIBS  = -lrt -lpthread -lm
BIN_DIR = build
TARGETS = $(BIN_DIR)/controller $(BIN_DIR)/sensors $(BIN_DIR)/car_panel

.PHONY: all clean test

all: $(TARGETS)

$(BIN_DIR)/%: src/%.c include/common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

$(BIN_DIR):
	mkdir -p $@

test: all
	bash tests/smoke_test.sh $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
