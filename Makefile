driver: driver.c
	gcc -Wall -o driver -g3 driver.c -lm -ljack

zip: driver.c
	gcc -Wall -o driver -O3 driver.c -lm -ljack
