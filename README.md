# anydesk-dark-mode

Dark mode for **AnyDesk 8.x on Linux**, plus a fix for the "do not start
AnyDesk as root" dialog.

AnyDesk's Linux client is light-only: its colours are hardcoded, it forces
`gtk-theme-name = "Adwaita-light"` at startup, and system GTK themes /
`GTK_THEME=` do not affect the main window. This project replaces those colours
at runtime with a small `LD_PRELOAD` shim, and adds two optional binary patches.

Tested on **Parrot Security 7.4 (Debian 13) arm64**, X11, **AnyDesk 8.1.0**
(official `anydesk_8.1.0_arm64.deb`). The shim itself is architecture
independent; the binary patch offsets are not (see
[Compatibility](#compatibility)).

## What it does

- **Dark UI** — rewrites AnyDesk's runtime GTK CSS and its cairo-painted
  surfaces (white cards, grey buttons, tabs, chips) to a dark palette.
- **Readable text** — keeps light text light, and lifts dark-grey labels that
  would otherwise disappear on dark backgrounds.
- **Visible icons** — inverts small monochrome glyph pixbufs (toolbar icons).
- **Root warning** — skips the modal dialog when the GUI runs as `root`.
- **Dark native widgets** — forces `Adwaita-dark` so GTK-drawn menus and
  dialogs match the main window.

Nothing is patched globally: the shim and the private GTK config are only used
by the AnyDesk process launched through the wrapper.

## Files

| Path | Purpose |
| --- | --- |
| `src/addark.c` | the `LD_PRELOAD` shim |
| `build.sh` | build / install the shim |
| `bin/anydesk-dark` | launcher (sets the shim and private GTK config) |
| `bin/anydesk-repatch` | applies the two binary patches (8.1.0 arm64) |
| `config/gtk-3.0/` | private dark GTK config for AnyDesk |
| `patches/anydesk-init-make-pidfile.patch` | SysV init script fix (chroot / no systemd) |

## Install

Dependencies (build):

```sh
sudo apt install build-essential pkg-config libgtk-3-dev
```

Build and install the shim and launcher:

```sh
sudo ./build.sh --install
sudo install -Dm755 bin/anydesk-dark /usr/local/bin/anydesk-dark
sudo install -Dm755 bin/anydesk-repatch /usr/local/bin/anydesk-repatch

mkdir -p ~/.config/anydesk-dark/gtk-3.0
cp config/gtk-3.0/gtk.css config/gtk-3.0/settings.ini ~/.config/anydesk-dark/gtk-3.0/
```

Apply the binary patches (only for the 8.1.0 **arm64** build):

```sh
sudo anydesk-repatch
```

Point the desktop menu entry at the wrapper (optional):

```sh
sudo sed -i 's|^Exec=/usr/bin/anydesk |Exec=/usr/local/bin/anydesk-dark |' \
  /usr/share/applications/anydesk.desktop
```

Launch:

```sh
anydesk-dark
# or, if you run the GUI as root:
sudo -E anydesk-dark
```

The launcher passes all arguments through (`anydesk-dark --get-id`, etc.).

## How it works

AnyDesk builds its interface from three layers, all handled by the shim:

1. **Generated GTK CSS** — `gtk_css_provider_load_from_data()` is intercepted
   and the CSS is rewritten (light palette → dark palette, white surfaces,
   black text, borders).
2. **cairo-painted surfaces** — flat buttons, cards, tabs and chips are filled
   with hardcoded `#F0F0F0` / white via `cairo_fill()`, `cairo_fill_preserve()`
   and `cairo_paint()`. The shim darkens opaque light-grey fills of at least
   ~30×30 px. Cairo's text routines do not go through the public `cairo_fill()`,
   so glyphs keep their own colour.
3. **Icon pixbufs** — small dark monochrome icons are inverted in
   `gdk_cairo_set_source_pixbuf()` so they stay visible on dark buttons.

Dark-grey text is lifted in `cairo_show_text*()`; Pango text is additionally
guarded against fill remapping. The private `XDG_CONFIG_HOME` supplies dark
rules for the few widgets GTK draws itself.

## Binary patches

`anydesk-repatch` verifies the expected bytes before writing anything and is
safe to run repeatedly.

| Offset | Patch |
| --- | --- |
| `0x397470` | `cbnz w0` → `b` — skips the root warning dialog |
| `0x1c0d688` | `"Adwaita-light"` → `"Adwaita-dark"` — dark native GTK widgets |

The first patch is required only when running the GUI as `root`; the second is
cosmetic.

## Compatibility

- The **shim** should work with any GTK3 AnyDesk 8.x build, since it targets
  API behaviour rather than byte offsets.
- The **binary patches** are for the official **8.1.0 arm64** package:

  ```
  anydesk_8.1.0_arm64.deb
  sha256 ba8edcd17ac18824b7de34ae42c013dc859c43fad384bee4f6fb598d7d188dc2
  ```

  Any other version or architecture will have different offsets. Do not force
  the patch; re-derive the offsets for that build.

- Tested on X11. Wayland was not tested (the shim is display-server agnostic,
  but AnyDesk's own behaviour may differ).

## Running the service

On a normal systemd host:

```sh
sudo systemctl enable --now anydesk
```

Inside a chroot where PID 1 is not systemd (e.g. Android/Termux:X11 setups),
use the SysV script instead:

```sh
sudo /etc/init.d/anydesk start
```

AnyDesk's `--service` daemon does **not** write a pidfile, so the stock init
script's `status` and `stop` are broken. Apply the included fix:

```sh
sudo patch -p1 -d / < patches/anydesk-init-make-pidfile.patch
```

It only adds `--make-pidfile` to `start-stop-daemon` in `d_start()`.

## Rebuild / upgrade

After rebuilding the shim:

```sh
sudo ./build.sh --install
```

After an AnyDesk package upgrade, the binary patches are gone and the shim may
need re-checking:

```sh
sudo anydesk-repatch
```

`/usr/bin/anydesk.orig` is kept as the pristine backup.

## Uninstall

```sh
# stock AnyDesk back
sudo cp -a /usr/bin/anydesk.orig /usr/bin/anydesk

# remove the local bits
sudo rm -f /usr/local/lib/libanydesk-dark.so /usr/local/bin/anydesk-dark \
           /usr/local/bin/anydesk-repatch
rm -rf ~/.config/anydesk-dark
```

Then launch `/usr/bin/anydesk` (light) as before.

## Disclaimer

Unofficial. Not affiliated with AnyDesk Software GmbH. Patching a third-party
binary can break with updates and may interact with the licence terms — use at
your own risk. The shim only changes colours; it does not modify networking,
authentication or session handling.

## License

MIT — see [LICENSE](LICENSE).
