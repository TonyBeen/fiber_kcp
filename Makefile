CC = g++
CPPFLAGS = -Wall -O2 -std=c++11 -g
SOFLAGS = -fPIC

TARGET = libkcp.so

INCLUDE_PATH = -I.

SO_LIB_LIST = -lutils -llog -lpthread -ldl

SRC_DIR = .
TEST_SRC_DIR = ./test

SRC_C_LIST = 					\
	$(SRC_DIR)/ikcp.c			\

SRC_CPP_LIST = 					\
	$(SRC_DIR)/kcp_client.cpp	\
	$(SRC_DIR)/kcp_context.cpp	\
	$(SRC_DIR)/kcp.cpp			\
	$(SRC_DIR)/kcp_manager.cpp	\
	$(SRC_DIR)/kcp_protocol.cpp	\
	$(SRC_DIR)/kcp_server.cpp	\
	$(SRC_DIR)/kcp_utils.cpp	\
	$(SRC_DIR)/kfiber.cpp		\
	$(SRC_DIR)/kschedule.cpp	\
	$(SRC_DIR)/kthread.cpp		\
	$(SRC_DIR)/ktimer.cpp		\

OBJ_LIST = $(patsubst %.cpp, %.o, $(SRC_CPP_LIST))
OBJ_LIST += $(patsubst %.c, %.o, $(SRC_C_LIST))

all :
	make $(TARGET)


$(TARGET) : $(OBJ_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST) -shared

test : kcp_server kcp_client kcp_bench

kcp_server : $(TEST_SRC_DIR)/test_kcp_server.cc $(SRC_CPP_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)
kcp_client : $(TEST_SRC_DIR)/test_kcp_client.cc $(SRC_CPP_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)
kcp_bench : $(TEST_SRC_DIR)/kcp_benchmark.cc $(SRC_CPP_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)

%.o : %.cpp
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

%.o : %.c
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

.PHONY: all $(TARGET) clean

debug:
	@echo $(OBJ_LIST)

clean :
	rm -rf $(OBJ_LIST) kcp_server kcp_client kcp_bench