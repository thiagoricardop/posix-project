CC      = gcc
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra -Wpedantic -fstack-protector-strong -Iinclude
LDLIBS  = -lrt -lpthread -lm
BIN_DIR = build
APPS    = controller sensors car_panel

# Raspberry Pi build (default) links wiringPi; `make SIM=1` swaps it for a
# shared-memory GPIO simulator so everything also runs on a regular Linux PC.
ifeq ($(SIM),1)
CFLAGS   += -Isim
GPIO_SRC  = sim/gpio_sim.c
EXTRA     = $(BIN_DIR)/gpio_ctl
else
LDLIBS   += -lwiringPi
GPIO_SRC  =
EXTRA     =
endif

TARGETS = $(addprefix $(BIN_DIR)/,$(APPS)) $(EXTRA)

.PHONY: all clean test

all: $(TARGETS)

$(BIN_DIR)/gpio_ctl: tools/gpio_ctl.c sim/gpio_sim.c sim/gpio_sim.h include/common.h | $(BIN_DIR)
	$(CC) $(CFLAGS) tools/gpio_ctl.c sim/gpio_sim.c -o $@ $(LDLIBS)

$(BIN_DIR)/%: src/%.c include/common.h $(GPIO_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< $(GPIO_SRC) -o $@ $(LDLIBS)

$(BIN_DIR):
	mkdir -p $@

# End-to-end test on the simulated build.
test: clean
	$(MAKE) SIM=1 all
	bash tests/smoke_test.sh $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
