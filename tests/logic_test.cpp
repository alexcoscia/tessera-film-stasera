// Run on a computer: g++ -std=c++17 -Wall -Werror -I components/film_stasera tests/logic_test.cpp -o /tmp/logic_test && /tmp/logic_test
#include <cstdio>
#include <cstdlib>
#include "logic.h"

using namespace esphome::film_stasera;

static int failures = 0;
#define CHECK(cond)                                                  \
  do {                                                               \
    if (!(cond)) {                                                   \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);            \
      ++failures;                                                    \
    }                                                                \
  } while (0)

static Situation good() {
  Situation s;
  s.has_entity = true;
  s.clock_set = true;
  s.day_offset = 0;
  s.shown = 3;
  return s;
}

int main() {
  // pages
  CHECK(page_count(0, 8) == 0);
  CHECK(page_count(8, 8) == 1);
  CHECK(page_count(9, 8) == 2);
  CHECK(page_count(16, 8) == 2);
  CHECK(page_count(5, 0) == 0);
  CHECK(page_for(123456, 1, 8) == 0);
  CHECK(page_for(0, 2, 8) == 0);
  CHECK(page_for(8, 2, 8) == 1);
  CHECK(page_for(16, 2, 8) == 0);
  CHECK(page_for(8, 3, 8) == 1 && page_for(16, 3, 8) == 2 && page_for(24, 3, 8) == 0);

  // views, in the order of the table of the spec
  Situation s = good();
  CHECK(resolve_view(s) == View::LIST);
  s.shown = 0;
  CHECK(resolve_view(s) == View::EMPTY);
  s = good();
  s.has_entity = false;
  CHECK(resolve_view(s) == View::CHOOSE_SENSOR);
  s = good();
  s.wrong_entity = true;
  CHECK(resolve_view(s) == View::CHOOSE_SENSOR);
  s = good();
  s.waiting = true;
  CHECK(resolve_view(s) == View::WAITING);
  s = good();
  s.clock_set = false;
  CHECK(resolve_view(s) == View::WAITING);          // without the clock a date means nothing: no button
  s = good();
  s.no_value = true;
  CHECK(resolve_view(s) == View::NO_DATA);
  s = good();
  s.day_offset = INT32_MIN;
  CHECK(resolve_view(s) == View::NO_DATA);
  s = good();
  s.day_offset = -1;
  CHECK(resolve_view(s) == View::OLD_GUIDE);
  s.day_offset = 1;
  CHECK(resolve_view(s) == View::OLD_GUIDE);
  s = good();
  s.no_value = true;
  s.day_offset = -1;
  CHECK(resolve_view(s) == View::NO_DATA);          // the state wins over the date
  s = good();
  s.day_offset = -1;
  s.shown = 0;
  CHECK(resolve_view(s) == View::OLD_GUIDE);        // an old guide asks for Update even when it lists nothing

  // the button
  Refresh r;
  CHECK(button_state(r, 1000, 50) == Button::IDLE);
  r.updated_at_tap = 50;
  r.busy_until = 1090;
  CHECK(button_state(r, 1000, 50) == Button::BUSY);
  CHECK(button_state(r, 1000, 51) == Button::IDLE);  // the sensor changed: done
  CHECK(button_state(r, 1090, 50) == Button::IDLE);  // 90 seconds are over
  r.denied_until = 1005;
  CHECK(button_state(r, 1000, 50) == Button::DENIED);
  CHECK(button_state(r, 1005, 50) == Button::BUSY);  // denied is over, busy still holds

  if (failures == 0) std::printf("logic_test: ok\n");
  return failures == 0 ? 0 : 1;
}
