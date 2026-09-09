# UART5 smart-sensor line protocol

Normative specification for the open-core UART5 receive parser
(`UART5_IRQHandler` in `wb_control.c`). It describes the wire behavior
of the smart-sensor line as spoken by the 5-in-1 grayscale sensor and
the option/mechanic accessories, distilled from the observable behavior
of the legacy controller driver. The parser is validated against this
document, not against any vendor artifact.

## Physical layer

- UART5, 115200 8N1, receive-interrupt driven (RXNE only). Pins and
  init live in `wb_bsp.c` (`usart5_init`): PC12 TX out, PD2 RX in.
- The 5-in-1 grayscale sensor streams frames continuously once it has
  been configured over its port's I2C group (`sensor_operation`). The
  sensor must be plugged into port 5: that port carries the UART5 data
  line (every port provides power, only port 5 provides data).
- Only the receive direction matters here; the driver never transmits.

## State

Shared with `wb_bsp.c` (defined there; do not redefine):

| name | type | meaning |
|---|---|---|
| `Uart5RxBuf[64]` | u8[64] | frame assembly buffer |
| `Uart5RxCnt` | u32 | frame position; 0 = idle |
| `Uart5RxFrameState` | u32 | set on every sealed gray frame |
| `Gray[5]` | u16[5] | gray slot values, filled by the parser |

Parser-local state (implementation chooses its own names):

- payload counter, uint8: how many payload bytes are still expected
- mode flags: line-option armed, mechanic armed, option-module armed
- dance-motion register, uint8: stored for a future consumer; nothing
  in the open core reads it today

## Definitions

- **position** — `Uart5RxCnt`. Positions 0/1/2 hold the prefix, tag and
  header bytes; positions >= 3 collect payload. Every stored byte is
  written to `Uart5RxBuf[position]` and the position increments after
  the store.
- **header byte** — the byte stored at `Uart5RxBuf[2]` as the position
  moves 2 -> 3. For most frames its value is the payload length.
- **seal byte** — the byte received while the payload counter is 0. It
  finalizes the frame and is NEVER stored. In back-to-back streams the
  seal byte is the first byte of the following frame (it is consumed).
- **full reset** — position = 0, all three mode flags cleared, plus one
  SR read + DR read pair (clears error flags on STM32F1 by read).

## Frame grammar (wire order)

| type | prefix | tag | header | payload | checksum |
|---|---|---|---|---|---|
| gray data | 0xAA or 0xFE | 0x55 | L = payload count | L bytes | none; frame carries one trailing byte (unvalidated) |
| line option | 'w' | 'h' | 0x17 | 17 bytes | `~sum(header + payload[0..15]) == payload[16]` |
| mechanic | 0xAA or 0xFE | 0xAA | H | H+1 bytes | `-(sum(payload[0..H-1])) == payload[H]` (mod 256) |
| option module | 0xAA or 0xFE | any <= 0x3F except 0x55/0xAA | filler/data byte | L bytes not counting 0xFE | none |

All arithmetic is modulo 256. The tag position is evaluated in the
order gray (0x55), mechanic (0xAA), option module (<= 0x3F); a tag byte
> 0x3F that is none of the above aborts to idle.

## Rules

1. **Idle (position 0).** 0xAA or 0xFE starts every frame type except
   line option: store it, position = 1. 'w' starts a line-option frame:
   store it, position = 1, arm line-option mode. Any other byte clears
   the line-option mode and stays idle (other mode flags untouched).
2. **Tag (position 1).** If line-option mode is armed: 'h' advances
   (store, position = 2); any other byte performs a full reset. Else
   evaluate the tag per the grammar table: 0x55 -> gray; 0xAA ->
   mechanic (arm mechanic mode); <= 0x3F -> option module (arm
   option-module mode; the tag byte doubles as L and is stored);
   > 0x3F -> full reset.
3. **Header (position 2).**
   - line option: 0x17 sets the payload counter to 17 and advances;
     any other byte full-resets.
   - mechanic: payload counter = header + 1; advance.
   - gray: payload counter = header; advance.
   - option module: if the byte is 0xFE, payload counter increments by
     one; advance. (0xFE is a filler for this frame type.)
   In every advancing case the byte is stored at `Uart5RxBuf[2]`.
4. **Payload (position >= 3, counter > 0).** Store the byte, increment
   the position, decrement the counter — except that for option-module
   frames a 0xFE byte is stored but does NOT decrement the counter.
   When the counter reaches 0 the parser stops storing: the next byte
   is the seal.
5. **Seal.** The seal byte finalizes the frame per its type and is
   dropped:
   - gray: update Gray[] (below).
   - line option: verify the checksum; on success store
     `Uart5RxBuf[3]` in the dance-motion register; on failure perform a
     full reset.
   - mechanic: verify the checksum; on success keep mechanic mode armed
     (consecutive mechanic frames); on failure perform a full reset.
   - option module: frame consumed; keep option-module mode armed.
   After a seal the position is 0 in all cases.
6. **Overflow guard.** If a store would target `Uart5RxBuf[64]` or
   beyond, abandon the frame and perform a full reset (the buffer is
   64 bytes; conforming frames never get close).
7. **Interrupt housekeeping.** If the interrupt fires without RXNE set
   (overrun/noise): perform a full reset. On the RXNE path read DR
   once. Every exit reads SR then DR once (F1 flag clearing by read).

## Gray update

For a sealed gray frame with declared payload length L, for
k = 0 .. min(4, (L+1)/2 - 1):

    Gray[k] = payload[2k] | (payload[2k+1] << 8)

i.e. five little-endian 16-bit values taken from payload bytes 0..9;
a frame with fewer than 10 payload bytes updates only the values it
fully carried. `Uart5RxFrameState` is set on every sealed gray frame.
The parser does not scale or map values; slot semantics (Gray[0] is
sensor slot 5 ... Gray[4] is sensor slot 1, raw 0..2500, dark = high)
are applied by `wb_bsp.c` (`get_Gray` / `get_Gray_Line`).

## Deliberate deviations from the legacy driver

None of these are observable with conforming traffic; they improve
recovery from corrupt streams and are audit-visible on purpose:

1. The legacy driver had no overflow guard on most collect paths and
   would corrupt memory for a declared length > 61; this parser full-
   resets instead (rule 6).
2. The legacy driver read all five Gray values from the assembly
   buffer regardless of L (stale bytes for L < 10); this parser updates
   only fully received values.
3. The legacy driver retried a checksum-failed frame in place until
   more than 32 bytes had accumulated, then reset; this parser resets
   immediately on checksum failure.
4. The legacy driver set a write-only "option frame done" flag with no
   reader; it is not carried over.
5. Some legacy exit paths read SR/DR twice; one read pair per exit is
   sufficient (flag clearing by read is idempotent).

## Non-goals

- Transmit path: the driver never sends on this line.
- Option-module and mechanic payloads: collected for protocol
  completeness; the open core exposes no consumer yet.
- Line-option frames are only reachable when the paired app sends them
  over this line; nothing in the whale API arms them.
