CC = g++
CPPFLAGS = -std=c++11 -g
SOFLAGS = -fPIC

TARGET = libkcp.so

INCLUDE_PATH = -I.

SO_LIB_PATH = -L.
SO_LIB_LIST = -lutils -llog -lpthread -ldl

SRC_DIR = .
TEST_SRC_DIR = ./test

HEADER_FILE_LIST = 				\
	$(SRC_DIR)/ikcp.h			\
	$(SRC_DIR)/kcp.h			\
	$(SRC_DIR)/kcpmanager.h		\
	$(SRC_DIR)/kfiber.h			\
	$(SRC_DIR)/kschedule.h     	\
	$(SRC_DIR)/kthread.h		\
	$(SRC_DIR)/ktimer.h			\

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

install:
	make $(TARGET)
	-sudo mv $(TARGET) /usr/local/lib/
	-sudo ldconfig
	-if [ ! -d "/usr/local/include/kcp/" ]; then sudo mkdir /usr/local/include/kcp/; fi
	-sudo cp $(HEADER_FILE_LIST) /usr/local/include/kcp/
	-make clean

uninstall:
	-sudo rm /usr/local/lib/$(TARGET)
	-sudo ldconfig
	-sudo rm -r /usr/local/include/kcp

$(TARGET) : $(OBJ_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST) -shared

test : kcp_server kcp_client kcp_bench

kcp_server : $(TEST_SRC_DIR)/test_kcp_server.cc $(SRC_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)
kcp_client : $(TEST_SRC_DIR)/test_kcp_client.cc $(SRC_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)
kcp_bench : $(TEST_SRC_DIR)/kcp_benchmark.cc $(SRC_LIST)
	$(CC) $^ -o $@ $(SO_LIB_LIST)

%.o : %.cpp
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

%.o : %.c
	$(CC) -c $^ -o $@ $(INCLUDE_PATH) $(CPPFLAGS) $(SOFLAGS)

.PHONY: all $(TARGET) install uninstall clean

clean :
	rm -rf $(OBJ_LIST) kcp_server kcp_client kcp_bench