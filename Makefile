C = gcc
CXXFLAGS = -pthread -march=native
CXX = g++
CXXFLAGS = -pthread -std=c++11 -march=native
INCLUDES = -I./src -I../madras-trie/src
L_FLAGS = -lsnappy -llz4 -lbrotlienc -lbrotlidec -lz
M_FLAGS = -mbmi2 -mpopcnt
#OBJS = build/imain.o

opt: CXXFLAGS += -O3 -funroll-loops -DNDEBUG
opt: madras

debug: CXXFLAGS += -g -O0 -fno-inline
debug: imain

clean:
	rm madras
	rm -rf madras.dSYM

madras: src/madras.cpp ../madras-trie/src/madras_dv1.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -std=c++11 -fPIC -shared src/madras.cpp ./sqlite-amalgamation-3430200/sqlite3.o -o madras
