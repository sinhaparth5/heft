# pkgtop TUI design

The inspiration is `design/inspiration.jpg`: a warm gruvbox palette, panes with labelled borders, an editor-style status line with a mode pill, numbered pill tabs, a key-hint panel, and a btm-style table with a solid highlighted row. We borrow the look, not the layout.

## Layout (120×34 reference, 80×24 minimum)

```
 pkgtop  1 Packages │ 2 Caches │ 3 PATH │ 4 Stale                       ▕ 48.2 GiB ▏ 7 providers ▕ ⣾ sizing 412/1290 ▏
╭─ Ecosystems ─────────╮╭─ Packages ─────────────────────────────────────────────────╮╭─ Details ─────────────────────────╮
│ ▾ all          1290  ││ NAME              VERSION      ECO     SIZE▼  LAST USED   ││ ripgrep 14.1.1             cargo  │
│   apt          1043  ││ texlive-full      2023.2024    apt    4.1G   1y ago  ◌   ││                                   │
│   cargo          31  ││ rust-analyzer     2026-11-10   cargo  612M   2h ago  ●   ││ Root    ~/.cargo/bin              │
│   npm            18  ││ cuda-toolkit      12.6         apt    3.9G   3mo ago ◐   ││ Bins    rg                        │
│   uv             12  ││▌ripgrep           14.1.1       cargo  5.2M   now     ●  ▐││ Size    5.2 MiB  (phys)           │
│   go              9  ││ node              22.11.0      brew   188M   —       ⚠   ││ Used    4m ago · zsh history  ●   │
│   brew          177  ││ gopls             0.16.2       go      41M   6d ago  ●   ││ PATH    #3 ~/.cargo/bin  wins     │
│                      ││ ...                                                       ││                                   │
├─ Disk ───────────────┤│                                                           │├─ Shadowing ───────────────────────┤
│ apt    ████████▌ 31G ││                                                           ││ node  brew  /opt/homebrew/bin #5  │
│ cargo  ███▏      9.8G││                                                           ││   ↳ shadowed by nvm  ~/.nvm/… #1  │
│ npm    █▍        3.1G││                                                           ││                                   │
╰──────────────────────╯╰───────────────────────────────────────────────── 5/1290 ──╯╰───────────────────────────────────╯
╭─ Keys ───────────────────────────────────────────────────────────────────────────────────────────────────────────────╮
│  / → Filter        s → Cycle sort       u → Uninstall       p → Purge cache       ? → Toggle this panel   q → Quit   │
╰──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────╯
 NORMAL  cargo ▸ ripgrep                                         sort: size ▼   filter: —            12.4 GiB reclaimable 
```

Panes, from the inspiration:

| Region | Inspired by | Content |
|---|---|---|
| Top bar | waybar pills | App pill, numbered tabs (`1`–`4` switch), global totals, a scan spinner with progress |
| Ecosystems (left) | nvim file tree | Provider list with counts; selecting one filters the table. Disk bars underneath |
| Main table | btm process list | Sortable columns, solid highlighted row, `n/N` position in the bottom border |
| Details (right) | fastfetch block | `Key  value` pairs for the selected row. Shadowing chain below |
| Keys panel | the hint box | Context-sensitive hints. `?` toggles it, and it's hidden by default after first run |
| Status line | lualine | Mode pill, breadcrumb, sort/filter state, reclaimable total |

Tabs: **Packages** (managed packages), **Caches** (cache stores), **PATH** (every `$PATH` entry with owner and flags), **Stale** (packages sorted by bloat score).

## Responsive rules
- **< 120 cols:** the Details pane collapses into a 3-line strip above the status line.
- **< 100 cols:** the Ecosystems pane collapses. The provider filter is still on `e` (popup).
- **< 80×24:** show "terminal too small (need 80×24)". Never render broken layout.
- Columns shrink in this order: LAST USED → VERSION → ECO (abbreviated to 3 chars). NAME truncates with `…`.

## Palette (gruvbox dark)

Truecolor when `COLORTERM=truecolor|24bit`, otherwise the nearest of 256 colours. `NO_COLOR` disables colour entirely and the layout stays the same.

