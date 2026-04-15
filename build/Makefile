CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g 

TARGET = server
OBJS = ../src/log/*.cpp ../src/pool/*.cpp ../src/time/*.cpp \
	../src/http/*.cpp ../src/webserver/*.cpp \
	../src/buffer/*.cpp ../main.cpp

all: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o ../bin/$(TARGET)  -pthread -L/usr/lib64/mysql -lmysqlclient

