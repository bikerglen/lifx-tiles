all: perlin

perlin: perlin.o
	g++ -o perlin perlin.o

perlin.o: perlin.cpp
	g++ -c perlin.cpp
