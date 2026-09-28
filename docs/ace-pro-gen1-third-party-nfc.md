# ACE 1 Pro (Gen 1): reading third-party filament tags (NTAG / OpenSpool / Bambu)

Tested on **ACE 1 Pro (Gen 1)** with firmware `V1.3.863`.

Goal: make the ACE read **third-party** tags and hand their contents to the printer, to the
**multiACE** Klipper module and to the printer screen — no external reader, no hardware changes.

Stock behaviour: only Anycubic tags are accepted. Every other tag is marked as unrecognised
(`rfid = 1`), the `sku` field of the slot record stays empty, and neither the screen nor multiACE
shows anything.

## Approach

Do not rewrite the parsers — hook the branch that marks a tag as *unrecognised* and let the
firmware's own parsers see the tag it already read.

| | |
|---|---|
| base image | public community firmware (CFW) build, md5 `9f7b9a678a96caf98d6a08842d3ff971`, 113720 B |
| hook | `VA 0x08016B9A`: `f8 93 10 36` (`ldrb.w r1,[r3,#0x36]`) → `f0 0d f8 4d` (`bl 0x08023C38`) |
| stub | 108 B appended to the end of the image at VA `0x08023C38`: position-independent code + `0123456789ABCDEF` table + a 4-byte literal pointing at the UID buffer `0x20006224` |
| result | 113828 B, md5 `6148cfc52431fc235536c6d64b5334ef`, crc16 `0xAE92` |

The stub checks the raw UID buffer (`0x20006224`). If it holds data it writes `rfid = 2`,
magic `0x007B`, version `0x0065` and 14 hex characters of the UID into the slot record
(164 bytes each, base `0x20006518`); otherwise the previous behaviour is kept unchanged.

Because the record is then "recognised" and carries the tag text, OpenSpool / Bambu / Creality tags
are decoded by the firmware itself and appear as material + colour on the printer screen and in
multiACE.

## Verification

* `printer/objects/query?ace` → `"firmware":"CV1.3.863"` (leading `C` marks the community build)
  instead of the stock `V1.3.863`.
* `ACE_EXT_RAW METHOD=get_filament_info INDEX=0 FORCE=1 ACE=0` →
  `"sku":"OPENSPOOL","brand":"Creality","type":"PETG","color":[222,53,48]`, while the same command on
  stock returns `"sku":""`.

## Known limitations

* **One reader antenna per slot pair** — the pairs are `1&3` and `2&4` (inside a pair the neighbour is
  `slot xor 2`; confirmed by register aliasing, contexts `{0,2}` and `{1,3}` answer identically).
  Cross-reads are therefore possible and tag ownership can only be decided by moving the spools
  (rotate your own spool out of the field / rotate the neighbour out).
* The hook fires **only** when no stock parser understood the tag. When a parser did understand it,
  the code stays non-unique (literally `OPENSPOOL` for OpenSpool tags), which is why a consumer cannot
  map tag → spool. A second hook at `VA 0x08016B32` (the parser-succeeded branch) that appends `,`
  plus 14 hex characters of the UID fixes that; there are still 968 bytes of update-slot headroom.
* Flashing resets the slot and binding tables — bindings have to be re-created. multiACE with
  `spoolman_auto` can do that from the tag UID.
* Do not bind a spool by colour/code alone: parser codes are not unique.

## Full material

Full technical report (EN), Russian write-up, flasher, stub sources and all images — patched CFW,
the earlier stock+register-tunnel variant and clean stock for rollback:

**https://github.com/Godless50/ACE-PRO-v1.-NFC-UID**

> Flashing note: do **not** use `/etc/init.d/S60klipper start|restart` for this. That path triggers the
> printer's update check and re-flashes the vendor firmware over the CFW (observed on real hardware;
> the device had to be re-flashed).
