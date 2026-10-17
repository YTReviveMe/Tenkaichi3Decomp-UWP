# Menu fonts

Three settings in F1 > UI draw text from a TrueType font instead of the game's own lettering. All three start on
"Original" and can be changed independently.

| setting | what it replaces |
|---|---|
| Menu font | the labels of the menus (pictures on the disc: "Dragon History", "1P VS COM", ...) and the names of added stages and songs |
| Battle font | Battle announcements, combo HUD, timer digits, Wins label, and blast stock digits |
| Dialogue / UI font | the game's shared text font (`Font_Print`): dialogue, subtitles, explanations, ordinary menu text |

So the Dialogue / UI font does change the game's ordinary `Font_Print` text: that is what it is for. Placement,
line breaks, colours, shadows and the button icons stay the game's; only the glyph pictures are drawn again.

## The fonts

The fonts are in `port/third_party/fonts/` (in a release: `fonts/` next to the program), one folder per family
with its licence, and the list `fonts.tsv`:

```
# file <tab> name in the list <tab> use: 1 = Menu, 2 = Dialogue / UI, 4 = Battle, added up for several
fira/FiraSansCondensed-Bold.ttf	Fira Sans Condensed Bold	1
dejavu/DejaVuSansCondensed-Bold.ttf	DejaVu Sans Condensed Bold	1
dejavu/DejaVuSansCondensed.ttf	DejaVu Sans Condensed	2
anton/Anton-Regular.ttf	Anton	4
```

To offer one font in several settings, add their numbers: `3` for Menu and Dialogue / UI, `7` for all three. The
column is the one it was before the Battle font came (1, 2, 3 = both), so lines added to an earlier list keep
their meaning, and a line without it is offered for Menu and Dialogue / UI as before. Fira Sans Condensed Bold is
first: it is the lettering of the PC's own screens (Tenkaichi Budokai Online) while the Menu font is "Original".
To add a font, put the `.ttf` there and add a line; restart the game. A font has to be a static TrueType file
(glyf outlines; not `.otf`, not a collection, not a variable font), have a capital `H` (the letter height is taken
from it) and be at most 16 MB. Choices are kept by file path, not by their place in the list.

A font file is checked when it is chosen and at every start. One that is cut short, damaged or of another kind is
not used: the setting goes back to "Original", and the log has one line with the reason, for example

```
bt3: font "DejaVu Sans Condensed Bold" (fonts/dejavu/DejaVuSansCondensed-Bold.ttf) is not used: a table lies outside
the file (the file is cut short or damaged). The setting is back on "Original".
```

## The menu labels

Which texture has which label, and how large its letters are, is measured on your own disc data. The game does it
itself (`port/src/gs/menu_labels.c`), in a source installation and in a release alike: when it starts with a
window and `gamedata/game-text/labels.tsv` is not there, or does not have the current header, it reads the HUD
resource `gamedata/pzs3us1/00005.bin` and eleven menu archives, `00448.bin` ... `00472.bin`, and writes `gamedata/game-text/`:

- `labels.tsv` and `name-styles.tsv`: what the game reads;
- `labels.json`: the same list for a person to edit (below). One that is already there is kept as it is.

This takes less than a second and runs in the background: the game does not wait for it. Until it is
done "Menu font" offers only "Original"; then the fonts appear in the list, and a font that the settings file
already names is taken into use between two frames (a menu on screen at that moment changes its lettering).
The log reports how many labels it made from the disc; the measured time varies.
Each file is written under another name and renamed when complete: a list is never half there.

The texture numbers are those of the USA release (SLUS-21678), so all twelve archives are checked by size and CRC-32
before anything is read from them. With any other disc, a missing archive or a folder that cannot be written to,
nothing is made, the log has one line saying why, and "Menu font" offers only "Original"; the game is otherwise
unaffected. (It is tried again at the next start.) `BT3_MENU_FONT_ANY_DISC=1` leaves the check out, for a changed
archive: where its textures are numbered differently, labels are missing or wrong.

