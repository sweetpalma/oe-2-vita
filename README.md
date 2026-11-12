# Orcs & Elves II — PlayStation Vita

PlayStation Vita port of **Orcs & Elves II**.

## Installation

Download the [latest release](https://github.com/sweetpalma/oe-2-vita/releases), and install it to your Vita. That's it. Enjoy.

## Building

Ensure `data/orcsandelves2.bar` and `data/orcsandelves2.mod` are present.

### Docker

Building with Docker is the easiest way. Just ensure it's installed and run:

```sh
docker compose run --rm --build vita
```

### Local VitaSDK

You may also use the VitaSDK directly. It must be installed at `tools/vendor/vitasdk` (or pointed to by `VITASDK`). Then run:

```sh
python3 tools/build_vita.py
```
