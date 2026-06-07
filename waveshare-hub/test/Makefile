# Host-side unit tests for GigaSenseHub Waveshare Hub
# Runs on macOS/Linux without ESP-IDF — tests pure logic only.
#
# Usage:
#   make          — build and run all tests
#   make clean    — remove test binaries

CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -std=c11 -I../main
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -I../main

TESTS = test_i18n test_weather_codes

.PHONY: all clean test

all: test

test: $(TESTS)
	@echo ""
	@echo "==============================="
	@echo "  Running host-side unit tests"
	@echo "==============================="
	@echo ""
	@fail=0; \
	for t in $(TESTS); do \
		./$$t || fail=1; \
		echo ""; \
	done; \
	if [ $$fail -eq 0 ]; then \
		echo "=== ALL TESTS PASSED ==="; \
	else \
		echo "=== SOME TESTS FAILED ==="; \
		exit 1; \
	fi

test_i18n: test_i18n.c ../main/i18n.cpp
	g++ -Wall -Wextra -std=c++17 -I../main -o $@ test_i18n.c ../main/i18n.cpp

test_weather_codes: test_weather_codes.c weather_code_funcs.c
	$(CC) $(CFLAGS) -o $@ test_weather_codes.c weather_code_funcs.c

clean:
	rm -f $(TESTS)
