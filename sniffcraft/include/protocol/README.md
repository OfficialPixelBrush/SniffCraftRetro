# Beta 1.7.3 protocol definition

`packets.h` is the packet definition for Minecraft Beta 1.7.3 (protocol 14) this project is built on.
It was provided together with the project, but it includes headers that were not, so these files are
**stand-ins written for SniffCraftRetro**, inferred from how `packets.h` uses them:

`base_structs.h`, `base_types.h`, `constants.h`, `dimensions.h`, `inventory/item_stack.h`,
`network_stream.h`, `numeric_structs.h`, `packet_data.h`, `packet_ids.h`, `quantized_types.h`

If you have the originals, you can drop them in. What the rest of SniffCraft relies on:

- `NetworkStream` (see `network_stream.h`) must throw `NetworkStream::NotEnoughData` when the input ends
  inside a packet (that is how the proxy knows to wait for more bytes) and `NetworkStream::ProtocolError`
  for invalid data. It also needs `RequireCount(count, itemSize)`, `Position()`, `Skip(n)` and
  the optional tracing of byte ranges (`Span`) used by the GUI.
- `Float2`, `Int8_2`... that `packets.h` puts in anonymous unions must stay trivially default constructible.
- `PacketId` values are the Beta 1.7.3 ids. SniffCraft's packet names come from the struct names in
  `packets.h` (see the list in `src/Protocol.cpp`); a new packet needs to be added there too.

## Changes made to `packets.h`

Each one is marked with a `SniffCraftRetro:` comment.

1. `Explosion::Deserialize` read `numberOfDestroyedBlocks` bytes, but each record is 3 bytes (x, y, z
   offsets), so the array is `3 * count` bytes long. With the original code the stream is desynchronized
   after every explosion. (`Serialize` was already consistent with 3 bytes per record.)
2. `RequireCount(...)` calls before the four `resize()` calls driven by a length read from the wire
   (`ChunkData`, `SetMultipleBlocks`, `Explosion`, `FillContainer`), so a corrupted length can not trigger a
   huge allocation or a wrong `size_t` conversion from a negative value.

## Things noticed and left alone

- `PlaceBlock::Serialize` always writes the item count and damage, even when the item id is -1 (the
  `Deserialize` side handles it correctly). SniffCraft never re-encodes packets, so it is not affected.
- `SpawnObject` tests `ownerEntityId != 0` where Beta 1.7.3 tests `> 0`. Equivalent for real traffic
  (entity ids are positive).
- `PlayerPosition`/`PlayerPositionAndRotation` store the wire fields in the same order in both directions
  (see the comment in `packets.h`). SniffCraft swaps `position.y` and `cameraY` for server to client
  packets when displaying them.
