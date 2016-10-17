OBJECTS = Timer.o
SRCS = Timer.cc
INC = ./includes

CXX = g++
CPPFLAGS = -I$(INC) -std=c++11 -Werror=return-type -Wno-unused-function -Wno-deprecated -fno-rtti -pthread

#for debug
#CPPFLAGS += -g -DDEBUG
#for release
CPPFLAGS += -O -DNDEBUG

TARGET = test

all : $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CXX) $(CPPFLAGS) -o $(TARGET) $(OBJECTS)

clean :
	rm -f $(OBJECTS) $(TARGET)

dep :
	gccmakedep $(INC) $(SRCS)
