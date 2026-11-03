# FAQ

**Q: Which ACE / printers are supported?**  
A: Both **ACE 1 Pro** (Gen 1, on KlipperGo-stack printers such as Kobra 3 / Kobra S1) and
**ACE 2 Pro** (Gen 2, on AVATA-stack printers such as Kobra X, and on KlipperGo-stack printers with
ACE 2 Pro support such as Kobra S1 Max). Use the firmware that matches **your ACE**, not your
printer: `ACE_V…` for Gen 1, `ACE2_V…` for Gen 2.

**Q: Does this work with ACE Gen 2 (ACE 2 Pro)?**  
A: Yes. The ACE 2 Pro CFW (v1.0.4+) and Flash Tool v1.0.1+ add Gen 2 support. Anycubic and Bambu Lab
spools are confirmed working; the other open vendors (OpenSpool, OpenPrintTag, Elegoo, TigerTag) are
implemented but not yet tested on real tags.

**Q: My printer is a KlipperGo printer (e.g. Kobra S1 Max) with an ACE 2 Pro — can I flash it?**  
A: Yes, with Flash Tool **v1.0.3+**. Earlier versions did not recognize the Kobra S1 Max and blocked
`ACE2_V…` images on KlipperGo printers. Flashing then ended with `update-failed` right after the
download ([#1](https://github.com/Jupsi/OpenCubic/issues/1)).

**Q: Flashing ends with `update-failed` right after the download**  
A: Update to the latest Flash Tool first. v1.0.2 fixed this for printer firmware 2.7+ (`model_id`),
v1.0.3 for the Kobra S1 Max / Kobra S1 V2 / Kobra 3 Max V2 (printer not recognized). If it still
fails, open an issue with the complete Flash Tool log.

**Q: My Bambu spool is not recognized**  
A: The firmware uses maximum TX power and RX gain for better read range. If the spool is still not
recognized, the NFC chip may sit too far toward the center of the spool hub, away from the reader.
Try rotating the spool slightly before inserting, or re-seat it so the hub tag is closer to the
reader side of the slot.

**Q: The slicer shows the wrong color for my spool**  
A: On ACE 1 Pro, update to firmware v1.3.863 or later (color byte order was fixed there). On ACE 2
Pro the color order is correct from the first CFW release.

**Q: After flashing, my ACE 2 Pro reports version V1.0.0 and the printer offers a firmware update**  
A: That is intentional. The CFW reports a deliberately low version so the cloud still offers the
official stock firmware — handy as a recovery path. Only accept that cloud update if you actually
want to return to stock (it will overwrite the CFW).

**Q: Can I go back to the original firmware?**  
A: Yes. Flash the matching original Anycubic firmware from the `originalFirmware/` folder using the
same flash tool (`ACE_V1.3.863…` for Gen 1, `ACE2_V1.1.31…` for Gen 2).

**Q: My printer shows as "unknown" in the Flash Tool**  
A: The printer type is detected by model ID. Not all model IDs are mapped yet, as printer
configuration files are not publicly available for every model. If you know the model ID of your
printer, please open an issue with the model ID and printer name, or submit a pull request with the
correct mapping in `firmware_packager.cpp`.
