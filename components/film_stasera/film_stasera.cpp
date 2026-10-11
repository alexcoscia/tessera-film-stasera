#include "film_stasera.h"
#include <algorithm>
#include <string>
#include <vector>

namespace esphome::film_stasera {

using tessera::Font;
namespace ui = tessera::ui;

// One film as Home Assistant's sensor has it: four lists of the same length, one entry per film (README.md).
struct Film {
  std::string title, channel, genre;
  uint32_t at = 0;   // when it starts, seconds since 1970
};

// A film that started less than this long ago is still on, so it stays on the tile. The guide has no end times.
static constexpr uint32_t ON_AIR_SECONDS = 150 * 60;
static constexpr int MAX_PER_COLUMN = 8;
static constexpr size_t MAX_FILMS = 20;

// The tile: a header (its name and the date), then one row per film as many as fit. A row has the start time on the left;
// beside it the title, and under the title the channel and the genre. A film that has already started shows its time in
// the accent. A tile at least WIDE_MM wide is split into two columns that fill one after the other, so a whole evening
// fits on a page. The sensor changes twice a day, the clock every second: only a text that changes is set again.
class FilmsTile : public tessera::Tile {
 public:
  explicit FilmsTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    name_ = c.name;
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
  }

  void on_state(JsonObjectConst data) override {
    films_.clear();
    JsonObjectConst a = data["attributes"];
    JsonArrayConst titles = a["titles"].as<JsonArrayConst>();
    JsonArrayConst channels = a["channels"].as<JsonArrayConst>();
    JsonArrayConst starts = a["starts"].as<JsonArrayConst>();
    JsonArrayConst genres = a["genres"].as<JsonArrayConst>();
    for (size_t i = 0; i < titles.size() && films_.size() < MAX_FILMS; ++i) {
      Film f;
      f.title = std::string(titles[i] | "");
      f.channel = std::string(channels[i] | "");
      f.genre = std::string(genres[i] | "");
      f.at = starts[i] | 0u;
      if (f.at && !f.title.empty()) films_.push_back(std::move(f));
    }
    std::sort(films_.begin(), films_.end(), [](const Film &x, const Film &y) { return x.at < y.at; });
    // Before a person has chosen the sensor the tile gets {}; a sensor without a value says unavailable or unknown.
    has_entity_ = !data["state"].isNull();
    const std::string state = std::string(data["state"] | "");
    no_value_ = state == "unavailable" || state == "unknown";
    stale_ = a["stale"] | false;
    wait_ = std::string(data["wait"] | "");
  }

  void on_tick(uint32_t now) override {
    // What is still worth showing: not over yet. The list comes in order of start, so the first rows are the next ones.
    std::vector<const Film *> shown;
    for (const auto &f : films_)
      if (now && f.at + ON_AIR_SECONDS >= now) shown.push_back(&f);
    const char *why = !has_entity_ || wait_ == "wrong_entity" ? "choose_sensor"
                      : !wait_.empty() || no_value_ || !now   ? "waiting"
                      : shown.empty()                          ? "none"
                                                               : nullptr;
    // One time column as wide as the widest time on the tile, so every title starts at the same place.
    int when_w = 0;
    if (!why)
      for (size_t i = 0; i < rows_.size() && i < shown.size(); ++i)
        when_w = std::max(when_w, ui::text_width(tessera::clock_text(shown[i]->at), Font::TITLE));
    when_w += when_w ? ui::px(ui::large() ? 12 : 8) : 0;
    paint_head(now);
    for (size_t i = 0; i < rows_.size(); ++i) {
      if (i == 0 && why) {
        place(rows_[0], "", plugin_->text(why), "", false, 0, width_);
      } else if (why || i >= shown.size()) {
        place(rows_[i], "", "", "", false, 0, col_w_);
      } else {
        const Film &f = *shown[i];
        std::string sub = f.channel;
        if (!f.genre.empty()) sub += " · " + f.genre;
        place(rows_[i], tessera::clock_text(f.at), f.title, sub, f.at <= now, when_w, col_w_);
      }
    }
  }

  void on_theme() override {
    if (head_) ui::set_color(head_, theme::INK);
    if (head_note_) ui::set_color(head_note_, theme::MUTED);
    for (auto &r : rows_) {
      ui::set_color(r.when, r.on_air ? theme::ACCENT : theme::INK);
      ui::set_color(r.title, theme::INK);
      ui::set_color(r.sub, theme::MUTED);
    }
  }

 private:
  struct Row {
    lv_obj_t *when{}, *title{}, *sub{};
    int x = 0, y = 0;
    bool on_air = false;
  };

  void make_row(Row &r, lv_obj_t *parent, int x, int y) {
    r.x = x;
    r.y = y;
    r.when = ui::label(parent, Font::TITLE, theme::INK);
    r.title = ui::label(parent, Font::BODY_LARGE, theme::INK);
    r.sub = ui::label(parent, Font::BODY, theme::MUTED);
  }

  // The time in a column of `when_w`, the title and the channel beside it, in a row `row_w` wide. A message (no time)
  // takes the title's place from the row's left edge. Nothing to show: all three empty.
  void place(Row &r, const std::string &when, const std::string &title, const std::string &sub, bool on_air,
             int when_w, int row_w) {
    r.on_air = on_air;
    ui::set_text(r.when, when);
    ui::set_color(r.when, on_air ? theme::ACCENT : theme::INK);
    const int tw = when.empty() ? 0 : when_w;
    lv_obj_set_size(r.when, std::max(1, tw), ui::line_height(Font::TITLE));
    lv_obj_set_pos(r.when, r.x, r.y + (line_ - ui::line_height(Font::TITLE)) / 2);
    const int w = std::max(1, row_w - tw);
    ui::set_text(r.title, title);
    lv_obj_set_size(r.title, w, ui::line_height(Font::BODY_LARGE));
    lv_obj_set_pos(r.title, r.x + tw, r.y + (line_ - ui::line_height(Font::BODY_LARGE)) / 2);
    ui::set_text(r.sub, sub);
    lv_obj_set_size(r.sub, w, ui::line_height(Font::BODY));
    lv_obj_set_pos(r.sub, r.x + tw, r.y + line_);
  }

  // The tile's name on the left; on the right the date, or that the guide is old.
  void paint_head(uint32_t now) {
    if (!head_) return;
    const std::string note = stale_ ? plugin_->text("old") : now ? tessera::date_text(now) : std::string();
    const int note_w = note.empty() ? 0 : ui::text_width(note, Font::BODY) + ui::px(2);
    const std::string title = name_.empty() ? plugin_->text("header") : name_;
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
  std::string name_, wait_;
  std::vector<Row> rows_;
  lv_obj_t *head_{}, *head_note_{};
  std::vector<Film> films_;
};

void FilmStasera::setup() {
  // The id is the tile's id in the manifest; the core makes a FilmsTile for every card that shows one.
  add_tile("films", [this]() { return new FilmsTile(this); });
}

}  // namespace esphome::film_stasera