A run whose buttons are recorded or played back, or that is re-run blank by blank (`BT3_PAD_REC`, `BT3_PAD_PLAY`,
`BT3_PAD_TABLE`, `BT3_SYNCTEST`), makes the list at start, before anything is drawn: its pictures do not depend on
how fast the list was made. During an online session nothing is taken into use; a list finished then waits for
the session's end. Without a window (`BT3_GS=none`) nothing of this runs.

The list is derived from the disc and is never part of the repository or of a package. Before this was in the
game it was a step of `install.py` (a Python tool with a C helper built by gcc), which a release could not run;
the files the game writes are the same bytes as that tool's, and a `game-text/` folder made by it goes on working.

## Edit labels

1. Open `gamedata/game-text/labels.json` and find the entry by `id` or `original`.
2. Change `text`. If the entry has `text_from`, edit that referenced entry to change both selection states.
3. Apply the changes and restart the game:

   ```sh
   python3 port/tools/menu_font.py
   ./play.sh
   ```

No game rebuild or archive repacking is needed. The tool turns `labels.json` into `labels.tsv` and
`name-styles.tsv`; it reads only that JSON and needs only Python.
For a release, take
`port/tools/menu_font.py` from the source and name the folder: `python3 menu_font.py --out <game>/gamedata/game-text`.
The game itself never reads `labels.json` and never writes over one. To go back to the disc's labels, delete
`labels.json` and `labels.tsv`: the game writes both again at its next start.
If a local `labels.json` lacks battle labels, delete it and `labels.tsv`, then start the game to generate the
current catalog. Copy any edits you want to keep into the new JSON and run the tool again.

To see one original and replacement example for each menu color style, run from the repository root:

```sh
python3 port/tools/menu_font.py --compare-styles
```

The command writes `port/build/menu-font/style-comparison.png`, names the fonts used, and applies current
`labels.json` edits before rendering. It uses DejaVu Sans Condensed Bold by default and needs the extracted disc
data, Pillow, and a C compiler. Announcement rows are included when the Battle font listed in `fonts.tsv` is present.
Set the PNG path with `--output path/to/comparison.png`.

| field | effect |
|---|---|
| `text` | Label text; `\n` makes two lines |
| `text_from` | Entry supplying shared text and enabled state |
| `enabled` | `false` keeps the original sprite |
| `italic` | `true` slants this label; default `false`. Enabled for “Press START button” and battle announcements. |
| `font_face` | `battle` uses the separate Battle font setting; `selected` uses the chosen Menu font |
| `color_style` | A shared palette such as `purple`, `main-heading`, or one of the `notice-*` styles |
| `shadow` | `false`, `true`, `hard`, or `translucent-hard` (a black, partly transparent bottom-right shadow) |
| `cap_height`, `baseline` | Letter height and baseline, in original texture pixels |
| `align`, `margin` | `left` or `center`, with horizontal margin |
| `width_scale` | Horizontal scale, 0.5–2.0; default 1.0 |
| `art_width` | Measured width of the original letter fill |

Leave texture identity and cell bounds unchanged: `hash`, `texture_width`, `texture_height`, `x`, `y`,
`width` and `height`. Edit the JSON, not the generated `labels.tsv` or `name-styles.tsv`.

Unchanged labels match the original artwork's letter width and intended height. Edited labels and added
names use natural font proportions, shrinking to fit. Text supports up to two lines and 255 UTF-8 bytes.
Characters absent from the selected font are skipped. Empty labels, or labels with no supported characters,
keep the original sprite. The tool reports characters unsupported by every font of the Menu font list.
Complex-script shaping and bidirectional text are not supported.

## Text styles

Each label's `color_style` references a shared entry in `color_styles` at the top of `labels.json`.
Styles apply across selectable typefaces. Colors are defined explicitly; artwork is measured only for layout.

