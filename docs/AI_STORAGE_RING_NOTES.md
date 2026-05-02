# AI Storage Ring Notes

This document describes the Flash history design. Preserve these constraints
unless the user explicitly asks for a new storage design.

## Storage Model

- Realtime samples are RAM-only.
- Flash stores 1-minute aggregates only.
- `/api/history` without `range` returns RAM realtime rows.
- `/api/history?range=...` returns minute aggregate rows.
- `/api/log.csv` exports minute aggregate CSV.

## File Layout

- Header: `/env_min.hdr`
- Segments: `/env000.bin` through `/env095.bin`
- Records per segment: `105`
- Segment count: `96`
- Total capacity: `10080` minute records
- Retention: about 7 days at 1 point/minute

## Header Fields

`MinuteRingHeader` tracks:

- `magic`
- `version`
- `capacity`
- `writeIndex`
- `count`
- `totalWrites`

Logical read order must be oldest to newest:

```text
start = (writeIndex + capacity - count) % capacity
slot = (start + logicalIndex) % capacity
```

Do not read by raw physical segment order after wrap.

## Append Behavior

- Each minute aggregate is appended to the current segment.
- At segment slot 0, the segment file is removed before writing the new cycle.
- Header is saved after appending the record.
- The current in-progress minute is appended to API history responses but is not
  committed to Flash until the minute changes.

## Why Segmented Ring Exists

Rejected design:

- One large fixed LittleFS file with random writes.

Measured failure:

- Random write to the large file caused about `8645 ms` persist duration on this
  board.

Current design:

- Segmented append ring.
- Initial measured writes around `30-60 ms`.
- Later health reports may show individual writes in the hundreds of ms, but
  without observed loop stalls in current tests.

## Risks

- Host-side `tools/test_ring_log.py` verifies order and wrap logic only.
- It does not prove LittleFS wear behavior, power-loss consistency, or segment
  deletion latency.
- `/api/history?range=all` can output up to `10080` minute records plus current
  in-progress minute. Keep streaming/chunking and `yield()` behavior.
- If power-loss consistency becomes critical, header journaling or dual headers
  may be needed.
