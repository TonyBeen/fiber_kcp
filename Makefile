CC = g++
CPPFLAGS = -Wall -Wno-unused-function -O0 -m64 -std=c++11 -g
SOFLAGS = -fPIC

TARGET = libkcp.so

INCLUDE_PATH = -I.

SO_LIB_LIST = -lutils -llog -lpthread -ldl

SRC_DIR = .
TEST_SRC_DIR = ./test

EXAMPLE_SRC_LIST = $(wildcard $(TEST_SRC_DIR)/*.cc)
EXAMPLE_OBJ_LIST = $(patsubst %.cc, %.o, $(EXAMPLE_SRC_LIST))

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
	make test

$(TARGET) : $(OBJ_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST) -shared

test : test_kcp_server test_kcp_client kcp_benchmark_server kcp_benchmark_client

test_kcp_server : $(TEST_SRC_DIR)/test_kcp_server.o $(OBJ_LIST)
	$(CC) $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SO_LIB_LIST)
test_kcp_client : $(TEST_SRC_DIR)/test_kcp_client.o $(OBJ_LIST)
	$(CC) $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SO_LIB_LIST)
kcp_benchmark_server : $(TEST_SRC_DIR)/kcp_benchmark_server.o $(OBJ_LIST)
	$(CC) $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SO_LIB_LIST)
kcp_benchmark_client : $(TEST_SRC_DIR)/kcp_benchmark_client.o $(OBJ_LIST)
	$(CC) $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SO_LIB_LIST)

%.o : %.cpp
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

%.o : %.c
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

%.o : %.cc
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

.PHONY: all $(TARGET) clean

clean :
	-rm -rf $(OBJ_LIST) $(EXAMPLE_OBJ_LIST)
	-rm -rf test_kcp_server test_kcp_client kcp_benchmark_server kcp_benchmark_client