| Type | Used for | Default fill, top → bottom | Outline |
|---|---|---|---|
| `purple` | Most unselected labels and unselected song names | Light gray `#CBCBCB` | Purple `#460059` |
| `main-menu` | Unselected main-menu labels | Light gray `#CBCBCB` | Lighter purple `#5E236D` |
| `main-heading` | Large headings in the main and submenu screens | Gold `#FFBE2F` → red `#FF2222` | Dark brown `#7C3025`, darkening at the base |
| `orange` | Selected labels, stage names and character-preview numbers | Yellow `#F4EF89` → orange `#D2684B` | Dark brown `#2C151B` |
| `title-prompt` | “Press START button” | Cream `#EFEFDF` → yellow-green `#B1B360` | Dark red `#3F1511` |
| `notice-blue` | Ready! | Icy white → blue | Deep blue |
| `notice-orange` | Fight! | Cream → orange | Dark brown |
| `notice-red` | K.O.! | Pink → red | Dark red |
| `notice-silver` | Success, Perfect! | White → blue-gray | Slate blue |
| `notice-green` | Ringout!, Time Up! | Pale green → green | Dark green |
| `notice-gold` | Winner! | Cream → gold | Dark brown |
| `notice-purple` | Loser | Pale lavender → purple | Dark purple |

Colors in the JSON use RGB triples, such as `[203, 203, 203]` for `#CBCBCB`.
Existing catalogs keep their saved style values; editing a shared style changes all labels using it.
Added song and stage names share the corresponding fill and outline colors.

| Style field | Effect |
|---|---|
| `top`, `bottom` | Fill gradient; equal RGB triples give a solid fill |
| `middle` | Optional center fill color; blends from `top` through `middle` to `bottom` |
| `outline` | Inner outline RGB color |
| `outline_width` | Optional width, 0–8 original texture pixels; default automatic, usually 3 |
| `outer_outline` | Optional outer outline RGB color |
| `outer_outline_width` | Width, 0–8 original texture pixels; default 0 (disabled) |
| `outline_bottom` | Optional color at the bottom of the inner outline; announcement rims use the solid `outline` color |
| `bevel_width` | Optional border bevel width, 0–4 original texture pixels; default 0 |
| `bevel_light`, `bevel_dark` | Highlight and recess colors for menu borders; announcement faces use directional shading |
| `depth` | Solid diagonal extrusion behind the letters, 0–8 original texture pixels; default 0 |
| `mid_gradient` | `true` sharpens a two-color fill transition around the middle; default `false` |
| `shadow` | Offset shadow; `"hard"` draws a short solid black shadow and a slight light edge on the top and left; default `true` |

## Files and rendering

- `port/third_party/fonts/` (release: `fonts/`): the typefaces, their licences and `fonts.tsv`. `BT3_FONTS` names
  another folder; an empty value switches all three settings off.
- `gamedata/game-text/` (or `$BT3_DATA/game-text`): the labels made from the disc; ignored by Git, never packaged.
  `BT3_MENU_FONT` names another folder; an empty value disables menu and battle label replacement.
- `port/src/gs/menu_labels.c`: makes the list. It holds the catalogue (`ml_catalog`: which archive, section and
  texture a menu's labels or battle announcements are in, and what they say) and nothing of the disc; the stage and song names are
  `namefont.c`'s tables, and its letter cutter measures the long name strips. To add a menu, add its sheets there.
- `port/tools/menu_font.py`: an edited `labels.json` into the files the game reads; `--compare-styles` renders a PNG.
  `--out` names the folder.
- `port/src/gs/menu_text.c`: the font check, the drawing and the list of fonts, for Vulkan and OpenGL. Text is
  drawn at the internal resolution, at least 2 and at most 4 times the PS2's.
- `port/src/gs/ui.cpp`: the three settings, and the names of added songs and stages.
- `src/battle/col_c.c` `Font_UploadTexture`: under `#ifdef PORT`, hands the game's glyph table to the renderer.
- `port/tests/menu_font/fuzz.c`: damaged font files against the font check, with the sanitizers.
- `port/tests/menu_font/labels.c`: the maker of the list on its own (to compare its files), and against damaged
  menu archives, with the sanitizers.

Texture-pack replacements take precedence over the font settings. They do not touch the game's state: an online
match is the same with any font on either side. `BT3_FONT_LOG=1` prints every decoded texture and the time the
text took.
