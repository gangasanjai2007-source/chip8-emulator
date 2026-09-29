CXX = g++
SDL_CFLAGS = $(shell sdl2-config --cflags)
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
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean