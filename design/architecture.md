# pkgtop architecture

This is a single process on a local machine: no servers, no IPC, no database. The interesting problems are latency (first paint in under 50 ms), concurrency (keeping the UI responsive while millions of `fstatat` calls run), and terminal lifecycle (handing the TTY to a child process and getting it back safely).

## Requirements

**Functional:** list packages, caches and `$PATH` binaries across providers; show physical sizes, last use and shadowing; uninstall/purge through the native command.

**Non-functional:**
- First paint with names/versions < 50 ms (warm page cache)
- Input latency < 16 ms always
- Zero subprocesses while reading
- Correct physical sizes (hardlink dedupe, `st_blocks`)
- A failure in any one provider never takes down the others

**Out of scope (for now):** remote version checks, a size cache that persists across runs, macOS, rpm.

## Scale estimate (one workstation)

| Thing | Typical | Heavy | Cost |
|---|---|---|---|
| dpkg status | 3k pkgs, ~4 MB | 8k pkgs, 12 MB | mmap + scan ≈ 2–10 ms |
| Small manifests (cargo/uv/npm/go) | < 100 files | ~500 | < 5 ms total |
| Files under all sized roots | ~500k | 5M (pnpm store, texlive, CUDA) | `fstatat` ≈ 1 µs warm, 50–100 µs cold NVMe |
| Shell history | 50k lines | 1M lines | reverse scan, ≈ 10–50 ms |

Conclusion: Stage 1 (metadata) fits easily in 50 ms on one thread. Stage 2 (sizing) takes 0.5 s warm and anywhere from 5 s to minutes cold, so it **must** run in the background. It's I/O-bound, so the pool gets `min(hw_threads, 8)` workers, not more.

## Components

```
              ┌────────────────────────── UI thread (FTXUI loop) ──────────────────────────┐
  keys/mouse ─▶  App state (single owner)  ──render──▶  terminal                           │
              │     ▲            │ enqueue(SizeJob, prio)                                   │
              └─────┼────────────┼──────────────────────────────────────────────────────────┘
                    │ screen.Post(closure)      ▼
                    │                 ┌───────────────────┐
                    ├─────────────────┤  Size pool (N)    │  openat/getdents64/fstatat walker
                    │                 │  prio queue+condvar│
                    │                 └───────────────────┘
                    ├─────────────────  History thread     zsh/bash/fish → name → last epoch
                    │
  startup ──▶ Providers::scan() (UI thread, sync) ──▶ Inventory {packages, caches, endpoints}
```

- **Provider:** a plain struct per ecosystem with free functions: `detect()`, `scan() -> ProviderResult`, `cache_paths()`, `uninstall_argv(pkg)`, `purge_argv(cache)`. The `Provider` table is a `std::array` of function pointers. There's no class hierarchy until behaviour actually diverges.
- **Inventory:** `std::vector<Package>`, `std::vector<Cache>`, `std::vector<Endpoint>`, all owned by the UI thread. Rows are addressed by a stable `RowId {provider, index, generation}`.
- **Walker:** `walk(dirfd) -> {bytes, files, skipped}`. Each walk has its own `unordered_set<pair<dev,ino>>` for files with `st_nlink > 1`. Entries with `nlink == 1` skip the set entirely, which keeps the set small.
- **History:** one thread, one pass per history file, newest-first. Produces `unordered_map<string, epoch>` and posts it once.
- **PATH resolver:** synchronous in Stage 1 (only `readlink` + `faccessat` on maybe 3k entries), so shadowing is visible on first paint.

## Decisions

| Decision | Solves | Worsens | Change it when… |
|---|---|---|---|
| **UI thread is the only writer of state**; workers send results via `ScreenInteractive::Post(closure)` | No locks or atomics around the inventory. Rendering always sees a consistent state | Every result costs one closure + redraw, and a burst of 100k tiny results would flood the loop | Measured `Post` rate > ~2k/s causes visible lag → batch results per worker every 50 ms |
| **Stage 1 runs synchronously before the first frame** | Simplest possible startup. No "empty table" flash | A slow provider delays first paint for all of them | Any provider's `scan()` exceeds 10 ms in profiling → move that provider to a worker and show `···` rows |
| **Bounded priority queue** (`std::priority_queue` + mutex + condvar) with priority = visible > cache > rest | Visible sizes arrive first. Plain stdlib | Re-prioritising on scroll is O(n) | The queue is > 10k jobs and scrolling stutters → switch to a two-level queue (visible / background) |
| **Generation counter per provider** for cancellation | A rescan after uninstall discards in-flight stale results without needing to stop workers | Old walks still burn I/O until they finish | Stale walks measurably delay a rescan → pass `std::stop_token` into the walker loop |
| **Mutation via `ScreenInteractive::WithRestoredIO` + `posix_spawnp` + `waitpid`**, argv vector, no shell | FTXUI restores termios / alt-screen for us. No shell, so package names can't inject commands | Commands that need shell features (pipes, globs) can't be expressed | A provider genuinely needs a pipeline → give that provider an explicit `/bin/sh -c` template with escaped arguments |
| **FTXUI** for the TUI | Flexbox-ish layout, tables, mouse, `Post`, and restored IO, all in one dependency | Pulls in ~20k LOC via FetchContent. Less control over diff rendering than raw ANSI | Render time > 4 ms per frame at 120×40 → hand-render the table component only |

## Mutation lifecycle

1. `u` → build `argv` from the provider. Refuse if the package is essential.
2. Modal shows `join(argv)` and the estimated reclaim. Default focus is Cancel.
3. `y` → `screen.WithRestoredIO([&]{ pid = posix_spawnp(argv); waitpid(pid); })()`. The child owns the TTY, so `sudo` prompts work.
4. Back in the TUI: re-run the scan (sync, < 10 ms) and keep only that provider's fresh rows. Rows that survived keep their size and usage; new or changed ones are measured synchronously. Until the generation counter exists, `u`/`p` wait for loading to finish so no worker holds an index into the rows being replaced.
5. Status line shows the exit status.

## Failure modes

| Failure | Behaviour |
|---|---|
| Provider parse error / missing file | That provider shows `✗` with the reason. The rest of the app is unaffected (`std::expected`, no throwing across the provider boundary) |
| `EACCES`/`EPERM` while walking | `skipped++`, continue. The size shows `+` |
| Mount boundary / symlink loop | Never crossed or followed (`st_dev` check, `AT_SYMLINK_NOFOLLOW`) |
| Child command fails or is killed | Exit status/signal shown. Rescan anyway, because a partial uninstall is still a state change |
| pkgtop crashes mid-session | FTXUI's signal handlers restore the terminal. Workers are `std::jthread`s that get joined on exit |
| History file huge or binary-corrupt | Stop after a 64 MB read cap. Skip invalid lines |

## Weakest point

Cold-cache sizing of very large trees (pnpm store, texlive, CUDA, 1M+ files) can take minutes and saturate the disk. The first fix is a persistent size cache in `~/.cache/pkgtop/sizes`, keyed by `(dev, ino, mtime)` of each root. It would be invalidated by the root's mtime, which is coarse but cheap. Add it when a cold start on a real machine takes more than 10 s to finish sizing.
