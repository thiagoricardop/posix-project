CC = gcc
CFLAGS = -Wall -pthread
LIBS = -lrt -lwiringPi -lpthread -lm

all: controller sensors car_panel

controller: controller.c common.h
	$(CC) $(CFLAGS) controller.c -o controller $(LIBS)

sensors: sensors.c common.h
	$(CC) $(CFLAGS) sensors.c -o sensors $(LIBS)

car_panel: car_panel.c common.h
	$(CC) $(CFLAGS) car_panel.c -o car_panel $(LIBS)

clean:
	rm -f controller sensors car_panel

.PHONY: all clean