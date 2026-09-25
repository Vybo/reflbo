# Third-party material

Code, fonts and other assets in this repository that come from other projects.
Each item keeps its original licence, and copied or adapted files keep their
licence headers. Add a row whenever you copy or adapt anything.

| Item | Source | Licence | Where | Notes |
|---|---|---|---|---|
| Unity v2.6.1 | https://github.com/ThrowTheSwitch/Unity | MIT (`test/host/third_party/unity/LICENSE.txt`) | `test/host/third_party/unity/` | Host unit-test framework; unmodified |
| DejaVu Sans 2.37 (Regular, Bold) | https://github.com/dejavu-fonts/dejavu-fonts | Bitstream Vera + Arev font licence, DejaVu changes public domain (`assets/fonts/LICENSE-DejaVu.txt`) | `assets/fonts/`; bitmaps rendered into `components/gfx/fonts/` | Rasterised by `tools/gen_fonts.sh`; C symbols use neutral `sans_*` names as the licence reserves the font names |
