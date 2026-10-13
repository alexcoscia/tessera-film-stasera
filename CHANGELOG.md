# Changelog

## 0.3.0 - 2026-10-12
- A second mode, Programmes: for each channel, in the order of the remote, the prime-time programme and the late one,
  each with its time. A tap on the tile changes between Films and Programmes; the tile option "Start mode" says which
  one it starts in; the mode a person chose stays when the screen wakes. The Programmes mode has its own title
  ("Programmes tonight") and a pale blue background. When the channels do not fit, the page turns by itself every 8
  seconds.
- When there is no data (the sensor has no value, or its guide is not today's) the tile says so and shows an Update
  button: a tap asks Home Assistant to update the sensor, which runs the script that fetches the guide. The screen must
  be allowed to perform Home Assistant actions.
- Needs the 0.3 version of the script `film-stasera-ha` (the sensor's new attributes `prog_*`); without them the Films
  mode works as before and the Programmes mode says there are no programmes.
- The tile asks for a little more room in the screen's layout memory (11000 bytes).
- A long press counts as a tap. On some screens the finger's release reaches the app late, so a quick tap was read as a
  long press, which a plugin never hears, and the tile sometimes ignored it. The tile also repaints only when
  something changed (page, minute, data), not every second.


## 0.2.1 - 2026-10-12
- Adds `store.png`, the picture of the plugin on the Plugins page, made with `tools/store_image.py`. No change to the tile.

## 0.2.0 - 2026-10-11
- Under each title the channel's name is now in the regular colour, with a dot before it in the colour of its group
  (Rai, Mediaset, Discovery, others), then the genre in grey. The sensor says the group in a new attribute, `groups`
  (a number 1 to 4); without it the dot is simply not drawn.
- The tile needs a little more room in the screen's layout memory (9000 instead of 6000 bytes).
- A tile can now start from 1x1 (it shows the next film) and grow with the editor's handles up to 4x4.

## 0.1.0 - 2026-10-11
- First version: a tile with the films on TV tonight, read from a sensor of Home Assistant.
