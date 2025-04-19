CXX ?= clang++
CXXFLAGS ?= -Wall -Wextra -std=c++17
LDFLAGS ?= -lncurses
SRC ?= melt.cpp
TARGET ?= mel
PREFIX ?= ~/.local/bin

OBJ := $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install:
	mkdir -p $(PREFIX)
	cp $(TARGET) $(PREFIX)
	chmod +x $(PREFIX)/$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all install clean
