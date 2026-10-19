# Branding

Drop your own images here before building (`eden/ps4/build.sh --no-configure` repackages quickly):

| File | Size | Used as |
|---|---|---|
| `icon0.png` or `icon0.jpg` | any square picture (it is centre-cropped and scaled to 512x512) | the app icon on the PS4 home screen |
| `pic1.png` or `pic1.jpg` (optional) | any 16:9 picture (scaled to 1920x1080) | the background behind the app on the PS4 home screen |

The included `icon0.png` is Eden's logo (`eden/dist/dev.eden_emu.eden.svg`, Eden Emulator Project,
GPL-3.0-or-later) with a "4" badge set in Nunito ExtraBold (SIL Open Font License). Delete it to get Eden's
plain logo, or replace it with your own.

`preview/` holds screenshots of the HOME menu for the README, rendered on a PC by
`eden/ps4/ui-preview/build.sh` with made-up games.

Only use pictures you have the right to publish.
