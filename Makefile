CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Wshadow -Wstrict-prototypes \
	   -Wmissing-prototypes -Wcast-align -Wpointer-arith -Wwrite-strings \
	   -Wold-style-definition -fPIC -pthread -Iinclude
LDFLAGS ?= -shared -pthread

TARGET  := libponhook.so
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
DEP     := $(OBJ:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	ln -s $@ $@.0

build/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	rm -rf build $(TARGET) $(TARGET).0
