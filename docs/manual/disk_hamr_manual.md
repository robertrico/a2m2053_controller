# Disk Hamr

Replacement controller board for the Apple UniDisk 3.5, model A2M2053.
Rev 1.0, firmware v1. Manual draft, 2026-10-25. Styled draft of this
file: `Disk_Hamr_Owners_Guide_DRAFT.pdf` beside it, photos still
placeholders.

## Design notes

For whoever builds the styled guide from this file. Read these, apply
them, and leave them out of the guide itself.

- **Photos are the manual.** This is a digital document; there is no
  print budget. Use the photos at full resolution, each one the full
  column width, caption beneath. No thumbnails, no photo beside the text.
  The step text sits above its photo.
- **Section 5, "Replacing the board": two steps per page.** Seven steps,
  so four pages; each page is two step blocks, each block the step text
  and its full-width photo. Step 7 (lid on) shares its page with the "ten
  minutes" closer.
- **Section 1 starts with "Which drive this is for"**, model number in the
  heading, on the first content page. Nothing goes above it.
- **Links.** Every link in the file is a real URL on bytehamr.com; keep
  them live in the PDF. The repository URL is still a placeholder.
- **Markers.** `[PHOTO n: ...]` is a photo still to be taken; the text
  after the colon is its caption, use it as the caption and nothing else.
  Show a labelled placeholder of the right shape until the file arrives.
  `[CONFIRM: ...]` is a fact awaiting a check; leave the text as written
  and drop the marker. Both disappear before the guide ships.
- **Shot list** (for the camera, never for the page): 1a underside label
  with the model number legible, front badge in a second frame. 2
  everything from the box laid out on the bench. 5a drive closed, rear
  toward the camera, underside screws called out. 5b lid off, the IF-83
  with all four cables attached; this is the photo the section hangs on,
  shoot it from above, evenly lit, cables labelled in post. 5c board out,
  standoffs visible. 5d new ribbon on the mechanism header, the marked
  edge and the header's pin 1 mark in the same frame. 5e Disk Hamr
  screwed down, no cables. 5f all four cables in, labelled J2 J3 J4 J5 in
  post. 5g drive closed and cabled, beside the IIe.
- **Voice.** The manual reads like the project page it links to:
  https://bytehamr.com/projects/unidisk-controller/ and its logs. Read two
  of them before styling anything. First person where it fits,
  contractions fine, numbers with units, one fact per sentence, said once.
  No em dashes. Shapes to avoid: "X, not Y" or "X, never Y" closers;
  "no A, no B, no C" triplets; fragment openers ("Clean first."); verdict
  punchlines ("The board is dead."); assurance words ("proven",
  "faithful"); grand summary sentences with no fact in them.
- **No invented diagrams.** Flowcharts, decision trees, "yes/no" boxes and
  icon figures that are not in this file add nothing a sentence doesn't.
  The figures are the photos, the one ribbon pin-1 sketch in section 3,
  and the chain diagram in section 7. If a section has no figure, it has
  no figure.
- **Logo and type.** The Byte Hamr hammer is in `assets/hamr_logo.svg`
  (a 23 x 44 mm vector, black on transparent, taken from the board's own
  silkscreen footprint). It sits small and horizontal in the page footer
  beside the page number, on the cover above the rule, and on the back
  page. Nowhere else. Display type on the board's silk is Katari; use it for
  the title and section numbers if its licence allows embedding in a
  distributed PDF, otherwise the current display face stays.
- **No marketing furniture.** No stat tiles, big-number callouts, pull
  quotes or taglines. The reader already owns the board. Section 11 is a
  specifications table and one sentence, nothing under it.


## 1. What this is

### Which drive this is for: the UniDisk 3.5, model A2M2053

Apple sold several 3.5-inch drives that look alike and share the same
Sony mechanism. Disk Hamr fits exactly one of them. Check before you buy
or open anything:

