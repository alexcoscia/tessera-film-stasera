#include "film_stasera.h"
#include "logic.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <set>
#include <tuple>
#include <climits>
#include <string>
#include <vector>

namespace esphome::film_stasera {

using tessera::Font;
namespace ui = tessera::ui;

static const char *const TAG = "film_stasera";
static ModeMemory g_mode;   // the mode of the last tap, kept while the tile is made again

// One film as Home Assistant's sensor has it: four lists of the same length, one entry per film (README.md).
struct Film {
  std::string title, channel, genre;
  uint32_t at = 0;   // when it starts, seconds since 1970
  int group = 0;     // the channel's group (1 to 4, 0 for none): the colour of the dot before its name
};

// One channel of the Programmes mode: the prime-time programme and the late one, each with its start (0 = none).
// Six lists of the same length, one entry per channel (README.md).
struct Show {
  std::string channel, title1, title2;
  uint32_t at1 = 0, at2 = 0;
  int group = 0;
};

// A film that started less than this long ago is still on, so it stays on the tile. The guide has no end times.
static constexpr uint32_t ON_AIR_SECONDS = 150 * 60;
static constexpr int MAX_PER_COLUMN = 8;
static constexpr size_t MAX_FILMS = 20;
static constexpr size_t MAX_SHOWS = 20;
static constexpr int PAGE_SECONDS = 8;           // the Programmes mode turns its page this often
static constexpr uint32_t BUSY_SECONDS = 90;     // "Updating..." lasts at most this long
static constexpr uint32_t DENIED_SECONDS = 5;    // "Actions not allowed" shows this long

// The tile has two modes, and a tap on it changes the mode. Films: a header (its name and the date), then one row per
// film as many as fit. A row has the start time on the left; beside it the title, and under the title the channel's
// name with a dot in the colour of its group (Rai, Mediaset...) and the genre. A film that has already started shows
// its time in the accent. Programmes: a header ("Programmes tonight" and the date), then one block per channel in the order of
// the remote: the channel on the left, and on its right two lines, the prime-time programme with its time and the late
// one with its time. When the channels do not fit the page turns by itself. A tile at least WIDE_MM wide is split into
// two columns that fill one after the other. The sensor changes a few times a day, the clock every second: only a text
// that changes is set again.
// Without data (no value, or a guide that is not today's) the tile says so and shows an Update button: a tap asks Home
// Assistant to update the sensor, which runs the script that fetches the guide.
class FilmsTile : public tessera::Tile {
 public:
  explicit FilmsTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    name_ = c.name;
    entity_ = c.entity;
    const Mode start = std::string(c.options["start"] | "films") == "programmes" ? Mode::PROGRAMMES : Mode::FILMS;
    mode_ = initial_mode(g_mode, start);
    start_ = start;
    remember_mode(g_mode, start_, mode_);
    ESP_LOGD(TAG, "tile made, mode %s", mode_ == Mode::PROGRAMMES ? "programmes" : "films");
    // Behind everything: the Programmes mode has its own background (pale blue), the Films mode keeps the card's.
    back_ = c.parent;
    // On the 7B the finger's release reaches LVGL late on this page (0.5 s and more), so a quick tap counted as a long
    // press (400 ms), which Tessera does not pass to a plugin. A long press is the same as a tap here.
    live().insert(this);
    if (lv_obj_t *tile = lv_obj_get_parent(back_)) lv_obj_add_event_cb(tile, on_long_press, LV_EVENT_LONG_PRESSED, this);
    lv_obj_set_style_radius(back_, ui::px(ui::large() ? 12 : 8), 0);
    gap_ = ui::px(ui::large() ? 8 : 5);
    line_ = std::max(ui::line_height(Font::TITLE), ui::line_height(Font::BODY_LARGE));
    row_h_ = line_ + ui::line_height(Font::BODY);
    // The header only where at least two rows still fit under it.
    const int head = ui::line_height(Font::TITLE) + gap_;
    const int step = row_h_ + gap_;
    has_head_ = (height_ - head + gap_) / step >= 2;
    top_ = has_head_ ? head : 0;
    // Wide: two columns, by the glass's own size, so a 4-column tile of a 7-inch screen splits and a narrow one does not.
    columns_ = width_ >= ui::mm(130) ? 2 : 1;
    col_gap_ = ui::px(ui::large() ? 28 : 18);
    col_w_ = (width_ - (columns_ - 1) * col_gap_) / columns_;
    const int fit = (height_ - top_ + gap_) / step;
    per_column_ = std::max(1, std::min(MAX_PER_COLUMN, fit));
    rows_.resize(static_cast<size_t>(per_column_ * columns_));
    for (size_t i = 0; i < rows_.size(); ++i) {
      const int col = static_cast<int>(i) / per_column_, r = static_cast<int>(i) % per_column_;
      make_row(rows_[i], c.parent, col * (col_w_ + col_gap_), top_ + r * step);
    }
    if (has_head_) {
      head_ = ui::label(c.parent, Font::TITLE, theme::INK);
      head_note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    }
    // The Update button: one label with a rounded fill of its own, hidden until there is no data.
    btn_text_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_style_radius(btn_text_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn_text_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_text_, ui::color(theme::BUTTON), 0);
    lv_obj_set_style_text_align(btn_text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(btn_text_, LV_OBJ_FLAG_HIDDEN);
  }

