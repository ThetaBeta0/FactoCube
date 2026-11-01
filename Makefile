CXX      := g++
CC       := gcc
# -MMD -MP makes the compiler emit a .d file per object listing the headers it
# includes, so editing a header (e.g. block.h) rebuilds every .o that uses it.
CXXFLAGS := -std=c++17 -O2 -I./include -I./src -MMD -MP
CFLAGS   := -O2 -I./include -MMD -MP
LDLIBS   := $(shell pkg-config --libs glfw3) -lGL

SRCDIR := src
TARGET := main

CXX_SRCS := $(shell find $(SRCDIR) -name '*.cpp')
C_SRCS   := $(SRCDIR)/glad.c
OBJS     := $(CXX_SRCS:.cpp=.o) $(C_SRCS:.c=.o)
DEPS     := $(OBJS:.o=.d)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# Headless factory-simulation tests: no window or GL context needed (glad's function
# pointers are linked but never called).
TEST_TARGET := sim_test
TEST_OBJS   := tests/sim_test.o src/sim/entity.o src/sim/factory.o src/sim/logic.o \
               src/construct/construct.o src/render/instance.o src/glad.o
TEST_DEPS   := $(TEST_OBJS:.o=.d)

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $@ $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	find $(SRCDIR) tests \( -name '*.o' -o -name '*.d' \) -delete
	rm -f $(TARGET) $(TEST_TARGET)

# Pull in the generated header-dependency rules (silently ignored if absent).
-include $(DEPS) $(TEST_DEPS)

.PHONY: run clean test
