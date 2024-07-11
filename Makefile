C = gcc
CXXFLAGS = -pthread -march=native
CXX = g++
CXXFLAGS = -pthread -std=c++11 -march=native
INCLUDES = -I./src -I../madras-trie/src
L_FLAGS = -lsnappy -llz4 -lbrotlienc -lbrotlidec -lz
M_FLAGS = -mbmi2 -mpopcnt
#OBJS = build/imain.o

opt: CXXFLAGS += -O3 -funroll-loops -DNDEBUG
opt: madras.dylib

debug: CXXFLAGS += -g -O0 -fno-inline
debug: madras.dylib

clean:
	rm madras.dylib
	rm -rf madras.dSYM

madras.dylib: src/madras_sql_dv1.cpp ../madras-trie/src/*.hpp ../ds_common/src/*.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -std=c++11 -fPIC -dynamiclib src/madras_sql_dv1.cpp ./sqlite-amalgamation-3430200/sqlite3.o -o madras.dylib