  void on_state(JsonObjectConst data) override {
    films_.clear();
    shows_.clear();
    JsonObjectConst a = data["attributes"];
    JsonArrayConst titles = a["titles"].as<JsonArrayConst>();
    JsonArrayConst channels = a["channels"].as<JsonArrayConst>();
    JsonArrayConst starts = a["starts"].as<JsonArrayConst>();
    JsonArrayConst genres = a["genres"].as<JsonArrayConst>();
    JsonArrayConst groups = a["groups"].as<JsonArrayConst>();
    for (size_t i = 0; i < titles.size() && films_.size() < MAX_FILMS; ++i) {
      Film f;
      f.title = std::string(titles[i] | "");
      f.channel = std::string(channels[i] | "");
      f.genre = std::string(genres[i] | "");
      f.at = starts[i] | 0u;
      f.group = groups[i] | 0;
      if (f.at && !f.title.empty()) films_.push_back(std::move(f));
    }
    std::sort(films_.begin(), films_.end(), [](const Film &x, const Film &y) { return x.at < y.at; });
    JsonArrayConst p_channels = a["prog_channels"].as<JsonArrayConst>();
    JsonArrayConst p_groups = a["prog_groups"].as<JsonArrayConst>();
    JsonArrayConst p1_titles = a["prog1_titles"].as<JsonArrayConst>();
    JsonArrayConst p1_starts = a["prog1_starts"].as<JsonArrayConst>();
    JsonArrayConst p2_titles = a["prog2_titles"].as<JsonArrayConst>();
    JsonArrayConst p2_starts = a["prog2_starts"].as<JsonArrayConst>();
    for (size_t i = 0; i < p_channels.size() && shows_.size() < MAX_SHOWS; ++i) {
      Show s;
      s.channel = std::string(p_channels[i] | "");
      s.group = p_groups[i] | 0;
      s.title1 = std::string(p1_titles[i] | "");
      s.at1 = p1_starts[i] | 0u;
      s.title2 = std::string(p2_titles[i] | "");
      s.at2 = p2_starts[i] | 0u;
      if (!s.channel.empty() && (s.at1 || s.at2)) shows_.push_back(std::move(s));
    }
    // Before a person has chosen the sensor the tile gets {}; a sensor without a value says unavailable or unknown.
    has_entity_ = !data["state"].isNull();
    const std::string state = std::string(data["state"] | "");
    no_value_ = state == "unavailable" || state == "unknown";
    stale_ = a["stale"] | false;
    // The app turns an attribute named ...date into seconds since 1970: the sensor's `date` is midnight of the guide's day.
    date_epoch_ = a["date"] | 0u;
    updated_ = a["updated"] | 0u;
    wait_ = std::string(data["wait"] | "");
    ++data_version_;
  }

