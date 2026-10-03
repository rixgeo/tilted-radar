# Third-party notices

## The-Definitive-UI radar source

This project vendors the radar subsystem and selected shared support code from
**The-Definitive-UI**, commit `3a3b62d148e2606d1cab8e9755fc74d4fc70060e`:
<https://github.com/multimaks2/The-Definitive-UI>

The vendored code lives in [`third_party/the-definitive-ui/`](third_party/the-definitive-ui/).

The upstream project is licensed under the GNU GPL version 3. See
[`LICENSE`](LICENSE), [`third_party/the-definitive-ui/LICENSE`](third_party/the-definitive-ui/LICENSE),
and the license headers retained in the vendored source files. The vendored
radar integration is adapted for this plugin-sdk host; unrelated HUD modules
are not included.

The upstream repository's binary/artwork assets are not included here. The
radar uses the game's `radar00.txd` through `radar144.txd` dictionaries for map
tiles and the native HUD `radardisc` sprite for its radar face. The 12-by-12
world grid has 144 geographic positions; `radar144` is also loaded as the
additional native tile dictionary.

## Runtime deployment

See the [installation section of the README](README.md#installation).