| Token | Hex | Used for |
|---|---|---|
| `bg` | `#282828` | Background |
| `bg-alt` | `#3c3836` | Pills (inactive), table header |
| `bg-sel` | `#504945` | Hover/inactive selection |
| `fg` | `#ebdbb2` | Primary text |
| `fg-dim` | `#a89984` | Secondary text, units, `—` |
| `border` | `#d5c4a1` | Focused pane border (warm tan, as in the image) |
| `border-dim` | `#665c54` | Unfocused pane border |
| `accent` | `#fabd2f` | Active tab, mode pill, focused title |
| `sel` | `#8ec07c` bg / `#282828` fg | Selected table row (the btm highlight) |
| `ok` | `#b8bb26` | Recent use, success |
| `warn` | `#fe8019` | Shadowed, stale |
| `danger` | `#fb4934` | Broken link, destructive action, failed command |
| `info` | `#83a598` | Shims, links, filter mode pill |

Ecosystem tag colours (always shown alongside the text label, never instead of it): apt `#fb4934`, pacman `#83a598`, brew `#fabd2f`, cargo `#fe8019`, go `#8ec07c`, uv/pip `#d3869b`, npm/pnpm `#b8bb26`.

Mode pill colours: NORMAL `accent`, FILTER `info`, CONFIRM `danger`, RUNNING `warn`.

## Glyphs
- Borders: rounded (`╭╮╰╯`). Focused pane gets `border` + bold title; unfocused gets `border-dim`.
- Usage confidence: `●` high, `◐` medium, `◌` low/none. Flags: `⚠` shadowed, `↯` shim, `✗` broken.
- Every glyph has a text equivalent in the Details pane, so meaning never depends on glyph or colour alone.
- Only box drawing, block elements and geometric shapes are used, so no Nerd Font is needed. `--ascii` falls back to `+-|` and `* o .`.

## Interaction

Vim-style keys, with arrow keys always working too.

| Key | Action |
|---|---|
| `j/k`, `↑/↓`, `g/G`, `PgUp/PgDn` | Move |
| `1`–`4`, `Tab` | Switch tab / cycle pane focus |
| `/` | FILTER mode: fuzzy-match name, live. `Esc` clears, `Enter` keeps |
| `s` / `S` | Cycle sort column / reverse |
| `e` | Ecosystem filter popup |
| `Enter` | Expand row (bins, all paths, history hits) |
| `u` / `p` | Uninstall package / purge cache → confirmation modal |
| `r` | Rescan the selected provider |
| `?` | Toggle keys panel |
| `q`, `Ctrl-C` | Quit |

Mouse: click selects a row, wheel scrolls, click on a header sorts. Mouse is optional and every action has a key.

## States
- **Loading sizes:** the SIZE cell shows `fg-dim` `···` until hydrated. The row never jumps, because column widths are fixed up front. The sort-by-size order is re-applied at most every 250 ms, and the selected row stays put.
- **Empty provider:** `No cargo packages found in ~/.cargo` + the path that was checked.
- **Errors:** a provider that failed to parse shows `✗` in Ecosystems. Its Details pane explains why. Other providers are unaffected.
- **Skipped entries:** `SIZE` shows `5.2M+` when entries were skipped for permissions, and Details says `3 entries skipped (permission denied)`.

## Confirmation modal

```
╭─ Uninstall ripgrep ─────────────────────────────────────╮
│                                                         │
│  Will run:   cargo uninstall ripgrep                    │
│  Reclaims:   ~5.2 MiB                                   │
│                                                         │
│  pkgtop will leave the screen while this runs.          │
│                                                         │
│          [ y  Run ]            [ n  Cancel ]            │
╰─────────────────────────────────────────────────────────╯
```

- The border and `Run` button are `danger`. Default focus is **Cancel**, and `Esc` cancels.
- Essential/base packages: `u` doesn't open the modal. The status line shows `ripgrep is Essential: refusing`.
- After the command exits, the status line shows `✓ exit 0` (`ok`) or `✗ exit 1` (`danger`) for 5 s.

## Accessibility
- Measured contrast: `fg`/`bg` 10.8:1, `fg-dim`/`bg` 5.3:1, `sel` row 7.0:1, `accent` pill 8.7:1.
- `danger` on `bg` is only 4.3:1, so danger text is always bold and paired with a glyph (`✗`).
- No information is conveyed by colour alone (tags carry text, flags carry glyph + text).
- No animation except the scan spinner, which `--no-spinner` turns off.
