#required lib
# -lthrift -lhiredis -lrdkafka++ -lkafka -ljsoncpp -lmysqlclient -lpthread -lz -ldl

CXX := g++
CXXFLAGS := -std=c++11 -I/usr/local/include -Igen-cpp
LDFLAGS := -L/usr/local/lib -L/usr/lib64/mysql

LIBS_SERVER = -lthrift -lhiredis -lrdkafka++ -lrdkafka -ljsoncpp -lmysqlclient
LIBS_CLIENT = -lthrift
LIBS_CONSUMER = -lrdkafka++ -lrdkafka -ljsoncpp -lpthread -lz -ldl	

#Thrift generation
THRIFT_SRC := user_service.thrift
THRIFT_GEN_DIR := gen-cpp
THRIFT_GEN_FILES := $(THRIFT_GEN_DIR)/UserService.cpp $(THRIFT_GEN_DIR)/user_service_types.cpp $(THRIFT_GEN_DIR)/UserService.h

# Targets
TARGETS := server client consumer

# Object files
SERVER_OBJS := server.o UserService.o user_service_types.o
CLIENT_OBJS := client.o UserService.o user_service_types.o
CONSUMER_OBJS := consumer.o

.PHONY : all clean

all: $(TARGETS)

# Thrift code generation
$(THRIFT_GEN_FILES) : $(THRIFT_SRC)
	thrift -gen cpp $< 
	@touch $(THRIFT_GEN_FILES)

# object file
%.o : %.cpp $(THRIFT_GEN_FILES)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# thrift-generated files
UserService.o : $(THRIFT_GEN_DIR)/UserService.cpp $(THRIFT_GEN_FILES)
	$(CXX) $(CXXFLAGS) -c $< -o $@

user_service_types.o : $(THRIFT_GEN_DIR)/user_service_types.cpp $(THRIFT_GEN_FILES)
	$(CXX) $(CXXFLAGS) -c $< -o $@

server : $(SERVER_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LIBS_SERVER)

client : $(CLIENT_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LIBS_CLIENT)

consumer : $(CONSUMER_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LIBS_CONSUMER) 

clean :
	rm -f $(TARGETS) *.o
	rm -rf $(THRIFT_GEN_DIR)