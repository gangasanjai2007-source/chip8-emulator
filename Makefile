CXX = g++
SDL_CFLAGS = $(shell sdl2-config --cflags) -I$(shell sdl2-config --prefix)/include
SDL_LIBS = $(filter-out -mwindows,$(shell sdl2-config --libs))
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 $(SDL_CFLAGS)
TARGET = chip8
SOURCES = src/main.cpp src/chip8.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(SDL_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_BIN)

.PHONY: all clean test
# Automated tests of the CPU core (no SDL needed): run with `make test`
TEST_BIN = tests/run_tests

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/run_tests.cpp src/chip8.cpp src/chip8.h
	$(CXX) -std=c++17 -Wall -Wextra -O2 -o $(TEST_BIN) tests/run_tests.cpp src/chip8.cpp
