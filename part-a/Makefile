# Makefile at the project root

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++14

TARGET = repl
SRCDIR = src

all:
	cd $(SRCDIR) && $(CXX) $(CXXFLAGS) blinkdb.cpp -o ../$(TARGET)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)
