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

---

## Which code is written here, and which is carried

Automated scanners walk the whole tree and attribute everything in it to this
project. Most of this repository by volume is the vendored upstream fork, so it
is worth stating the boundary plainly. `.gitattributes` marks it for tooling that
honours `linguist-vendored`.

**Written here:**

| path | what |
|---|---|
| `firmware/main/boards/m5stack/stackchan/` | the StackChan board — face, servo driver, camera, LED ring, privacy switches |
| `server/` | the Dockerfile, `config.yaml`, and the patch scripts |
| `tools/` | model-bench, claimcheck, status-server |
| `deploy/`, `docs/`, `.github/workflows/` | build scripts, documentation, CI |

Elsewhere under `firmware/` are upstream files with modifications, described in
the commit history.

**Carried, not written here:** the rest of `firmware/`, which is
[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) brought in by
`git subtree`.

### 🔴 Upstream's release tooling is present but unused

`firmware/scripts/versions.py` and `firmware/docker/firmware-builder/` are
upstream's own publishing pipeline. They upload builds to **Alibaba Cloud OSS**
and read `OSS_ACCESS_KEY_SECRET`, `VERSIONS_TOKEN` and `GITHUB_TOKEN`.

They are carried because the subtree is taken whole, and **nothing in this
project calls them**:

- no file outside `firmware/` references any of those three variables
- `.github/workflows/firmware.yml` never invokes those scripts
- **this repository's CI uses no secrets at all** — there is no `secrets.*`
  reference in the workflow

Releases here are built by `deploy/build.sh` and published as GitHub release
assets. Nothing is uploaded to any third-party storage service.
