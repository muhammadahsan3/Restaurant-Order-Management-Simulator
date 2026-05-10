
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -g -pthread
LDFLAGS  = -lpthread
TARGET   = program

SRCS = project.cpp Logger.cpp Dashboard.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)
	@echo ""
	@echo "  Build OK  ->  ./$(TARGET)"
	@echo ""

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

project.o: project.cpp Logger.h Dashboard.h
	$(CXX) $(CXXFLAGS) -c project.cpp

Logger.o: Logger.cpp Logger.h
	$(CXX) $(CXXFLAGS) -c Logger.cpp

Dashboard.o: Dashboard.cpp Dashboard.h
	$(CXX) $(CXXFLAGS) -c Dashboard.cpp

run: all
	./$(TARGET)

clean:
	rm -f *.o $(TARGET)
	@echo "  Cleaned"

.PHONY: all run clean
