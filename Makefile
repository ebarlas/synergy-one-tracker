# compiler flags, including all warnings and optimization level 3
CXXFLAGS=-std=c++1z -Wall -O3 -Wextra -Wno-unused-parameter

# rgb distribution vars for compiling and linking
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all : synergy

synergy : synergy.o
	${CXX} synergy.o -o synergy $(LDFLAGS)

synergy.o : synergy.cpp
	${CXX} -I$(RGB_INCDIR) ${CXXFLAGS} -c synergy.cpp

clean : 
	rm -rf synergy synergy.o
