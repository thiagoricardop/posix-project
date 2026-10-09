CC = gcc
CFLAGS = -Wall -pthread
LIBS = -lrt -lpthread -lm

all: controller sensors car_panel

controller: controller.c
	$(CC) $(CFLAGS) controller.c -o controller $(LIBS)

sensors: sensors.c
	$(CC) $(CFLAGS) sensors.c -o sensors $(LIBS)

panel: panel.c
	$(CC) $(CFLAGS) car_panel.c -o car_panel $(LIBS)

clean:
	rm -f controller sensors car_panel

.PHONY: all clean