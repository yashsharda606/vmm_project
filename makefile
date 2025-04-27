CC = g++
CFLAGS = -std=c++11 -Wall
TARGET = mmu
SRC_DIR = src
OBJ = $(SRC_DIR)/main.o $(SRC_DIR)/pager.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

$(SRC_DIR)/main.o: $(SRC_DIR)/main.cpp $(SRC_DIR)/types.h $(SRC_DIR)/pager.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/main.cpp -o $(SRC_DIR)/main.o

$(SRC_DIR)/pager.o: $(SRC_DIR)/pager.cpp $(SRC_DIR)/pager.h $(SRC_DIR)/types.h
	$(CC) $(CFLAGS) -c $(SRC_DIR)/pager.cpp -o $(SRC_DIR)/pager.o

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET) *.log