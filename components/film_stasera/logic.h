#pragma once
// The decisions of the tile that need no screen: which mode and view, which page, and what the Update button says. Plain C++ so tests/logic_test.cpp can run it on a computer.
#include <climits>
#include <cstdint>

namespace esphome::film_stasera {

enum class Mode { FILMS, PROGRAMMES };
enum class View { CHOOSE_SENSOR, WAITING, NO_DATA, OLD_GUIDE, EMPTY, LIST };
enum class Button { IDLE, BUSY, DENIED };

// How many pages `items` make with `per_page` on each (0 for nothing), and the page to show at `epoch` when the pages
// turn by themselves every `seconds_per_page`.
inline int page_count(int items, int per_page) {
  return per_page <= 0 || items <= 0 ? 0 : (items + per_page - 1) / per_page;
}
inline int page_for(uint32_t epoch, int pages, int seconds_per_page) {
  if (pages <= 1 || seconds_per_page <= 0) return 0;
  return static_cast<int>((epoch / static_cast<uint32_t>(seconds_per_page)) % static_cast<uint32_t>(pages));
}

// What the tile knows when it paints.
struct Situation {
  bool has_entity = false;     // the app sent data: a sensor was chosen
  bool wrong_entity = false;   // "wait": "wrong_entity"
  bool waiting = false;        // any other "wait"
  bool no_value = false;       // the sensor is unavailable or unknown
  bool clock_set = false;      // the screen knows the time
  int day_offset = INT32_MIN;  // days_from_today of the sensor's `date` (seconds since 1970); INT32_MIN when it has none
  int shown = 0;               // what the current mode would list
};

// The first that holds: no sensor; waiting for the app (no button: nothing to ask Home Assistant); no value or no date
// (Update); a guide that is not today's (Update); nothing to list; the list.
inline View resolve_view(const Situation &s) {
  if (!s.has_entity || s.wrong_entity) return View::CHOOSE_SENSOR;
  if (s.waiting || !s.clock_set) return View::WAITING;
  if (s.no_value || s.day_offset == INT32_MIN) return View::NO_DATA;
  if (s.day_offset != 0) return View::OLD_GUIDE;
  return s.shown > 0 ? View::LIST : View::EMPTY;
}

// The Update button after a tap: busy until the sensor's `updated` changes or `busy_until` passes; "not allowed" for a
// few seconds when the screen could not send the action.
struct Refresh {
  uint32_t busy_until = 0, denied_until = 0, updated_at_tap = 0;
};
inline Button button_state(const Refresh &r, uint32_t now, uint32_t updated_now) {
  if (now < r.denied_until) return Button::DENIED;
  if (now < r.busy_until && updated_now == r.updated_at_tap) return Button::BUSY;
  return Button::IDLE;
}

}  // namespace esphome::film_stasera
