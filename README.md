![Build status](https://github.com/adepierre/Sniffcraft/actions/workflows/automatic_release.yml/badge.svg)
[![Discord](https://badgen.net/badge/icon/discord?icon=discord&label)](https://discord.gg/wECVsTbjA9)
[![Youtube](https://badgen.net/badge/Youtube/tutorial?color=FF0000)](https://youtu.be/wXOD41jI_Rg)

# SniffCraft

SniffCraft is a cross-platform C++ proxy which let you inspect the content of each packet sent through any minecraft client and server. It can run either with a GUI or in headless mode.

It works as a man-in-the-middle: instead of connecting directly to the server, you ask your client to connect to SniffCraft which then is connected to the server. All packets are transmitted to their original recipient and are simultaneously logged on-the-fly.

```
    ┌────────┐       ┌──────────────┐       ┌────────┐
    │        ├───────► - - - - - - -├───────►        │
    │ Client │       │  SniffCraft  │       │ Server │
    │        ◄───────┤- - - - - - - ◄───────┤        │
    └────────┘       └──────┬───────┘       └────────┘
                            │
                            ▼
                         Logfile
```

## Minecraft Beta 1.7.3

This version of SniffCraft only supports **Minecraft Beta 1.7.3** (protocol version 14). The wire format is defined in [`sniffcraft/include/protocol/packets.h`](sniffcraft/include/protocol/packets.h), which replaces the protocolCraft/botcraft layer SniffCraft was using for modern versions.

The Beta protocol is very different from the modern one, and this shapes what SniffCraft can do:

- there is no packet length prefix, no compression, no encryption and no connection states: a packet is just an id byte followed by its fields. SniffCraft has to parse every packet to find where it ends.
- because of this, **a stream can't be resynchronized** once a packet can't be understood (unknown id or invalid content). When this happens, SniffCraft logs a message and keeps forwarding the rest of that direction untouched, but stops logging it. Traffic is never altered. This is also what happens with the server list ping (`0xFE`), which is not a game packet.
- packets are always forwarded exactly as received, never re-encoded.

## Features

- GUI mode
- Packet logging with different levels of details (ignore packet, log packet name only, log full packet content)
- Detailed network usage recap
- Byte level packet inspection
- Logging raw packets at byte level
- Configuration (which packet to log/ignore) can be changed on-the-fly without restarting
- Automatically create a session file to log information, can also optionally log to console at the same time
- Save full session to binary file (`.scbin`) and reopen it later in the GUI
- No log at all is possible, in this case, SniffCraft becomes a pure proxy

Not supported anymore (they do not exist in Beta 1.7.3): authentication/encryption, compression, Replay Mod captures, DNS SRV lookup, transfer packets, mods plugins. Beta online-mode authentication relied on a session server that no longer exists, so use servers in offline mode.

## GUI support

If compiled with the cmake option SNIFFCRAFT_WITH_GUI, a GUI will appear when starting SniffCraft. This can be disabled by launching it with the ``--headless`` command line argument. In GUI mode, packets data are kept in memory while the session is displayed in GUI. This is usually not really an issue for regular usecase. However, if you plan to do some multi-hours long capture sessions or have a lot of sessions running simultaneously, it is recommended to use the ``--headless`` argument (or SniffCraft compiled without GUI enabled). This way, all data will only be stored in files and not in the RAM. SniffCraft binary files (**NOT** text files) can be reimported later in the GUI by simply dragging them onto SniffCraft window.

## Dependencies

You don't have to install any dependency to build SniffCraft, everything that is not already on your system will be automatically downloaded and locally built during the build process.

- [asio](https://think-async.com/Asio/) (git submodule in `3rdparty/asio`)
- the JSON implementation from [botcraft](https://github.com/adepierre/botcraft) (GPL v3), copied in `sniffcraft/include/sniffcraft/Json` and `sniffcraft/src/Json`

GUI dependencies (only if cmake option SNIFFCRAFT_WITH_GUI is set)
- [glfw](https://github.com/glfw/glfw)
- [Dear ImGui](https://github.com/ocornut/imgui)

Tests only (cmake option SNIFFCRAFT_BUILD_TESTS): [Catch2](https://github.com/catchorg/Catch2) v3

botcraft is no longer a dependency, the `3rdparty/botcraft` submodule can be removed.

## Build and launch

```
git clone --recursive <this repo>
cd SniffCraft
mkdir build
cd build
cmake -DSNIFFCRAFT_WITH_GUI=ON ..
cmake --build . --config Release
```

Once built, you can start SniffCraft by double clicking the executable (by default, compiled executable file can be found in ``bin`` folder next to the source code), or with the following command line:

```
sniffcraft <optional:--headless> <optional:conf/file/path>
```

conf/file/path is the path to a json file, and can be used to filter out the packets. Examples can be found in the [conf](conf/) directory. If no path is given, a default conf.json file will be created. With the default configuration, only the names of the packets are logged. The lists are in the `Packets` section: when a packet is added to an ignored list, it won't appear in the logs, when it's in a detail list, its full content will be logged. Packets can be added either by id or by name (the names are the ones displayed in the logs and the GUI, e.g. `ChunkData`). `ignored_clientbound`/`detailed_clientbound` apply to packets sent by the server to the client, `*_serverbound` to the other direction.

ServerAddress is the address of the server you want to connect to, as `host` or `host:port` (default port 25565). You can then connect your Beta 1.7.3 client to SniffCraft as if it were a regular server using <your computer IP:LocalPort>. If you are running SniffCraft on the same computer as your client, something like 127.0.0.1:LocalPort should work.

## Tests

```
cmake -DSNIFFCRAFT_BUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

`protocol_tests` checks `packets.h` against hand-written Beta 1.7.3 packets (parsing, truncation at every byte, serialization, byte ranges used by the GUI). `proxy_tests` (Linux/macOS) starts the real executable between a fake client and server and checks that traffic is forwarded byte for byte however it is split, and what is logged.

## License

GPL v3