- **Front badge** reads "UniDisk 3.5". Beige case, matching the Apple IIe.
- **Label on the underside** reads model **A2M2053**.
- **Rear panel** has a DB-19 socket for the next drive in the chain, next
  to the captive cable.
- **Inside**, the controller board's silk reads **IF-83**, and it carries
  a 65C02, ROM, RAM and a large Sony gate array marked CXD1013.

It is **not** for:

- the Apple 3.5 Drive, model A9M0106 (platinum, badge "Apple 3.5 Drive",
  for the IIgs, IIc Plus and Macintosh). Same mechanism, different board,
  different protocol.
- the Macintosh 800K External Drive, model M0131, or any Macintosh-only
  drive.
- the Disk II, UniDisk 5.25 or any 5.25-inch drive.

[PHOTO 1a: The underside label, model A2M2053, and the front badge.]
[CONFIRM: the A9M0106 and M0131 model numbers, and that the IF-83 silk
is readable without removing the board.]

The UniDisk 3.5 is Apple's 800K floppy drive for the Apple II. Inside it
are a Sony mechanism and a controller board, Apple part IF-83 (service part
661-0307). When the controller board fails there is no repair path: it is
built around a Sony gate array with no datasheet and no replacement.

Disk Hamr replaces the whole controller board. The mechanism, the case, the
front panel, the LED, the eject button and the drive's own cables stay. The
board speaks SmartPort to the Apple and drives the Sony mechanism directly.
The firmware is new code on an RP2350. The original drive's ROM served as
a reference for protocol, status codes and timing.

**How it was built.** Every step from the first power-up of a bare
mechanism to the board in your hands is written up, with scope traces,
Apple screenshots and the mistakes, on the project page:
https://bytehamr.com/projects/unidisk-controller/. Section 10 of this manual is the
short version, with a link to each log.

### Tested on

- Apple IIe with the Liron card (Apple 3.5 Disk Controller), ProDOS.
- 800K double-sided ProDOS disks: boot, read, write, format, catalog, copy.
- Daisy chain: Disk Hamr in front of a stock UniDisk 3.5, and behind one.
- Disk swaps, write-protected disks, empty drive, eject and reset in the
  middle of a transfer, long write runs (1610 blocks in one session, no
  errors).

### Untested

- Apple IIc (with the UniDisk 3.5 ROM) and Apple IIgs (built-in disk port).
  Both talk the same SmartPort protocol, so it should work. It has never
  been plugged into either.
- Macintosh disks (524-byte blocks) and 400K single-sided disks. Out of
  scope by design.
- Any mechanism other than the Sony 800K double-sided unit the UniDisk 3.5
  shipped with.

### Who this is for

Someone who has already decided the controller board in their drive is
dead and wants the drive back. It assumes you can open the drive, handle
a ribbon cable and read a connector's pin 1 mark. It does not explain what
a floppy drive is.

## 2. In the box

- Disk Hamr rev 1.0, assembled and tested, firmware v1 loaded.
- One 20-way ribbon cable, IDC socket at both ends. It connects Disk Hamr's
  mechanism header (J2) to the mechanism's own 20-pin header. The IF-83's
  mechanism ribbon ends in a flat connector that does not fit a shrouded
  header, so this cable replaces it.
- This manual, as a QR code to the PDF.

Not included: a micro-USB cable. You only need one for a firmware update,
and you already have one.

[PHOTO 2: Everything in the box: the board, the ribbon, the card.]

## 3. Before you start

**The drive's cables carry three power rails.** The DB-19 and the ribbon
from it carry +5 V, +12 V and -12 V next to signal lines. The mechanism
ribbon carries +5 V and +12 V. A ribbon plugged in backwards or off by one
row puts a supply on a signal pin.

- Use the drive's own cables and the ribbon in the box. Do not improvise a
  DB-19 adapter.
- Match pin 1 on every connector before you push it home. Ribbon cables
  have a red or marked edge on pin 1; shrouded headers have a key notch;
  the mechanism header has a pin 1 mark. [CONFIRM: what the pin 1 mark on
  the Sony mechanism header looks like; photo it.]
