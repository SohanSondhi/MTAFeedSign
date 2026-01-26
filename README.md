# MTAFeedSign
Real time display of the York Street F line and Jaystreet-Metrotech AC line written in C++ optimized for an ESP32

Project layout

```
MTAFeedSign/
├── README.md
└── esp32/
    ├── platformio.ini
    ├── src/
    │   └── main.cpp
    └── lib/
        └── nanopb/
            ├── nanopb_src/
            ├── proto/
            └── gen/
```

Useful links

- nanopb (Protocol Buffers for embedded C): https://github.com/nanopb/nanopb

Notes

- This repository currently contains a minimal ESP32 project skeleton; add real nanopb sources and your .proto files under `esp32/lib/nanopb`.

```
