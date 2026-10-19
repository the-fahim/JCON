CC = clang
AR = ar
CPPFLAGS = -Iinclude
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
BUILD = build

.PHONY: all test sanitize clean
all: $(BUILD)/j1 $(BUILD)/libjson.a

$(BUILD):
	mkdir -p $@

$(BUILD)/json.o: src/json.c include/json.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/libjson.a: $(BUILD)/json.o
	$(AR) rcs $@ $^

$(BUILD)/j1: src/main.c $(BUILD)/json.o include/json.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) src/main.c $(BUILD)/json.o $(LDLIBS) -o $@

$(BUILD)/test_json: tests/test_json.c $(BUILD)/json.o include/json.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) tests/test_json.c $(BUILD)/json.o $(LDLIBS) -o $@

test: $(BUILD)/test_json $(BUILD)/j1
	./$(BUILD)/test_json
	sh tests/cli.sh ./$(BUILD)/j1

sanitize:
	$(MAKE) BUILD=build/sanitize CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' test

clean:
	rm -rf build
