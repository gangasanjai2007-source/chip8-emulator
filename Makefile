CXX = g++
SDL_CFLAGS = $(shell sdl2-config --cflags) -I$(shell sdl2-config --prefix)/include
SDL_LIBS = $(filter-out -mwindows,$(shell sdl2-config --libs))
# IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS: SDL already handles text input on Windows, and it
# avoids needing the extra imm32 library when building with MinGW/MSYS2
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 $(SDL_CFLAGS) -Ithird_party/imgui -DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
TARGET = chip8

# Our code
SOURCES = src/main.cpp src/chip8.cpp src/ui.cpp
# Dear ImGui (on-screen panels), vendored in third_party/imgui
IMGUI_SOURCES = third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp \
                third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp \
                third_party/imgui/imgui_impl_sdl2.cpp third_party/imgui/imgui_impl_sdlrenderer2.cpp
OBJECTS = $(SOURCES:.cpp=.o) $(IMGUI_SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(SDL_LIBS)

# Rebuild our files when the shared headers change
src/main.o src/ui.o: src/app.h src/chip8.h src/games.h
src/chip8.o: src/chip8.h

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_BIN)

# Automated tests of the CPU core (no SDL needed): run with `make test`
TEST_BIN = tests/run_tests

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/run_tests.cpp src/chip8.cpp src/chip8.h
	$(CXX) -std=c++17 -Wall -Wextra -O2 -o $(TEST_BIN) tests/run_tests.cpp src/chip8.cpp

.PHONY: all clean test
