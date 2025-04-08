# Detect OS
UNAME_S := $(shell uname -s)

# Enable static linking by default (change to 'no' to use dynamic linking)
STATIC_LINKING = yes

# Compiler settings based on OS
ifeq ($(UNAME_S),Linux)
# Linux settings

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -m64 -std=c++17 -march=native -mtune=native -Ofast -mssse3 -Wall -Wextra \
           -funroll-loops -ftree-vectorize -fstrict-aliasing -fno-semantic-interposition \
           -fvect-cost-model=unlimited -fno-trapping-math -fipa-ra -flto -fopenmp \
           -mavx2 -mbmi2 -madx
LDFLAGS = -lgmp -lgmpxx -lxxhash

# Source files
SRCS = bsgs.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Target executable
TARGET = bsgs 

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	rm -f $(OBJS) && chmod +x $(TARGET)

# Compile each source file into an object file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	@echo "Cleaning..."
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

else
# Windows settings (MinGW-w64)

# Compiler
CXX = g++

# Check if compiler is found
CHECK_COMPILER := $(shell which $(CXX) 2>/dev/null)

# Add MSYS path if the compiler is not found
ifeq ($(CHECK_COMPILER),)
  $(info Compiler not found. Adding MSYS path to the environment...)
  SHELL := cmd
  PATH := C:\msys64\mingw64\bin;$(PATH)
endif

# Compiler flags
CXXFLAGS = -m64 -std=c++17 -march=native -mtune=native -Ofast -mssse3 -Wall -Wextra \
           -funroll-loops -ftree-vectorize -fstrict-aliasing -fno-semantic-interposition \
           -fvect-cost-model=unlimited -fno-trapping-math -fipa-ra -flto -fopenmp \
           -mavx2 -mbmi2 -madx
LDFLAGS = -lgmp -lgmpxx -lxxhash

# Add -static flag if STATIC_LINKING is enabled
ifeq ($(STATIC_LINKING),yes)
    LDFLAGS += -static
else
    $(info Dynamic linking will be used. Ensure required DLLs are distributed)
endif

# Source files
SRCS = bsgs.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Target executable
TARGET = bsgs.exe

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	del /q $(OBJS) 2>nul

# Compile each source file into an object file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	@echo Cleaning...
	del /q $(OBJS) $(TARGET) 2>nul

.PHONY: all clean
endif