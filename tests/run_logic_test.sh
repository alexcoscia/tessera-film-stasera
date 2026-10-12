#!/bin/sh
# Builds and runs the host test of the tile's logic (no screen, no firmware).
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -Werror -I components/film_stasera tests/logic_test.cpp -o /tmp/film_stasera_logic_test
/tmp/film_stasera_logic_test
