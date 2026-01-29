# MTAFeedSign
Real time display of the York Street F line and Jaystreet-Metrotech AC line written in C++ optimized for an ESP32

Project layout

```
MTAFeedSign/
├── README.md
└── esp32/
    ├── .gitignore
    ├── platformio.ini
    ├── .pio/
    ├── .vscode/
    ├── lib/
    │   └── nanopb/
    │       ├── pb_common.c
    │       ├── pb_common.h
    │       ├── pb_decode.c
    │       ├── pb_decode.h
    │       ├── pb_encode.c
    │       ├── pb_encode.h
    │       └── pb.h
    ├── proto/
    │   ├── gtfs-realtime.options
    │   └── gtfs-realtime.proto
    └── src/
        ├── gtfs-realtime.pb.c
        ├── gtfs-realtime.pb.h
        ├── main.cpp
        ├── MTAFeed.cpp
        ├── MTAFeed.h
        ├── TimeSync.cpp
        ├── TimeSync.h
        ├── wifi_secrets.h
        ├── WiFiManager.cpp
        └── WiFiManager.h
```

Useful links

- nanopb (Protocol Buffers for embedded C): https://github.com/nanopb/nanopb

Notes

- You will have to add your Wifi SSID and Password, reccomend making a file called wifi_secrets.h its what I used and is already included in the .gitignore

```
