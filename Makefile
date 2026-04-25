
CXX = C:/msys64/mingw32/bin/g++.exe
CXXFLAGS = -g -static

TARGET = fuzzer.exe
SRC = fuzzy.cpp

export PATH := C:/msys64/mingw32/bin:$(PATH)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)