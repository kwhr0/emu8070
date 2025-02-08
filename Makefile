OPT = /opt/homebrew
#OPT = /opt/local
#OPT = /usr/local

all:
	c++ -std=c++11 -I$(OPT)/include -L$(OPT)/lib -O3 *.cpp -o emu8070 -lSDL2

