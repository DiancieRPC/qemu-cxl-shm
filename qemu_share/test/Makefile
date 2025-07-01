CXX = g++
CXXFLAGS = -std=c++17 -O0 -g
INCLUDES = -I../includes

# Source files
SERVER_SRCS = test_server.cpp ../includes/*.cpp
CLIENT_SRCS = test_client.cpp ../includes/*.cpp

COPY_SERVER_SRCS = test_copy_server.cpp ../includes/*.cpp
COPY_CLIENT_SRCS = test_copy_client.cpp ../includes/*.cpp

# Libraries
LIBS = -lpthread

.PHONY: all clean

all: test_server test_client copy_server copy_client

test_server: $(SERVER_SRCS)
	@echo "Building test server..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../serverlib -I. $(SERVER_SRCS) $(LIBS) -o $@

test_client: $(CLIENT_SRCS)
	@echo "Building test client..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../clientlib -I. $(CLIENT_SRCS) $(LIBS) -o $@

copy_server: $(COPY_SERVER_SRCS)
	@echo "Building copy server..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../serverlib -I. $(COPY_SERVER_SRCS) $(LIBS) -o $@

copy_client: $(COPY_CLIENT_SRCS)
	@echo "Building copy client..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../clientlib -I. $(COPY_CLIENT_SRCS) $(LIBS) -o $@

clean:
	@echo "Cleaning..."
	rm -f test_server test_client copy_client copy_server