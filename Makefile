CC = g++
CPPFLAGS = -std=c++11 -Wall -g
SOFLAGS = -fPIC

TARGET = libkcp.so

INCLUDE_PATH = -I.

SO_LIB_PATH = -L.
SO_LIB_LIST = -lutils -llog -lpthread -ldl

SRC_DIR = .
TEST_SRC_DIR = ./test

SRC_LIST = 						\
	$(SRC_DIR)/ikcp.c			\
	$(SRC_DIR)/kcp.cpp			\
	$(SRC_DIR)/kcpmanager.cpp	\
	$(SRC_DIR)/kfiber.cpp		\
	$(SRC_DIR)/kschedule.cpp	\
	$(SRC_DIR)/kthread.cpp		\
	$(SRC_DIR)/ktimer.cpp		\

OBJ_LIST =						\
	$(SRC_DIR)/ikcp.o			\
	$(SRC_DIR)/kcp.o			\
	$(SRC_DIR)/kcpmanager.o		\
	$(SRC_DIR)/kfiber.o			\
	$(SRC_DIR)/kschedule.o		\
	$(SRC_DIR)/kthread.o		\
	$(SRC_DIR)/ktimer.o			\

all :
	make $(TARGET)
	make test

$(TARGET) : $(OBJ_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)

test : kcp_server kcp_client

kcp_server : $(TEST_SRC_DIR)/test_kcp_server.cc $(SRC_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)
kcp_client : $(TEST_SRC_DIR)/test_kcp_client.cc $(SRC_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)

%.o : %.cpp
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

%.o : %.c
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

.PHONY: all $(TARGET) clean

clean :
	rm -rf $(OBJ_LIST) kcp_server kcp_client