  void on_tick(uint32_t now) override {
    now_ = now;
    const bool programmes = mode_ == Mode::PROGRAMMES;
    // What is still worth showing of the films: not over yet. The list comes in order of start.
    std::vector<const Film *> films;
    for (const auto &f : films_)
      if (now && f.at + ON_AIR_SECONDS >= now) films.push_back(&f);

    Situation s;
    s.has_entity = has_entity_;
    s.wrong_entity = wait_ == "wrong_entity";
    s.waiting = !wait_.empty() && !s.wrong_entity;
    s.no_value = no_value_;
    s.clock_set = now != 0;
    s.day_offset = now && date_epoch_ ? tessera::days_from_today(date_epoch_) : INT32_MIN;
    s.shown = static_cast<int>(programmes ? shows_.size() : films.size());
    view_ = resolve_view(s);

    pages_ = programmes && view_ == View::LIST ? page_count(static_cast<int>(shows_.size()), static_cast<int>(rows_.size())) : 0;
    page_ = page_for(now, pages_, PAGE_SECONDS);

    const char *message = nullptr;
    switch (view_) {
      case View::CHOOSE_SENSOR: message = "choose_sensor"; break;
      case View::WAITING: message = "waiting"; break;
      case View::NO_DATA: message = "no_data"; break;
      case View::OLD_GUIDE: message = "old_day"; break;
      case View::EMPTY: message = programmes ? "none_programmes" : "none"; break;
      case View::LIST: break;
    }
    const bool button = view_ == View::NO_DATA || view_ == View::OLD_GUIDE;
    // Nothing that is painted has changed since the last tick: leave LVGL alone. Repainting every second kept it busy
    // enough to lose short taps. What is painted depends on mode, view, page, the minute (on air, date) and the button.
    const int button_now = button ? static_cast<int>(button_state(refresh_, now_, updated_)) + 1 : 0;
    const auto signature = std::make_tuple(true, data_version_, now / 60, page_, static_cast<int>(view_),
                                           static_cast<int>(mode_), button_now);
    if (signature == painted_) return;
    painted_ = signature;
    lv_obj_set_style_bg_color(back_, ui::color(theme::ACCENT_TINT), 0);
    lv_obj_set_style_bg_opa(back_, programmes && (view_ == View::LIST || view_ == View::EMPTY) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    paint_head(now);
    paint_button(button);
    if (message)
      show_message(plugin_->text(message), button);
    else if (programmes)
      show_shows();
    else
      show_films(films, now);
  }

  ~FilmsTile() override { live().erase(this); }

  // The tiles that exist: the callback gets its tile from LVGL, which may outlive it.
  static std::set<FilmsTile *> &live() {
    static std::set<FilmsTile *> tiles;
    return tiles;
  }
  static void on_long_press(lv_event_t *e) {
    auto *self = static_cast<FilmsTile *>(lv_event_get_user_data(e));
    if (self && live().count(self)) self->on_tap();
  }

  // A tap: Update where there is no data, otherwise the other mode.
  void on_tap() override {
    ESP_LOGD(TAG, "tap, view %d", static_cast<int>(view_));
    switch (view_) {
      case View::NO_DATA:
      case View::OLD_GUIDE:
        refresh();
        break;
      case View::EMPTY:
      case View::LIST:
        mode_ = mode_ == Mode::FILMS ? Mode::PROGRAMMES : Mode::FILMS;
        remember_mode(g_mode, start_, mode_);
        ESP_LOGD(TAG, "tap: mode %s", mode_ == Mode::PROGRAMMES ? "programmes" : "films");
        on_tick(now_);
        break;
      default:
        break;
    }
  }

  void on_theme() override {
    if (head_) ui::set_color(head_, theme::INK);
    if (head_note_) ui::set_color(head_note_, theme::MUTED);
    lv_obj_set_style_bg_color(back_, ui::color(theme::ACCENT_TINT), 0);
    lv_obj_set_style_bg_color(btn_text_, ui::color(theme::BUTTON), 0);
    ui::set_color(btn_text_, theme::INK);
    for (auto &r : rows_) {
      ui::set_color(r.when, r.on_air ? theme::ACCENT : theme::INK);
      ui::set_color(r.title, theme::INK);
      ui::set_color(r.chan, theme::INK);
      ui::set_color(r.when2, theme::MUTED);
      ui::set_color(r.sub, theme::MUTED);
      paint_dot(r);
    }
  }

 private:
  struct Row {
    lv_obj_t *when{}, *title{}, *dot{}, *chan{}, *sub{}, *when2{};
    int x = 0, y = 0, group = 0;
    bool on_air = false;
  };

  void make_row(Row &r, lv_obj_t *parent, int x, int y) {
    r.x = x;
    r.y = y;
    r.when = ui::label(parent, Font::TITLE, theme::INK);
    r.title = ui::label(parent, Font::BODY_LARGE, theme::INK);
    r.dot = ui::block(parent, theme::MARK_BLUE);
    r.chan = ui::label(parent, Font::BODY, theme::INK);
    r.sub = ui::label(parent, Font::BODY, theme::MUTED);
    r.when2 = ui::label(parent, Font::BODY, theme::MUTED);
  }

  // The colour of a group's dot: the four colours of the Tessera mark, the same in both looks. No group: no dot.
  static theme::Role group_role(int group) {
    switch (group) {
      case 1: return theme::MARK_BLUE;
      case 2: return theme::MARK_AMBER;
      case 3: return theme::MARK_GREEN;
      default: return theme::MARK_PURPLE;
    }
  }
  // A row without a group has no dot at all (hidden, so it cannot lie under another part).
  void paint_dot(Row &r) {
    if (r.group <= 0) {
      lv_obj_add_flag(r.dot, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    lv_obj_remove_flag(r.dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(r.dot, ui::color(group_role(r.group)), 0);
  }

  // Ask Home Assistant to update the sensor: that runs the script. The button says "Updating..." until the sensor's
  // `updated` changes (or BUSY_SECONDS pass), or "Actions not allowed" when the screen may not call Home Assistant.
  void refresh() {
    if (entity_.empty() || !can_refresh(refresh_, now_, updated_)) return;
    refresh_.updated_at_tap = updated_;
    if (tessera::action("homeassistant.update_entity", entity_))
      refresh_.busy_until = now_ + BUSY_SECONDS;
    else
      refresh_.denied_until = now_ + DENIED_SECONDS;
    on_tick(now_);
  }

  void paint_button(bool visible) {
    if (!visible) {
      ui::set_text(btn_text_, "");
      lv_obj_add_flag(btn_text_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    const Button b = button_state(refresh_, now_, updated_);
    const std::string text = plugin_->text(b == Button::BUSY ? "refreshing" : b == Button::DENIED ? "not_allowed" : "refresh");
    const int pad = ui::px(ui::large() ? 14 : 9);
    const int text_h = ui::line_height(Font::TITLE);
    const int w = std::min(width_, ui::text_width(text, Font::TITLE) + 2 * pad);
    // Under the message, on the second row; a tile of one row has only the button.
    const int y = top_ + button_row(per_column_) * (row_h_ + gap_);
    lv_obj_remove_flag(btn_text_, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(btn_text_, text);
    lv_obj_set_style_pad_top(btn_text_, pad / 2, 0);
    lv_obj_set_size(btn_text_, std::max(1, w), text_h + pad);
    lv_obj_set_pos(btn_text_, 0, y);
  }

  // A message on the first row, the rest empty; the button takes the first row alone on a tile of one row.
  void show_message(const std::string &message, bool button) {
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (i == 0 && !(button && button_row(per_column_) == 0))
        place(rows_[0], "", message, nullptr, false, 0, width_);
      else
        place(rows_[i], "", "", nullptr, false, 0, col_w_);
    }
  }

  void show_films(const std::vector<const Film *> &shown, uint32_t now) {
    // One time column as wide as the widest time on the tile, so every title starts at the same place.
    int when_w = 0;
    for (size_t i = 0; i < rows_.size() && i < shown.size(); ++i)
      when_w = std::max(when_w, ui::text_width(tessera::clock_text(shown[i]->at), Font::TITLE));
    when_w += when_w ? ui::px(ui::large() ? 12 : 8) : 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (i >= shown.size()) {
        place(rows_[i], "", "", nullptr, false, 0, col_w_);
      } else {
        const Film &f = *shown[i];
        place(rows_[i], tessera::clock_text(f.at), f.title, &f, f.at <= now, when_w, col_w_);
      }
    }
  }

  // The channels of this page, in order: the widest name and the widest time set the two columns.
  void show_shows() {
    const size_t per_page = rows_.size();
    const size_t first = static_cast<size_t>(page_) * per_page;
    const Font chan_font = col_w_ < ui::mm(70) ? Font::BODY : Font::BODY_LARGE;
    int name_w = 0, when_w = 0;
    for (size_t i = 0; i < per_page && first + i < shows_.size(); ++i) {
      const Show &s = shows_[first + i];
      name_w = std::max(name_w, ui::text_width(s.channel, chan_font));
      if (s.at1) when_w = std::max(when_w, ui::text_width(tessera::clock_text(s.at1), Font::TITLE));
      if (s.at2) when_w = std::max(when_w, ui::text_width(tessera::clock_text(s.at2), Font::BODY));
    }
    const int dot_room = ui::px(ui::large() ? 16 : 12);
    name_w = std::min(name_w + dot_room, col_w_ / 3 + dot_room) + ui::px(ui::large() ? 12 : 8);
    when_w += when_w ? ui::px(ui::large() ? 12 : 8) : 0;
    for (size_t i = 0; i < per_page; ++i)
      place_show(rows_[i], first + i < shows_.size() ? &shows_[first + i] : nullptr, name_w, when_w, chan_font);
  }

  // The time in a column of `when_w`, the title beside it and under the title the channel and the genre (`film`; null for
  // a message, which has only the title), in a row `row_w` wide. A message takes the title's place from the row's left
  // edge. Nothing to show: everything empty.
  void place(Row &r, const std::string &when, const std::string &title, const Film *film, bool on_air, int when_w,
             int row_w) {
    r.on_air = on_air;
    ui::set_text(r.when2, "");
    ui::set_font(r.chan, Font::BODY);
    ui::set_font(r.sub, Font::BODY);
    ui::set_color(r.chan, theme::INK);
    ui::set_color(r.sub, theme::MUTED);
    ui::set_text(r.when, when);
    ui::set_color(r.when, on_air ? theme::ACCENT : theme::INK);
    const int tw = when.empty() ? 0 : when_w;
    lv_obj_set_size(r.when, std::max(1, tw), ui::line_height(Font::TITLE));
    lv_obj_set_pos(r.when, r.x, r.y + (line_ - ui::line_height(Font::TITLE)) / 2);
    const int w = std::max(1, row_w - tw);
    ui::set_text(r.title, title);
    ui::set_color(r.title, theme::INK);
    lv_obj_set_size(r.title, w, ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(r.title, r.x + tw, r.y + (line_ - ui::line_height(Font::BODY_LARGE)) / 2);
    // The second line: [dot] Channel . Genre
    const int lh = ui::line_height(Font::BODY), y2 = r.y + line_;
    const std::string channel = film ? film->channel : std::string();
    const std::string genre = film && !film->genre.empty() ? "· " + film->genre : std::string();
    r.group = film ? film->group : 0;
    const int d = std::max(ui::px(7), lh * 6 / 10);
    const int dot_w = r.group > 0 ? d + ui::px(6) : 0;
    paint_dot(r);
    lv_obj_set_size(r.dot, std::max(1, d), std::max(1, d));
    lv_obj_set_pos(r.dot, r.x + tw, y2 + (lh - d) / 2);
    const int chan_w = std::min(w - dot_w, channel.empty() ? 0 : ui::text_width(channel, Font::BODY) + ui::px(2));
    ui::set_text(r.chan, channel);
    lv_obj_set_size(r.chan, std::max(1, chan_w), lh);
    lv_obj_set_pos(r.chan, r.x + tw + dot_w, y2);
    const int gx = dot_w + (chan_w > 0 ? chan_w + ui::px(6) : 0);
    ui::set_text(r.sub, genre);
    lv_obj_set_size(r.sub, std::max(1, w - gx), lh);
    lv_obj_set_pos(r.sub, r.x + tw + gx, y2);
  }

  // A channel of the Programmes mode (null: an empty block). On the left the dot and the channel's name, in the middle of
  // the block; on the right two lines: the first with its time and title, the second (smaller, muted) with its time and
  // title. A programme that is missing shows a dash.
  void place_show(Row &r, const Show *s, int name_w, int when_w, Font chan_font) {
    r.on_air = false;
    r.group = s ? s->group : 0;
    const int lh = ui::line_height(Font::BODY), y2 = r.y + line_, block_h = line_ + lh;
    const int d = std::max(ui::px(7), lh * 6 / 10);
    const int dot_w = r.group > 0 ? d + ui::px(6) : 0;
    paint_dot(r);
    lv_obj_set_size(r.dot, std::max(1, d), std::max(1, d));
    lv_obj_set_pos(r.dot, r.x, r.y + (block_h - d) / 2);
    const int chan_h = ui::line_height(chan_font);
    ui::set_font(r.chan, chan_font);
    ui::set_color(r.chan, theme::INK);
    ui::set_text(r.chan, s ? s->channel : std::string());
    lv_obj_set_size(r.chan, std::max(1, name_w - dot_w), chan_h);
    lv_obj_set_pos(r.chan, r.x + dot_w, r.y + (block_h - chan_h) / 2);
    const int x = r.x + (s ? name_w : 0);
    const int tw = std::max(1, col_w_ - (s ? name_w : 0) - when_w);
    // Line 1: the prime-time programme.
    ui::set_text(r.when, s && s->at1 ? tessera::clock_text(s->at1) : std::string());
    ui::set_color(r.when, theme::INK);
    lv_obj_set_size(r.when, std::max(1, when_w), ui::line_height(Font::TITLE));
    lv_obj_set_pos(r.when, x, r.y + (line_ - ui::line_height(Font::TITLE)) / 2);
    ui::set_text(r.title, !s ? std::string() : s->at1 ? s->title1 : std::string("—"));
    ui::set_color(r.title, theme::INK);
    lv_obj_set_size(r.title, tw, ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(r.title, x + when_w, r.y + (line_ - ui::line_height(Font::BODY_LARGE)) / 2);
    // Line 2: the late programme.
    ui::set_font(r.sub, Font::BODY);
    ui::set_text(r.when2, s && s->at2 ? tessera::clock_text(s->at2) : std::string());
    ui::set_color(r.when2, theme::MUTED);
    lv_obj_set_size(r.when2, std::max(1, when_w), lh);
    lv_obj_set_pos(r.when2, x, y2);
    ui::set_text(r.sub, !s ? std::string() : s->at2 ? s->title2 : std::string("—"));
    ui::set_color(r.sub, theme::MUTED);
    lv_obj_set_size(r.sub, tw, lh);
    lv_obj_set_pos(r.sub, x + when_w, y2);
  }

  // The tile's name on the left ("Programmes tonight" in the Programmes mode); on the right the date and, with several
  // pages, the page, or that the guide is old.
  void paint_head(uint32_t now) {
    if (!head_) return;
    std::string note;
    if (stale_) {
      note = plugin_->text("old");
    } else if (now) {
      note = tessera::date_text(now);
      if (mode_ == Mode::PROGRAMMES && pages_ > 1) note += "  " + std::to_string(page_ + 1) + "/" + std::to_string(pages_);
    }
    const int note_w = note.empty() ? 0 : ui::text_width(note, Font::BODY) + ui::px(2);
    // The Programmes mode has a title of its own; the Films mode keeps the tile's name.
    const std::string title =
        mode_ == Mode::PROGRAMMES ? plugin_->text("header_programmes") : name_.empty() ? plugin_->text("header") : name_;
    ui::set_text(head_, title);
    lv_obj_set_size(head_, std::max(1, width_ - note_w - (note_w ? ui::px(8) : 0)), ui::line_height(Font::TITLE));
    lv_obj_set_pos(head_, 0, 0);
    ui::set_text(head_note_, note);
    lv_obj_set_size(head_note_, std::max(1, note_w), ui::line_height(Font::BODY));
    lv_obj_set_pos(head_note_, width_ - note_w, (ui::line_height(Font::TITLE) - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_style_text_align(head_note_, LV_TEXT_ALIGN_RIGHT, 0);
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0, gap_ = 0, line_ = 0, row_h_ = 0, top_ = 0;
  int columns_ = 1, col_gap_ = 0, col_w_ = 0, per_column_ = 1;
  bool has_head_ = false, has_entity_ = false, no_value_ = false, stale_ = false;
  std::string name_, wait_, entity_;
  std::vector<Row> rows_;
  lv_obj_t *back_{};
  Mode start_ = Mode::FILMS;
  lv_obj_t *head_{}, *head_note_{}, *btn_text_{};
  std::vector<Film> films_;
  std::vector<Show> shows_;
  Mode mode_ = Mode::FILMS;
  View view_ = View::WAITING;
  Refresh refresh_;
  uint32_t now_ = 0, date_epoch_ = 0, updated_ = 0;
  int page_ = 0, pages_ = 0;
  uint32_t data_version_ = 0;
  std::tuple<bool, uint32_t, uint32_t, int, int, int, int> painted_{};  // what the last paint was made from
};

void FilmStasera::setup() {
  // The id is the tile's id in the manifest; the core makes a FilmsTile for every card that shows one.
  add_tile("films", [this]() { return new FilmsTile(this); });
}

}  // namespace esphome::film_stasera