- Power off the Apple before plugging or unplugging anything on the disk
  port.

During development a homemade DB-19 adapter put -12 V on a signal line
([the log](https://bytehamr.com/projects/unidisk-controller/logs/m6-a-loose-strand-put-12-v-on-wrreq/)). It destroyed
the IWM chip on the Liron card, which then had to be
[replaced with a donor](https://bytehamr.com/projects/unidisk-controller/logs/the-liron-card-runs-on-a-unidisk-s-iwm-now/).
That chip is socketed and a donor was found, but it is the most expensive
mistake available in this job and it is entirely avoidable.

**Static.** The board has a bare RP2350 module and CMOS level shifters.
Touch the chassis before you touch the board.

## 4. Is the board really dead?

Most "dead" UniDisks have dirty heads or a dry carriage rail, so start
there: isopropyl on the heads, a drop of light oil on the rail, and try the
disk again.

Here is how I knew mine were dead. With the drive plugged into the Liron
card, the Apple wouldn't boot ProDOS from any disk at all, including a
Disk II in another slot. The board was holding a line on the SmartPort
bus and nothing could get past it. Unplug the drive and the Apple boots
again. Two of my drives did exactly that, and cleaning changed nothing.

If you see the same thing, the board is the problem. It doesn't matter
which chip on it failed, since the gate array in the middle of it has no
datasheet and no replacement, and that's why this project replaced the
whole board instead of repairing it ([Going around the
CXD1013](https://bytehamr.com/projects/unidisk-controller/logs/going-around-the-cxd1013/)).

If the Apple boots fine with the drive attached and the drive just never
spins or never reads, the mechanism is the more likely suspect, and a new
controller board won't change that.

## 5. Replacing the board

Tools: one Phillips screwdriver.

1. **Open the drive.** Four screws on the underside, lid lifts off toward
   the rear. [CONFIRM: screw count and lid direction.]

[PHOTO 5a: The drive before opening. The four lid screws are underneath.]

2. **Unplug four cables from the IF-83.** Front-panel LED lead (2 pins),
   eject button lead (3 pins), the ribbon from the front DB-19 (20 way),
   and the mechanism ribbon (20 way, flat connector). Note which way
   each one faces.

[PHOTO 5b: Lid off. The IF-83 with its four cables: LED, eject, the ribbon from the front DB-19, the mechanism ribbon.]

3. **Unscrew the IF-83.** Four M3 screws into the standoffs. Keep the
   screws; Disk Hamr uses the same holes. [CONFIRM: four screws.]

[PHOTO 5c: The IF-83 out. The four standoffs take Disk Hamr next.]

4. **Set the old mechanism ribbon aside.** It stays with the IF-83. The
   mechanism end of the new ribbon goes where it was: pin 1 to pin 1.

[PHOTO 5d: The new ribbon on the mechanism header. The marked edge sits on the header's pin 1.]

5. **Place Disk Hamr** on the standoffs, rear DB-19 through the case
   opening. Four screws. The outline and the rear connector were fitted
   with 3D prints before the board was made
   ([outline](https://bytehamr.com/projects/unidisk-controller/logs/the-pcb-outline-fitted-with-a-3d-print/),
   [DB-19](https://bytehamr.com/projects/unidisk-controller/logs/m7-2-the-rear-db-19-fitted-with-a-3d-print/),
   [populated board](https://bytehamr.com/projects/unidisk-controller/logs/disk-hamr-printed-fitted-ordered/)).

[PHOTO 5e: Disk Hamr on the original standoffs, nothing plugged in yet.]

6. **Plug in the four cables.** The silk on Disk Hamr labels each header
   by its reference; the two 20-way headers sit at the top edge of the
   board, the two small ones on the tab beside the rear DB-19:
   - **J2**, top left: the new ribbon from the mechanism. Shrouded, keyed.
   - **J3**, top right, under the rear DB-19: the ribbon from the front
     DB-19. Shrouded, keyed.
   - **J5**, the 2-pin header on the tab: the front-panel LED lead. The
     shroud only lets it in one way.
   - **J4**, the 3-pin header beside it: the eject lead. Same.
   [CONFIRM: left/right as seen with the rear DB-19 away from you; check
   against photo 5f and reword to match.]

[PHOTO 5f: All four cables in: J2, J3, J4 and J5.]

7. **Lid on, screws in.** Plug the drive back into the Apple.

[PHOTO 5g: Closed up and back beside the IIe.]

Time: ten minutes.

## 6. First boot

Plug the drive into the Liron card's DB-19, or into the rear port of
another drive. Turn the Apple on.

- The front LED lights while the drive is being accessed and goes out
  between accesses. It flickers through a boot and a catalog.
  [CONFIRM: does the LED do anything at power-on before the first access?]
- `PR#5` (or whichever slot the Liron card is in) boots from the disk in
  the drive. ProDOS lists it as a 800K SmartPort device in that slot.
- The eject button ejects the disk, at any time, as the original did.

If the Apple does not boot and the drive is the only thing on the bus,
go to section 8.

## 7. Daisy chain

The rear DB-19 is a full SmartPort pass-through. Disk Hamr can sit in
front of a stock UniDisk 3.5 or behind one; both orders are tested
([the chain log](https://bytehamr.com/projects/unidisk-controller/logs/m8-the-daisy-chain-once-5v-reached-the-second-drive/)). The
Apple numbers the drives in cable order: the one plugged into the card is
D1, the next is D2.

Chain rules are the same as for the original:

- The last drive in the chain has nothing on its rear port. Disk Hamr
  senses this (the LASTONE line on rear pin 16) and tells the Apple it is
  the last device.
- A drive further down the chain only becomes visible after the one in
  front of it has been initialised, which the Apple does at boot. Plugging
  a drive onto the rear port after boot needs a reboot.

## 8. If it does not work

In order, stopping when it works.

1. **Apple will not boot at all with the drive attached.** Power off.
   Reseat J3 (the ribbon from the front DB-19) and check its pin 1 edge.
   Reseat the DB-19 cable at the Liron card.
2. **Apple boots, drive never spins.** Power off. Reseat J2 at both ends,
   pin 1 to pin 1. A reversed mechanism ribbon is the most likely cause of
   a dead-quiet drive.
3. **Drive spins, Apple says I/O error.** Try a known-good disk. Clean the
   heads. Try the disk in a working drive if you have one.
4. **LED never lights but everything works.** The LED lead is in J5 the
   right way round by the shroud, so check the LED board itself and its
   lead.
5. **Eject button does nothing.** Reseat J4. The button is a changeover
   switch; Disk Hamr reads the contact that closes when pressed.

Still stuck: open an issue on the repository with what the Apple shows,
which slot the card is in and what else is on the chain. [CONFIRM: repo
URL and whether issues are open.]

## 9. Firmware

The board ships with firmware v1, the code that passed the bench
milestones. It needs nothing from you, and the plan is that it never
will.

If a later version does ship, know this first: updating means taking
the board out of the drive. The Pico's USB jack and its BOOTSEL button
face the inside of the case and can't be reached with the board on its
standoffs, so an update is section 5 in reverse, the whole board out,
then the swap again. There is no update port on the case and I'm not
adding one; a drive from 1985 with a USB socket in the back is a
different project.

With the board out:

1. Unplug the drive from the Apple before anything else.
2. Hold the BOOTSEL button on the Pico module, plug a micro-USB cable
   from the Pico to a computer, release the button. A drive called
   `RP2350` appears.
3. Drag the new `.uf2` file onto it. The drive disappears when it is done.
4. Unplug USB, put the board back in (section 5, steps 5 to 7), plug the
   drive back into the Apple.

If you work out a tidy way to do this without the teardown, a cable
routed to a spare opening, a part, a bracket, send it. I'll link it from
the project page.

Source, schematic and firmware: [repository link]. MIT licence. Build it
yourself with the Pico SDK; the README covers it.

## 10. How it was built

Disk Hamr started as a Pico 2 and five level-shifter boards on a
breadboard, wired to a mechanism from a dead UniDisk. It was brought up in
nine milestones on the bench, each one logged with scope captures and
Apple screenshots:

- [M0](https://bytehamr.com/projects/unidisk-controller/logs/m0-the-strobe-on-every-power-edge/) and
  [M1](https://bytehamr.com/projects/unidisk-controller/logs/m1-the-mechanism-answers/): power-up behaviour of the
  mechanism, then status reads.
- [M2 and M3](https://bytehamr.com/projects/unidisk-controller/logs/m2-m3-the-whole-disk-once-5v-got-there/): motor, seek
  and eject, then raw flux capture decoded on a Mac.
- [M4](https://bytehamr.com/projects/unidisk-controller/logs/m4-the-board-reads-the-disk-itself/): whole-disk reads on the
  board itself.
- [M5](https://bytehamr.com/projects/unidisk-controller/logs/m5-the-unidisk-reads-a-disk-my-board-wrote/): writes and
  formats. A disk written by Disk Hamr was read back by a working UniDisk,
  and the other way round.
- M6: SmartPort on the RP2350. The Liron card's
  [first packet](https://bytehamr.com/projects/unidisk-controller/logs/m6-the-liron-s-first-packet-reaches-my-board/),
  then [ProDOS booting from a RAM disk](https://bytehamr.com/projects/unidisk-controller/logs/m6-prodos-boots-from-my-board/)
  through the card.
- [M7](https://bytehamr.com/projects/unidisk-controller/logs/m7-a-real-floppy-through-the-liron-card/): ProDOS booted from
  a real floppy. Every block the Apple read through Disk Hamr was compared
  byte for byte against the same disk in a working UniDisk, with the disk
  in, write-protected and with no disk.
- [M8 and M9](https://bytehamr.com/projects/unidisk-controller/logs/m8-the-daisy-chain-once-5v-reached-the-second-drive/):
  daisy chain with the working UniDisk, both orders, a full 800K disk
  copied across and booted from the copy. Then disk swaps, swap-then-write,
  write-protected disks, empty drive, eject and resets mid-transfer, 1610
  block writes without an error.
- The board: [routed](https://bytehamr.com/projects/unidisk-controller/logs/disk-hamr-the-pcb-is-routed/),
  [laid out again by hand](https://bytehamr.com/projects/unidisk-controller/logs/disk-hamr-laid-out-again-by-hand-this-time/),
  [printed, fitted and ordered](https://bytehamr.com/projects/unidisk-controller/logs/disk-hamr-printed-fitted-ordered/).

The board itself was measured against a real IF-83: connector positions,
the LED circuit (180 Ω pull-up, transistor sink), the eject switch wiring,
and the rear-port pinout, all with a meter on the original board. The
layout was printed five times and fitted in the case with the mechanism
before any copper was ordered.

All of it, with photos, scope traces and the things that went wrong:
https://bytehamr.com/projects/unidisk-controller/.

## 11. Specifications

| | |
|---|---|
| Controller | Raspberry Pi RP2350A (Pico 2 module) |
| Level shifting | 5 × SN74LVC8T245, 3.3 V to 5 V |
| Host interface | SmartPort over the Apple DB-19, Liron card |
| Mechanism interface | Sony 800K 20-pin |
| Rear port | DB-19 daisy chain: PH3, /ENBL pass-through, LASTONE |
| Power | +5 V and +12 V from the Apple through the host cable; no external supply |
| Board | 100 × 110 mm, 2 layer, fits the IF-83 standoffs and case opening |
| Firmware | C on the Pico SDK, MIT licence |

Schematic, board files, bill of materials and firmware source are in the
repository.
