# Third-party notices

This project is licensed MIT — see [LICENSE](LICENSE). That file is kept as the
bare licence text and nothing else, so that automated tooling can identify it;
the attribution that used to live at the bottom of it is here instead.

## firmware/

`firmware/` is a fork of [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32),
brought in with `git subtree`. It carries its own MIT licence, preserved
unchanged at [`firmware/LICENSE`](firmware/LICENSE):

    Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.

That licence continues to govern the upstream code in that directory. The
modifications in it — the StackChan board, the face, the servo driver, the
camera work and the fixes described in the commit history — are covered by the
MIT licence in [LICENSE](LICENSE).

## server/

The server side builds on
[xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)
(xinnan-tech), which is **not redistributed here**: `server/` contains a
Dockerfile that starts from their published image and a set of patch scripts.
Their licence governs their code.

## Pulled in at build time

ESP-IDF, LVGL, esp-sr and the other components fetched during the firmware build
carry their own licences, unchanged and unbundled. None of them are vendored into
this repository.
