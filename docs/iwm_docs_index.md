# IWM document index (for the A2M2053 replacement controller)

Scope: the fifteen IWM documents listed below (twelve original files plus three Xerox scans added later: sections 1.13 to 1.15), read page by page from the scan images on 2026-10-07. The UniDisk PDFs, "Apple II IWM Port Info.pdf" and the byte-identical `apple2_IWM_*` duplicates were not read again. The three added scans contain no technical content that is not already in the other twelve, so section 2 cites the earlier files. Where an added scan carries the same text, it is cross-referenced in its own summary.

Conventions:

- "p. N" is the PDF page number (the "Page 000N of 00NN" footer added by the scanner). Where the original document has its own page or sheet number, it is given as well, for example "p. 11 (sheet 10 of 14)".
- Quoted text is transcribed as printed, including the original spelling mistakes. Slashed zeros ("Ø") in the typewritten originals are written as "0".
- [illegible] means the scan cannot be read. [unsure] means it can be read, but the reading or its meaning is not certain.

---

## 1. Per-document summaries

### 1.1 APPLE 2 IWM INFO 198205.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. Addendum to Software Control of the IWM or Disk ][ Controller". Apple Computer, Inc., "ca. May 1982", "PREFACE SECTION ONLY". No author is named. Scanned by Brutal Deluxe Software, 31 December 2008.
- **Pages:** 2 (a cover page and 1 page of content).
- **Summary:** Only the preface of an addendum to "Software Control of the IWM or Disk ][ Controller". It lists the operations that the full document covers: selecting the drive, turning the motor on, sensing the write-protect switch, reading and writing data bytes, reading and writing self-sync bytes, and seeking. It says that no executable code is given "since the operation within the timing loops depends on other factors". There is no technical data. Nothing in it concerns 3.5-inch drives.

### 1.2 APPLE 2 IWM INFO 19820531.pdf
- **Title as printed:** "Floppy Disk I/O Controller IWM Chip Memo". Peter Baum, Apple Computer, Inc., May 31, 1982. Topic: "Software control of Disk II/IWM controller". The memo is addressed to Norman Leung and Conrad Chen.
- **Pages:** 2.
- **Summary:** Review comments on the draft of "Software Control of Disk II/IWM Controller". The memo asks for timing-critical code examples, because some 65C02 instructions take a different number of cycles from the NMOS 6502. It reports that ProDOS does not follow the self-sync write guidelines exactly, giving cycle gaps of 40, 36, 32, 32 and 36 between sync bytes, address-mark bytes ($D5, $AA, $AD) and nibbles. It also asks where the address and data marks fit in the document. Everything in it concerns the 5.25-inch drive and software. Nothing concerns 3.5-inch drives.

### 1.3 APPLE 2 IWM INFO 19831129.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. Misc. IWM Operation Notes with handwritten Corrections". Bob Bailey, Apple Computer, Inc., November 29, 1983.
- **Pages:** 4 (a cover page and 3 pages of notes marked 1/3 to 3/3).
- **Summary:** Internal notes on how the IWM works, with handwritten corrections. The "Commands" section covers how L6, L7 and DMOTOR-ON decode commands, and the 1-second timer. The "Reading" section covers how RDDATA is synchronised to FCLOCK, the 1 / 01 / 001 spacing windows, and the asynchronous versus synchronous read register. The last page covers asynchronous and synchronous writes, the write handshake bit, underrun, and /WRREQ. It has the most detailed account of write timing in FCLK periods of any document in this set.

### 1.4 APPLE 2 IWM INFO 1984-01.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. 6502 assembly program to determine if IWM or Disk ][ disk state machine is installed". Apple Computer, Inc., January 1984. The assembler banner reads "15-JAN-84 21:28".
- **Pages:** 3.
- **Summary:** A 51-line 6502 program that detects an IWM. It turns the motor off, writes $04 (timer disable) to the mode register with Q6H/Q7H, turns the motor on, waits about 10 ms, then reads the status register with Q7L. It masks the result with $1F and compares it with $04. If they match, the chip is an IWM. The program then writes $00 to restore the mode register. It shows how software reaches the mode and status registers and confirms that status bits 0 to 4 echo the mode register.

### 1.5 APPLE 2 IWM INFO 19840228.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. Engineering Drawing # 343-0041-A. IC -- Integrated Woz Machine (IWM)". Bob Bailey, Apple Computer, Inc., February 28, 1984. "INCLUDES DISK II ANALOG BOARD SCHEMATIC".
- **Pages:** 16. These are the cover, 14 sheets of drawing 343-0041-A, and the schematic.
- **Summary:** The released Apple engineering drawing of the IWM. Drawn by A. Bader 1/83. Revision A ECOs are C288 (initial release), F115 (changed test program limits) and F221 (page 2, added warning section, 2/23/84 [unsure]). The text matches the Revision 19 device specification. It also has a WARNING section about synchronous mode with the NMOS 6502, and some changed DC and AC values: Icc 60 mA, a separate PHASE 1 drive specification, and a different test condition for the RDDATA pull-up. Sheet 11 (PDF p. 12) is scanned with its left half cut off. The last page is the "Disk II Analog Board" schematic (Apple 050-0007-01, 1978). It uses ULN2003 drivers, 74LS125 buffers and an MC3470 read amplifier.

### 1.6 APPLE 2 IWM INFO 19840510.pdf
- **Title as printed:** "Software Control of the Disk II or IWM Controller". Norman Leung, prepared April 26, 1984, "Revision 1, May 10, 1984". Apple Computer, Inc.
- **Pages:** 9 (a cover page and 8 pages of content).
- **Summary:** A programmer's guide to the Disk II/IWM soft switches $C080 to $C08F (plus slot × 16). It gives the Q6/Q7 state table and the select-drive, motor-on and sense-write-protect sequences. It also covers reading data bytes, writing data bytes in exactly 32 cycles, writing self-sync bytes in exactly 40 cycles, and synchronisation on D5 AA 96 and D5 AA AD. It ends with 5.25-inch four-phase stepper seek timing (11.5 ms, 0.1 ms, 36.6 ms, and the Figure 2 waveforms). Everything in it is for the 5.25-inch drive.

### 1.7 APPLE 2 IWM INFO 19840524.pdf
- **Title as printed:** "Floppy Disk I/O Controller IWM Chip Memo". Peter Baum, Apple Computer, Inc., May 24, 1984. Topic: "Differences between the IWM and the Apple // Disk Controller".
- **Pages:** 3.
- **Summary:** Lists the known differences between the IWM and the Disk II state machine:
  - Only the most significant bit of the write-protect status should be tested.
  - The IWM malfunctions if Q6H and Q7H are both set while the motor is off (Q4L).
  - The IWM does not need the 6502 false-read cycle.

  It also notes that the IWM has never been sold on a slot card, and that the //c uses a GTE/NCR 65C02 whose cycle times differ from the NMOS 6502. It lists reference documents.

### 1.8 APPLE 2 IWM INFO APPLE 2C.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. Apple //c Computer External Floppy Disk Port Interface". Apple Computer, Inc., "ca. 1984".
- **Pages:** 2.
- **Summary:** Pinout of the //c DB-19 external disk port, cross-referenced to the 20-pin header of the Apple //e controller card. Notes explain why a straight ribbon cable will not work between a //e card and a //c drive in either direction. The external drive is "accessed as slot 6 drive 2". This is the only connector pinout in the set.

### 1.9 APPLE 2 IWM PARKER 1994-02.pdf
- **Title as printed:** "Controlling the 3.5 Drive Hardware on the Apple IIGS". Neil Parker (nparker@cie.uoregon.edu, parker@corona.uoregon.edu), Version 1.00, February 1994. Copyright 1994 Neil Parker. Captured from http://www.mac.linux-m68k.org/devel/iwm.php on 31 December 2008.
- **Pages:** 25 (a cover page and 24 pages of the original).
- **Summary:** **The only document in the set that covers the 3.5-inch drive.** It gives:
  - the IIGS IWM soft switches (CA0, CA1, CA2, LSTRB, ENABLE, SELECT, Q6, Q7) and DISKREG $C031 (bit 7 = SEL, bit 6 = 3.5-inch enable);
  - the full IWM mode, status and handshake register layouts, with the recommended mode value $0F for 3.5-inch drives;
  - the full 3.5-inch drive **status** table (16 entries) and **control** table (7 entries), selected by CA2, CA1, CA0 and SEL and strobed with LSTRB.

  It also has a pseudo-code I/O sequence, the DUMP3.5 track-dump program as assembly source and hex, and a bibliography. The author says the information comes from ROM disassembly and experiment, and is not authoritative. Some entries are marked "*" as undocumented.

### 1.10 Apple 2 IWM Spec 1982.pdf
- **Title as printed:** "Integrated Woz Machine (IWM) Device Specification [Revision 19]". Author "Apple Computer Inc.", "Document dates of record 1978 - February 1982". Owner: DigiBarn Computer Museum (curator Bruce Damer). Creative Commons BY-NC-ND 2.5.
- **Pages:** 18. Pages 1 to 6 were added by DigiBarn: the cover, an introduction, references with a 1985 BYTE interview with Wozniak, a property statement and the licence. Pages 7 to 18 are the original 12 pages, footed "IWM device specification page N September 24, 1982 rev# 19".
- **Summary:** The clean copy of IWM spec rev 19. It is stamped "CONFIDENTIAL" with a handwritten "SERVICE Do Not Copy", but the text is fully legible. It covers features, pinout, technical description (write and read state machines), the read bit-cell window table, signal descriptions, the state, mode, status and handshake registers, maximum ratings, DC and AC characteristics, and the revision history. The DigiBarn introduction (p. 2) says the IWM drove "Apple 3.5" floppy drives (capacity 800 KB)" on the IIc, IIgs and Macintosh, but this page is not part of the original document. Use this copy as the main reference for IWM numbers.

### 1.11 APPLE 2 IWM SPEC 19820924.pdf
- **Title as printed:** "IWM Floppy Disk I/O Controller Info. Integrated Woz Machine (IWM) Device Specification. Revision 19 -- Apple Computer, Inc. -- September 24, 1982. POOR COPY, SEVERAL AREAS BLACKED OUT". Brutal Deluxe scan, 31 December 2008.
- **Pages:** 13 (a cover page and 12 pages of the specification).
- **Summary:** The same Revision 19 text as section 1.10, from a different and poorer photocopy. Several sentences are blacked out: the GCR description on spec page 2, the synchronous-mode Q3 sentence, the asynchronous buffer paragraph, and the port-operation paragraph on spec page 4. All the numbers that survive agree with section 1.10. The copy carries a handwritten note in the left margin of spec page 2 that reads like "8mHz" [unsure], and a handwritten mark over the "4" in "The duration of the write window is 4" that may read "4½" [unsure], with an arrow in the margin.

### 1.12 apple2_IWM_PATENT1988.pdf
- **Title as printed:** US Patent 4,742,448, "Integrated Floppy Disk Drive Controller". Inventors Wendell B. Sander (Los Gatos) and Robert Bailey (San Jose). Assignee Apple Computer, Inc. Filed Dec. 18, 1986, as a continuation of Ser. No. 573,067 of Jan. 24, 1984. Granted May 3, 1988.
- **Pages:** 12. These are the front page (with a handwritten "IWM chip (Integrated Woz Machine)" note), 4 drawing sheets (Figures 1 to 4), and the specification and claims in columns 1 to 14.
- **Summary:** The patent version of the IWM. It covers the block diagrams of the controller, read control (Figure 3) and write control (Figure 4), and the state latch address table (Table 1). It gives the status register bit assignment, the read-window arithmetic in CLKs, and the shift and toggle spacing in CLKs. It describes the OBT timer, including a **one-half millisecond** timeout when LATCH is set. Table 2 is the asynchronous write sequence and Table 3 the synchronous one. Figure 1 shows the drive interface: P0 to P3 go to the track select amplifiers, WRREQ, WRDATA and RDDATA go to the read/write amplifiers, SENSE comes from a write-protect switch, and ENABL1 and ENABL2 go to the drive motor amplifier.

### 1.13 iwm_19820924.pdf (added later)
- **Title as printed:** "Integrated WOZ Machine (IWM) Device Specification", footer "IWM device specification page N September 24, 1982 rev# 19". Apple Computer, Inc. No author is named. This is a Xerox scan (PDF producer "Xerox WCP C3545") with no added cover page and no text layer.
- **Pages:** 19.
- **Summary:** The same Revision 19 specification, **from the same marked-up photocopy as "APPLE 2 IWM SPEC 19820924.pdf"** (section 1.11). It has the same blacked-out sentences, the same "8mHz" [unsure] margin note and "4½" [unsure] mark on spec page 2, and the same margin scribbles beside state bits 4 and 5 on spec page 7. All the numbers are identical to sections 1.10 and 1.11. Differences from section 1.11:
  - There is no Brutal Deluxe cover page and no "Page 000N of 0013" footer frame. The pages are bare scans.
  - **Spec pages 6 to 12 appear twice.** PDF pages 1 to 12 are spec pages 1 to 12 in order. PDF pages 13 to 19 repeat spec pages 6, 7, 8, 9, 10, 11 and 12. The repeated pages show the same marks and blackouts, so they look like a second scan of the same sheets rather than a different version.
  - The image quality is about the same as section 1.11, with slightly more speckle at the page edges. No passage that is blacked out in section 1.11 can be read here.

  Page mapping for citations: PDF p. N = spec page N for N = 1 to 12.

### 1.14 iwm_19840426.pdf (added later)
- **Title as printed:** "SOFTWARE CONTROL OF THE DISK II OR IWM CONTROLLER", "PREPARED BY NORMAN LEUNG APRIL 26, 1984", "REVISION 1, MAY 10, 1984". A Xerox scan with no text layer.
- **Pages:** 8.
- **Summary:** **The same document and revision as "APPLE 2 IWM INFO 19840510.pdf"** (section 1.6). The file name uses the original preparation date, but the title page says Revision 1, May 10, 1984. The text, tables, code listings, revision bars and Figure 2 seek waveforms match page for page. PDF p. N here equals p. N+1 in section 1.6. Differences:
  - There is no Brutal Deluxe cover page.
  - The handwritten page numbers "1/8" to "8/8" at the bottom of each page in section 1.6 are absent from this scan.

  It also contains these 5.25-inch Disk II figures, which section 2 already covers:
  - "The spindle motor should be on for 150 ms before starting the seek operation" (p. 7).
  - The last phase must be on for 36.6 msec "because this timing includes the head settling time requirement (25 ms) of the drive" (p. 7).

### 1.15 iwm_discussion_19820531.pdf (added later)
- **Title as printed:** No overall title. It is a bundle of memos, a Xerox scan with no text layer. The file name uses the date of the first memo.
- **Pages:** 7.
- **Summary:** A compilation of material that is mostly also present in other files, in these parts:
  - **p. 1:** Peter Baum's memo of May 31, 1982 to Norman Leung and Conrad Chen. It is the same text as section 1.2, including the ProDOS sync-gap figures 40, 36, 32, 32, 36 and 32.
  - **pp. 2 and 3:** Peter Baum, "Differences Between the IWM and the Apple // Disk Controller", May 24, 1984. It is the same text as section 1.7.
  - **p. 4:** A printout headed "File: iwm.infolist / Report: iwm.infolist". It is a short mailing list of outside developers' names and addresses, crossed out by hand, with "SEE BACK" and "IWM" written on it. The personal details are not transcribed here. Handwriting from the back of the sheet shows through, mirror-imaged. It appears to repeat the 40 / 36 / 32 / 32 / 36 cycle figures and "(D5)", "(AA)" from the p. 1 memo [unsure].
  - **p. 5:** "Addendum to Software Control of the IWM or Disk ][ Controller", the PREFACE. It is the same text as section 1.1.
  - **pp. 6 and 7:** The IWM-detection 6502 listing ("SOURCE FILE #01 =>IWM", 51 lines, "ASSEMBLER CREATED ON 15-JAN-84 21:28"). It is the same as section 1.4.

  The only content not found elsewhere in the set is the p. 4 mailing list, which has no technical value. Nothing in it concerns 3.5-inch drives, UniDisk, Sony or SmartPort.

---

## 2. Facts relevant to a 3.5" mechanism driver

### 2.1 3.5-inch drive register and command map (CA0, CA1, CA2, SEL, LSTRB, SENSE)

Only **Parker 1994** has this map. None of the Apple documents in this set mention the 3.5-inch drive protocol.

**Line names and IIGS addresses** (Parker p. 4, original page 3):

```
CA0     EQU $C0E0 ;stepper phase 0 / control line 0
CA1     EQU $C0E2 ;stepper phase 1 / control line 1
CA2     EQU $C0E4 ;stepper phase 2 / control line 2
LSTRB   EQU $C0E6 ;stepper phase 3 / control strobe
ENABLE  EQU $C0E8 ;disk drive off/on
SELECT  EQU $C0EA ;select drive 1/2
Q6      EQU $C0EC
Q7      EQU $C0EE
```

Further quotes from the same page:

- "Each of these I/O locations represents a two-way switch; accessing location X turns off the switch; accessing location X+1 turns it on."
- "For a 3.5-inch drive, these switches have become general-purpose control lines."
- ENABLE "turns on the red "in use" light, holds the disk in the drive, and prepares the drive to receive further commands. Unlike the 5.25-inch drive, it does not start the spindle motor spinning."
- SELECT: "if it is off, drive 1 will be accessed; turning it on selects drive 2."

**SEL line** (Parker p. 5 and p. 6, original pages 4 and 5). DISKREG is at $C031. Its bit layout is `S E R R R R R R`:

- "S General purpose control line used in conjunction with CA0...LSTRB switches"
- "E Enables 3.5-inch drive support: 0 = 5.25-inch drive and smartport devices available, 1 = 3.5-inch drive available"
- Note: "The Hardware Reference and the Firmware both incorrectly state that bit 7 selects between the upper and lower heads."

**Selection mechanism** (Parker p. 12, original page 11): "CA0...CA2 and SEL form a 16-way switch which selects the desired control or status function, and the LSTRB switch signals the drive to perform a control function."

The selection routine SEL35 takes the function number in A. Its encoding is given on Parker p. 22 (original page 21) as "Bit 0=CA2 status / Bit 1=SEL status / Bit 2=CA0 status / Bit 3=CA1 status". Before setting the new state, SEL35 sets the known state CA0 off, CA1 on, LSTRB off, CA2 off. It turns CA1 off only if bit 3 is 0.

**Reading status** (Parker p. 12 and p. 13): "To read a status bit, turn Q6 off, Q7 on, and ENABLE on, configure CA0...CA2 and SEL for the desired function, and read the status bit from bit 7 of the IWM status register."

```
STAT35  JSR SEL35   ;select desired status bit
        BIT Q6+1
        BIT Q7      ;test status register
        RTS         ;(returns result in processor N-flag)
```

Note the conflict: the prose says "Q6 off, Q7 on", but the code does BIT Q6+1 (Q6 on), then BIT Q7 (Q7 off). That is the IWM "read status register" state (Q6 on, Q7 off), according to Parker's own register table on p. 7. [unsure: the prose on p. 12 appears to have Q6 and Q7 swapped.]

**Status table** (Parker p. 13 and p. 14, original pages 12 and 13). The column order is as printed: CA2 CA1 CA0 SEL. The parameter is the SEL35 function number.

| CA2 | CA1 | CA0 | SEL | Param | Function (as printed) |
|---|---|---|---|---|---|
| off | off | off | off | $00 | Step direction. 0 = head set to step inward (toward higher-numbered tracks); 1 = head set to step outward (toward lower-numbered tracks) |
| off | off | off | on | $02 | Disk in place. 0 = disk in drive; 1 = drive is empty. |
| off | off | on | off | $04 | Disk is stepping. 0 = head is stepping between tracks; 1 = head is not stepping. |
| off | off | on | on | $06 | Disk locked. 0 = disk is write protected; 1 = disk is write-enabled. |
| off | on | off | off | $08 | Motor on. 0 = spindle motor is spinning; 1 = motor is off |
| off | on | off | on | $0A | Track 0. 0 = head is at track 0; 1 = head is at some other track. "This bit becomes valid beginning 12 msec after the step that places the head at track 0." |
| off | on | on | off | $0C | *Disk switched? 0 = user ejected disk by pressing the eject button; 1 = disk not ejected. |
| off | on | on | on | $0E | Tachometer. 60 pulses per disk revolution |
| on | off | off | off | $01 | Instantaneous data from lower head. Reading this bit configures the drive to do I/O with the lower head. |
| on | off | off | on | $03 | Instantaneous data from upper head. Reading this bit configures the drive to do I/O with the upper head. |
| on | on | off | off | $09 | Number of sides. 0 = single-sided drive; 1 = double-sided drive |
| on | on | off | on | $0B | *Disk ready for reading? 0 = ready; 1 = not ready. "I am not too sure about this bit. The firmware waits for this bit to go low before trying to read a sector address field." |
| on | on | on | on | $0F | Drive installed. 0 = drive is connected; 1 = no drive is connected |

The combinations CA2 on, CA1 off, CA0 on (parameters $05 and $07) and CA2 on, CA1 on, CA0 on, SEL off (parameter $0D) are not listed in the status table.

Parker's notes on the status table (p. 14):

- "Functions marked with an asterisk, i.e. "*", are used by the IIGS ROM but not documented in any publication available to me. I am fairly certain of the function of status bit $0C (used by the firmware to test for disk-switched errors), but I am unsure about status bit $0B (if my programs neglect to test for it, the drive displays an annoying tendency to start reading while the head is still stepping)."
- "The settings of most of these bits are "backwards": 0 means "yes" and 1 means "no"."

**Performing a control function** (Parker p. 14): "To perform a control function, turn off LSTRB, configure CA0, CA1, and SEL for the desired function, set CA2 to the desired value (all control functions can be turned on or off), and then turn LSTRB on and back off."

```
CONT35  JSR SEL35     ;select desired function
        BIT LSTRB+1   ;strobe on
        BIT LSTRB     ;strobe off
        RTS
```

**Control table** (Parker p. 15, original page 14). The column order is as printed: CA1 CA0 SEL CA2.

| CA1 | CA0 | SEL | CA2 | Param | Function (as printed) |
|---|---|---|---|---|---|
| off | off | off | off | $00 | Set step direction inward (toward higher-numbered tracks.) |
| off | off | off | on | $01 | Set step direction outward (toward lower-numbered tracks. |
| off | off | on | on | $03 | *Reset disk-switched flag? (The firmware uses this to clear disk-switched errors.) |
| off | on | off | off | $04 | Step one track in current direction (takes about 12 msec). |
| on | off | off | off | $08 | Turn spindle motor on. |
| on | off | off | on | $09 | Turn spindle motor off. |
| on | on | off | on | $0D | Eject the disk. This takes about 1/2 sec to complete. The drive may not recognize further control commands until this operation is complete. |

Parker: "* Again, the asterisk marks a function used by the ROM but not documented in any publication available to me."

**Recommended sequence** (Parker pp. 15 to 17, original pages 14 to 16):

1. Set the IWM mode register to $0F.
2. Select drive 1 or 2.
3. Turn the drive on (ENABLE+1).
4. Turn the spindle motor on (LDA #$08; JSR CONT35).
5. If the current track is unknown, set direction out ($01). Then loop: step ($04), wait while $04 shows "still stepping", until $0A (track 0) reads 0.
6. Seek by setting the direction ($00 inward or $01 outward) and stepping ($04). After each step, wait on status $04.
7. "Select desired side (LDA #$01 or LDA #$03; JSR STAT35)".
8. "WHILE not ready to read (LDA #$0B; JSR STAT35; BMI ...)".
9. Read or write.
10. Clean up: spindle off ($09), drive off (ENABLE), CA0 to LSTRB off, mode register to $00, deselect the 3.5-inch drive.

DUMP3.5 (Parker pp. 20 and 21) follows the same order. It checks for a disk with status $02 before turning the motor on.

### 2.2 Bit-cell timing, FAST and SLOW, 7 and 8 MHz FCLK, mode register, read windows

**Bit-cell time and data rate:**

- Spec rev 19 (Spec 1982 p. 8, spec page 2; also Spec 19820924 p. 3 and Info 19840228 p. 5, sheet 4): "The bit cell time defaults to 4 uS (set to 2 uS in fast mode). Therefore the data rate is one nibble every 32 uS (16 uS in fast mode). When writing data out, a one is written as a transition on the WRDATA output at a bit cell boundry time and a zero is written as no transition."
- Features list (Spec 1982 p. 7): "Use of 7M (or 8 mHz) to minimize sampling error rate" and "Fast mode using 2 uS bit cells".
- Asynchronous bit cell in FCLK periods (Spec 1982 p. 8): "In fast mode the CLK clock is equivalent to the clock input on FCLK. In slow mode CLK is equivalent to the clock input on FCLK divided by two. Therefore, in 7M and slow mode the bit cell time will be 28 FCLK clock input periods in duration, in 8M and slow mode the cell time will be 32 periods, and in 8M and fast mode the cell time will be 16 periods. In asynchronous mode the write shift register is loaded every 8 bit cell times starting seven CLK periods after the write state begins."
- Synchronous write timing (Spec 1982 p. 8): "In the synchronous mode, the time of that transition and every 8 Q3 periods (4 uS) thereafter, until L7 is cleared, marks the beginning of a write window. The duration of the write window is 4 periods of the Q3 input signal (2 uS). ... If the next write access has not occured 32 uS (64 Q3 periods) after a load, the write will be extended in multiples of 4 uS (8 Q3 periods) until another write access, and zeros will be shifted out." Also: "Q3 clock input is used internally to generate the 32 and 40 uS timings, which would then be 64 and 80 of the Q3 clock input periods in duration, respectively, and the bit cell timings, 8 Q3 periods per bit cell time in slow mode."
- Q3 (Spec 1982 p. 11): "2.0 Mhz clock input used to qualify the timing of the serial data being written out in the synchronous mode." FCLK: "Clock input for the serial data logic; either 7 or 8 MHz."
- Bailey 1983 (p. 4): synchronous writes "are done using Q3. Thus to have 2us bit cells instead of 4us requires asynchronous mode."
- Patent col. 3 (p. 7): "In a preferred embodiment, Q3 is a 2 MHz clock and FCLK is a 7 MHz clock. Additionally, Q3 may be left at "0" (if only asynchronous mode is used) and/or FCLK may be 8 MHz."
- Patent col. 6 (p. 8): "When FAST is cleared, internal timing, i.e. CLK period, is equal to twice the period of FCLK. 8/7 also relates to timing the FCLK. When an 8 MHz clock is in use, 8/7 is set. If FCLK is running at 7 MHz, 8/7 is cleared. ... if FCLK is 8 MHz, one microsecond will be eight clock periods; if FCLK is 7 MHz, one microsecond will be seven clock periods. This allows computers with 7 MHz clocks and computers with 8 MHz clocks to read and write equivalently".
- Parker p. 5: "The 3.5-inch drive does its I/O twice as fast as the 5.25-inch drive ... when the system speed is slow, each data byte from the disk drive must be dealt with in 16 cycles or less".

**Mode register** (write-only). Spec 1982 p. 14 (spec page 8); identical in Spec 19820924 p. 9 and Info 19840228 p. 11. "All eight mode bits are reset to 0 by /RESET low."

| bit | function (as printed) |
|---|---|
| LSB 0 | 1 = latch mode (should be set in asynchronous mode) |
| 1 | 0 = synchronous handshake protocol; 1 = asynchronous |
| 2 | 0 = 1-second on board timer enable; 1 = timer disable |
| 3 | 0 = slow mode; 1 = fast mode ( 2 uS bit cell timing ) |
| 4 | 0 = 7MHz; 1 = 8MHz (7 or 8 mHz clock descriptor) |
| 5 | 1 = test mode; 0 = normal operation |
| 6 | 1 = MZ-reset |
| MSB 7 | reserved for future expansion |

More text from the same page:

- "In latch mode the msb of the read data is latched internally during /DEV low (this internally latched msb is then used for the determination of a valid data read)."
- "Fast mode selects a bit cell time of 2 uS instead of 4 uS. The 7/8 mHz descriptor indicates whether the input clock (FCLK) is to be divided by 7 or 8 to provide 1 uS internal timings."
- "When the test mode bit is a 1, device operation is unspecified, except that status register bit 5 can always be read and that the mode register can always be set."

Parker (p. 8) gives the same register as `R R R S C M H L`. S = clock speed (0 = 7 MHz, 1 = 8 MHz), "Should always be 0". C = bit cell time (0 = 4 usec/bit for 5.25, 1 = 2 usec/bit for 3.5). M = motor-off timer (0 = 1 sec, 1 = no delay), "Should be 0 for 5.25 and 1 for 3.5". H = handshake (0 = synchronous, 1 = asynchronous), "Should be 0 for 5.25 and 1 for 3.5". L = latch (0 = "read-data stays valid for about 7 usec", 1 = "read-data stays valid for full byte time"), "Should be 0 for 5.25 and 1 for 3.5". Then: "Before doing I/O to the 3.5-inch drive, the mode register should be set to $0F. When your routine is done, it should be sure to set the mode register back to $00."

The patent (col. 6, p. 8) names mode bits D0 to D4 "LATCH, SYNCH, OBT, FAST and 8/7".

**Read data bit-cell windows.** Spec 1982 p. 10 (spec page 4); identical in Spec 19820924 p. 5 and Info 19840228 p. 7.

| mode | | Nclks | period | data | notes |
|---|---|---|---|---|---|
| slow | 7M | 7-20 | FCLK/2 | 1 | 2.0-5.71+ uS |
| | | 21-34 | | 01 | 6.0-9.71+ uS |
| | | 35-48 | | 001 | 10.0-13.71+ uS; window is 28 clks |
| slow | 8M | 8-23 | FCLK/2 | 1 | |
| | | 24-39 | | 01 | |
| | | 40-55 | | 001 | |
| fast | 7M | 7-20 | FCLK | 1 | window is 14 clks |
| | | 21-34 | | 01 | |
| | | 35-48 | | 001 | |
| fast | 8M | 8-23 | FCLK | 1 | 1.0-2.875+ uS |
| | | 24-39 | | 01 | 3.0-4.75+ uS |
| | | 40-55 | | 001 | 5.0-6.875+ uS; window is 16 clks |

The text under the table reads: "Nclks is the number of clock periods between falling transitions of the internally synchronous version of RDDATA. The clock period is either that of the FCLK input or that of the FCLK input divided by two in slow mode. Each falling transition resets the read data windows for subsequent data to be relative to that transition."

Spec 1982 p. 9: "When reading serial data, a falling transition within a bit cell window is considered to be a one, and no falling transition within a bit cell window is considered to be a zero. The receive data input on RDDATA is synchronized internally with the CLK clock. The synchronized falling transition is then discriminated to the nearest bit cell window using the 7/8 mHz FCLK clock signal in fast mode and the FCLK signal divided by two in slow mode. A digital one-shot data recovery scheme is used."

**Patent read-window arithmetic** (col. 7 and col. 8, p. 9):

- "Each time a negative transition of RDDATA occurs, it resets an interval counter. When 8/7 is set, the interval is 16 CLKs. When 8/7 is reset, the interval is 14 CLKs."
- "The expected time is widened by approximately one-half an interval before and after the expected time since the data is not precisely spaced when read due to variations in drive speed and other external factors."
- "The next expected data is nominally at 16 CLKs when 8/7 is set. This may range between 16-8=8 CLKs and 16+7=23 CLKS. ... next expected data is nominally at 32 CLKs. This may range between 32-8=24 CLKS and 32+7=39 CLKs."
- "When 8/7 is reset, LFT0 and LFT1 are pulsed as described above, except intervals are nominally 14 CLKs and are widened minus 7 CLKs and plus 6 CLKs."
- A detected "1" makes the read data extractor pulse LFT1 "for one CLK cycle". A "0" pulses LFT0.

**Bailey 1983 read spacing** (p. 3, page 2/3; FCLOCK counts, written for slow 8 MHz [unsure: mode not stated]):

- "Negative transitions are detected and generate a pulse of 1 FCLOCK period."
- "After a negative transition a 0 is inserted [handwritten: into the shift register] if the next negative transition is 24 FCLOCKS or more later. If it is less than 24 FCLOCKS another 1 is ~~shifted~~ [handwritten: inserted] and the timer reset. ... another 0 will be inserted if the next negative transition does not occur for 16 more FCLOCKS."
- Table as printed: "0 - 23 FCLOCKS DATA = 11 / 24 - 39 FCLOCKS 01 / 40 - 55 FCLOCKS 001". The first entry is printed "11". This was checked at higher magnification.
- "Group code has 2 important rules: 1) the leading bit (MSB) is 1 and, 2) there is a maximum of 2 consecutive zeroes."

**Read register hold and stall:**

- Spec 1982 p. 9: "In the synchronous mode ... when a one is shifted into the MSB, the shift register will appear, to the data bus, to be stalled for a period of two bit times plus four CLK periods. ... In asynchronous mode the data register will latch the shift register when a one is shifted into the MSB and will be cleared 14 FCLK periods (about 2 uS) after a valid data read takes place (a valid data read being defined as both /DEV being low and D7 (the msb) outputting a one from the data register for at least one FCLK period)."
- Bailey 1983 p. 3: "Once D7 is read as 1 by the processor the MSB of the read data register will reset 14 FCLOCKS (1.75 usec) later. This time is not affected by fast/slow mode." Also: "in synchronous mode ... the read data register will not re-load for 2 data shifts + 4 FCLOCKS (8 FCLOCKS in slow mode)."
- Spec 1982 p. 10: "In port operation, which is asynchronous mode true and latch mode false with /DEV held low indefinitely, read data will appear and change as if the IWM were being continually read. In port operation the MSB can be used to continously clock data into external registers. The MSB will be cleared at least six FCLK periods before being set. Except in port operation, in asynchronous mode the latch mode bit should be set (for reliability in clearing the data register after a read)."
- Patent col. 8: the RR7 output "is not set until 1 CLK period, when FAST is "1" (fast mode), and a ½ CLK period when FAST is "0" (slow mode) after X7 is set". X7 is reset "fourteen FCLK's after READ DATA REGISTER is set and D7 is "1"".

**Write timing in clocks:**

- Patent col. 9 and col. 10 (p. 10): "In asynchronous mode (SYNCH is set), the load will be completed approximately eight CLK's after WRITE DATA REGISTER has been set. In synchronous mode, the load will be completed between four and five Q3 periods after WRITE DATA REGISTER has been set."
- Patent col. 10: "When 8/7 is set, shifts and toggles are separated by 8 CLKs. When 8/7 is reset, toggles occur 6 CLKs after shifts, and shifts occur 8 CLKs after toggles." This was checked at higher magnification.
- Patent col. 9: "the first bit of a byte must be a "1"". In synchronous mode, "a "1" must be placed on WRITE DATA REGISTER every 64 Q3 periods, otherwise 0's will be shifted out".
- Bailey 1983 p. 4: asynchronous writes go through "set L6, set L7. This causes the pre-write pre-set pulse followed by the release of all write logic." Handwritten: "Eight +/- 1/2 FCLOCKS after set L7 the parallel load ends. After 8 more FCLOCKS the MSB of the byte to be written (which is 1) will cause the WRDATA output to toggle from 1 to 0. ... The shift and toggle are separated by 8 FCLOCKS. After all 8 bits are shifted the write data shift register is loaded. This parallel load ends 8 x 16 FCLOCKS after the last parallel load ended." Synchronous mode: "This will end 4-5 Q3 periods after the first time L7 is set. Two Q3 periods later the MSB ... will cause the WRDATA output to toggle from 1 to 0. The shifting and toggling are each updated every 8 Q3 periods."
- Data sampling (Spec 1982 p. 10): "Data written to the IWM is sampled by the the zero to one transition of the logical OR of Q3 and /DEV. In asynchronous mode the Q3 input may be tied low."

**Write precompensation:** not mentioned in any document in this set. See section 3.

**Handshake register** (read-only; Spec 1982 p. 15, spec page 9):

- bits 0 to 5: "reserved for future use (currently read as ones)"
- bit 6: "1 = write state ( cleared to 0 if a write underrun occurs )"
- bit 7: "1 = write data buffer register ready for data"

**Underrun** (Spec 1982 p. 9): "If an underrun occurs in asynchronous mode /WRREQ will be disabled (set to a TTL high state) and the /underrun flag will be set to zero. ... Clearing state bit L7 will reset the /underrun flag."

Parker p. 11 gives the end-of-write idiom: "WLAST BIT Q6 ;wait until last data underruns / BVS WLAST / BIT Q7 ;be VERY SURE to turn off write mode!"

**Self-sync (5.25-inch context)** (Info 19840510 p. 7): "A self sync byte consists of eight bits of "1" and two bits of "0."" ... "40 us between two consecutive parallel load instructions and a 4 us bit time". "It is necessary to write at least five self sync bytes at the beginning of both the address and data field."

### 2.3 /ENBL, motor-off timer, and Q6/Q7 state behaviour

**State register** (write-only pseudo-register; Spec 1982 p. 13, spec page 7): "The bits in this register are individually addressed by A3,A2,A1. The data on A0 is latched into the addressed state bit by /DEV low. All eight state bits are reset to 0 by /RESET low."

| Address | Name | Function |
|---|---|---|
| 0 | | A 1 in this bit will drive PHASE0 to a high state. |
| 1 | | PHASE1 |
| 2 | | PHASE2 |
| 3 | | PHASE3 |
| 4 | LMotor-On | A 1 on LMotor-On sets the enable selected below low |
| 5 | Drive-Sel | A 1 on this bit selects /ENBL2; a 0 selects /ENBL1 |
| 6 | L6 | (see description below) |
| 7 | L7 | (see description below) |

The patent's Table 1 (col. 4, p. 7) gives the same mapping: A3 A2 A1 = 000 to 111 select P0, P1, P2, P3, L4, L5, L6, L7.

**Register select** (Spec 1982 p. 13): "Other registers are read during any operation in which A0 is a zero. A register is written when both L6 and L7 are set or are being set to 1 and A0 is a one. The combination of L7 and Motor-On and /underrun enables /WRREQ low."

| L7 | L6 | Motor-On | register operation selected | State Name |
|---|---|---|---|---|
| 0 | 0 | 0 | read all ones | |
| 0 | 0 | 1 | read data register | Read |
| 0 | 1 | x | read status register | Write-Protect Sense |
| 1 | 0 | x | read write-handshake register | Write |
| 1 | 1 | 0 | write mode register | Mode Set |
| 1 | 1 | 1 | write data register | Write Load |

**Motor-off timer and /ENBL** (Spec 1982 p. 14): "If the 1-second timer bit is a zero then the enable (/ENBL1 or /ENBL2) selected by Drive-Sel will be held low for 2^23 +-100 FCLK periods (about 1 second) after the LMotor-On state bit is reset to zero. If the latch mode bit is set the timer is not guarantied to count up to 2^23. Motor-On is synonomous with either /ENBL1 or /ENBL2 being low."

**/ENBL1 and /ENBL2** (Spec 1982 p. 12, spec page 6): "Programmable buffered output lines. No more than one enable may be low at any time. If an enable is low than Motor-On is true. If the 1-second on board timer is enabled then the selected one will stay low for about 1 second after it is programmed high."

**Patent OBT timer** (col. 5 and col. 6, p. 8):

- "When latch L4 is set, if latch L5 is "0", drive 1 is enabled by ENABL1; if L5 is "1", drive 2 is enabled by ENABL2."
- "If OBT is set, if L4 is cleared, ENABL1 or ENABL2 is disabled by logic circuit 42, which includes the onboard timer, depending upon the setting of L5, thereby shutting down drive motor 29. However, if OBT is cleared, then the clearing of L4 will not cause logic circuit 42 to disable ENABL1 or ENABL2 until a one second timer has elapsed (if LATCH is reset or until a one-half millisecond timer has elapsed if LATCH is set)." This was checked at higher magnification.
- The spec says only that with latch mode set the timer "is not guarantied to count up to 2^23". The patent says one-half millisecond.

**DMOTOR-ON and command decode** (Bailey 1983 p. 2):

- "Commands generate control line pulses which are only as wide as DEV. ... They are decoded by L6, L7, and DMOTOR-ON. DMOTOR-ON includes the 1 second timer when enabled. When L6 or L7 are changed the decoder uses the new state. ... A0 must be 0 to enable a read command. This corresponds to clearing a state. A0 must be 1 to enable a write command."
- "The DMOTOR-ON input to the decoder comes from the same line that enables the selected drive motor [handwritten: (ENABLE 1, ENABLE 2)] but it has some added delay so it changes when the device is de-selected. ... if L4 is set MOTOR-ON becomes 1, but DMOTOR-ON will remain 0 during that DEV and be 1 throughout the next DEV."

**Status register** (read-only; Spec 1982 p. 15, spec page 9):

- bits 0 to 4: "same as mode register"
- bit 5: "1 = either /ENBL1 or /ENBL2 is currently active (low)"
- bit 6: "1 = MZ ( reset to 0 by /RESET and MZ-reset )"
- bit 7: "1 = SENSE input high; 0 = SENSE input low"
- "The MZ bit is reserved for compatibility with future products and should always be read as a zero."

The patent (col. 6) gives: "LATCH is placed on D0, SYNCH is placed on D1, OBT is placed on D2, FAST is placed on D3, 8/7 is placed on D4, MOTOR-ON is placed on D5, a 0 is placed on D6 and SENSE, from the disk drive, is placed on D7." Parker (p. 9) uses the layout `I R E S C M H L`, with "I Sense input. write-protect indicator (5.25-inch drive) general status line (3.5-inch drive)".

**Q6/Q7 soft-switch view** (Info 19840510 p. 3 and p. 4). Even addresses turn a switch off and odd addresses turn it on: $C088 motor off, $C089 motor on, $C08A drive 1, $C08B drive 2, $C08C Q6L, $C08D Q6H, $C08E Q7L, $C08F Q7H.

| Q6 | Q7 | FUNCTION |
|---|---|---|
| L | L | READ |
| H | L | SENSE WRITE PROTECT OR PREWRITE STATE |
| L | H | WRITE |
| H | H | WRITE LOAD |

Parker's version (p. 7): Q6 off, Q7 off = read data register; Q6 off, Q7 on = read handshake register; Q6 on, Q7 off = read status register; Q6 on, Q7 on = "Write mode register (if drive is off) / data register (if drive is on)".

Parker p. 7 adds: "the drive may remain active for a second or two after the ENABLE access, and that the write to the mode register will fail unless the drive is fully deactivated. This means that the mode register must be repeatedly written until the status register ... indicates that the desired changes have taken effect." The SELIWM loop compares `status AND $1F`.

**Restrictions:**

- Baum 1984 p. 2: "The IWM will not work properly in an Apple // if it is placed into a mode where both Q6H and Q7H are set and the motor is off (Q4L). If the chip is placed in this mode at any time then all subsequent operations of the IWM may not work properly."
- Baum 1984 p. 2: "When reading the write protect switch only the most significant bit (MSB) should be tested for the status."
- Leung 1984 p. 4 (Disk II): "The disk II controller hardware will keep the ENABLE/ signal to its active low state for approximately one second after the execution of the motor off instruction". "After the completion of the motor on instruction, the program should wait at least 1 second for the motor to come up to speed".

**Patent write sequence, asynchronous** (Table 2, col. 10 and col. 11). Columns are L4 L5 L6 L7 MOTOR-ON, then Action:

- 0000 0: initial state
- 0010 0: set L6
- 0011 0: set L7; write data on bus into the mode register
- 0010 0: clear L7
- 0000 0: clear L6
- 1000 1: set L4; select drive 1, set MOTOR-ON
- 1010 1: set L6; pre-write state; initialize write shift clock; initialize load/shift control; set WRDATA; set WRREQ; reset URF
- 1011 1: set L7; enable WRITE DATA REGISTER
- 1001 1: clear L6; read HS and URF flags
- 1001 1: continue polling HS flag until it has been set
- 1011 1: set L6; enable WRITE DATA REGISTER
- (the poll and write steps repeat)
- 1010 1: clear L7; exit write mode
- 1000 1: clear L6
- 0000 1: clear L4
- 0000 0: MOTOR-ON clears after timer counts down

### 2.4 Drive interface pins and electrical notes

**IWM pinout** (28-pin, 600 mil plastic DIP; Spec 1982 p. 7; same in Info 19840228 p. 4):

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | PHASE0 | 28 | PHASE1 |
| 2 | PHASE2 | 27 | PHASE3 |
| 3 | A0 | 26 | Vcc |
| 4 | A1 | 25 | Q3 |
| 5 | A2 | 24 | FCLK |
| 6 | A3 | 23 | /RESET |
| 7 | /DEV | 22 | RDDATA |
| 8 | WRDATA | 21 | SENSE |
| 9 | /WRREQ | 20 | /ENBL1 |
| 10 | D0 | 19 | /ENBL2 |
| 11 | D1 | 18 | D7 |
| 12 | D2 | 17 | D6 |
| 13 | D3 | 16 | D5 |
| 14 | GND | 15 | D4 |

**Signal descriptions** (Spec 1982 pp. 11 and 12):

- RDDATA: "The serial data input. The falling transition of each pulse is synchronized by the IWM."
- SENSE: "An input to the IWM that can be polled via the status register."
- WRDATA: "The serial data output. A transition occurs on this output for each one bit."
- /WRREQ: "This signal is a programmable buffered output line."
- PHASE0-3: "These are programmable output lines. A true TTL logic "1" (2.4 volts) can be maintained even while driving two darlington inputs in parallel."

**DC characteristics** (Spec 1982 p. 16, spec page 10, rev 19):

- Vcc 4.75 to 5.25 V.
- Icc max 200 mA (note 5).
- Vil max 0.8 V. Vih min 2.0 V.
- Il max 100 uA.
- Vol max .4 V. Voh min 2.4 V.
- Ioh min .32 mA. Iol min 3.2 mA.
- Maximum ratings: supply and input voltage -0.3 to +7.0 V. Operating temperature 0 to +70 degrees C (ambient).
- Note 1: "The inputs have static protection. All IWM inputs and bidirectional lines in the input mode are high impedance except as noted below: WPROT and /RDDATA : pullup to VCC of 10K ohms nominal (source current of 80 to 600 uA at 0.4 to 2.8 Volts)".
- Note 2: "Outputs Ioh and Iol apply to D0 thru D7 and WRDATA. The following output lines have special drive capabilities":
  - /ENBL1, /ENBL2: "Sink current of at least 5.0 mA at Vol. Source current of at least 40 uA at Voh"
  - /WRREQ: "Sink current of at least 10.0 mA at Vol. Source current of at least 40 uA at Voh"
  - PHASE0-3: "Source current of at least 1.0 mA at Voh. Source current of 0.5 mA when pulled down to 3.0 V. Sink current of at least 2.4 mA at Vol"
- These figures (40 uA source on /ENBL and /WRREQ against a 5 to 10 mA sink) suggest outputs that are strong when low and weak when high, close to open-drain behaviour. The documents never use the words "open collector" or "open drain". This is an inference, not a quote.

**Changes in the 1984 drawing 343-0041-A** (Info 19840228 p. 13, sheet 12):

- Icc max reads "60" mA.
- RDDATA pull-up note reads "source current of 80 to 600 uA at 0 Volts with VCC = 5.25V".
- PHASE 0, PHASE 2 and PHASE 3: "Source current of at least 1.0 mA at Voh. Source current of 0.5 mA when pulled down to 3.0 V. Sink current of at least 2.4 mA at Vol".
- PHASE 1 is listed separately: "Source current of at least 1.0 mA atVoh. Source current of 0.5 mA when pulled down to 3.0V. Sink current of at least 12.4 mA at Vol".

**AC characteristics** (Spec 1982 p. 17, spec page 11). Selected values, min / max:

- tas 40 / — nS.
- tda (/DEV fe to data out) — / 200 nS.
- tdsl (/DEV low) 200 nS min.
- tde (/DEV to /ENBLx or /WRREQ) — / 500 nS.
- tdph (/DEV to PHASEx) — / 500 nS.
- trdh (RDDATA high time) 300 nS min, note 5: "trdh and trdl must be at least twice the period of CLK to be properly synchronized".
- tckh and tckl 50 / 200 nS.
- tckp (FCLK period) 120 / 143 nS.
- tckpt (FCLK period, no timer) 120 / 500 nS.
- tsj (sampling jitter) — / 12.5 nS.
- tckwr (write clock re to WRDATA) — / 500 nS.
- twrj (write data jitter) — / 62.5 nS.
- trwrh (/RESET to /WRREQ high) — / 300 nS.

The 1984 drawing (Info 19840228 p. 14) prints the values one line out of step with the labels in the scan. Read in order, they match rev 19 except for three places [unsure]: tda max 180 rather than 200; tqdsl and tqdsh max 200 rather than 100; and the last rows read "300, 10, 500, 15, 15" where rev 19 has trwrh 300, tsj 12.5, tckwr 500, twrj 62.5, cp 15. The 1984 tckp label reads "FCLK period, with timer".

**Apple //c DB-19 external disk port** (Info Apple 2C p. 2). Columns are //c DB-19 pin, signal, and //e 20-pin header pin:

| DB-19 | Signal | //e header |
|---|---|---|
| 1 | GND | 1 |
| 2 | GND | 3 |
| 3 | GND | 5 |
| 4 | GND | 7 |
| 5 | -12V | 9 |
| 6 | +5V | 11 |
| 7 | +12V | 13 |
| 8 | +12V | 15 |
| 9 | EXTINT* | — |
| 10 | WRPROT | 20 |
| 11 | PHASE0 | 2 |
| 12 | PHASE1 | 4 |
| 13 | PHASE2 | 6 |
| 14 | PHASE3 | 8 |
| 15 | WRREQ | 10 |
| 16 | nc | — |
| 17 | ENBL2 | 14 |
| 18 | RDDATA | 16 |
| 19 | WRDATA | 18 |
| — | nc | 17 (+12V) |
| — | nc | 19 (+12V) |
| — | nc | 12 (+5V) |

Notes on the same page:

- "The //c disk cannot be used from an Apple //e disk controller card with a simple ribbon cable (20-pin insulation displacement (i.d.) header to i.d. DB-19), since pins 9 and 16 of the DB-19 should not be connected and pin 19 should connect to pin 20 of the card."
- "The Apple //e drive can not be used on a //c unless the DB-19 pin 10 is connected to pin 20 of the disk and DB-19 pin 9 is not connected (disk pin 19 should also be left unconnected)."
- [unsure: "pin 19 should connect to pin 20" conflicts with the table, which maps DB-19 pin 19 (WRDATA) to header pin 18. It is transcribed as printed.]
- The port has no SEL or HDSEL line, and no second enable.

**Disk II drive interface (5.25-inch)** (Info 19840228 p. 16, schematic 050-0007-01). The 20-pin flat-cable connector carries PHASE 0 to 3, ENBL (14), W PROT (20), RD DATA (16), WR DATA (18), WR REQ (10), +12 and -12. The phases drive ULN2003 darlington inputs, which matches the "two darlington inputs" note above. The write-protect and read-data returns go through 74LS125 buffers. Pull-up and resistor values on the schematic are too small to read reliably in this scan [illegible].

**Patent Figure 1** (p. 2): ENABL1 goes to "DRIVE MOTOR AMP 27". WRREQ, WRDATA and RDDATA go to "READ/WRITE AMPS 25". SENSE comes from switch 28. P0 to P3 go to "TRACK SELECT AMPS 23". All lines except ENABL1 are paralleled "TO SECOND DRIVE". Col. 4: "only one of two drives can be selected at any given point in time." And: "with respect to lines such as SENSE, which may be set for one drive and reset for the other, appropriate logic circuits are employed to ensure that only signals from the selected drive are input to the controller logic."

### 2.5 Mentions of UniDisk, Liron, Sony, 3.5", Macintosh drives, or SmartPort

- **UniDisk:** none in any of the fifteen documents.
- **Liron:** none.
- **Sony:** none.
- **3.5-inch drives:**
  - Parker 1994 throughout. All the 3.5-inch material in section 2.1 is from Parker.
  - Spec 1982 p. 2 (DigiBarn introduction, "not part of the original document"): "This device was used by the Apple IIc, Apple IIgs and Apple Macintosh computers to control attached Apple Disk II 5.25" floppy drives (capacity 143 KB) or Apple 3.5" floppy drives (capacity 800 KB)."
- **Macintosh:**
  - Spec 1982 p. 3 (DigiBarn page, quoting the BYTE interview of January 1985). BYTE: "The Macintosh uses a custom chip called the IWM ...". Wozniak: "The IWM takes that design and adds other features like the ability to go twice as fast - it can also do IBM format, double-density recordings."
  - Spec 1982 p. 3 also references US Patent 4,916,556 "Disk Drive Controller" (the SWIM), 10 April 1990.
  - Parker p. 25 bibliography: "Inside Macintosh, Volume III: Contains a description of most of the 3.5 Drive status and control bits." Also "Macintosh Family Hardware Reference: The 3.5 Drive information from Inside Macintosh is also reprinted in this book".
- **SmartPort:**
  - Parker p. 6, DISKREG bit E: "0 = 5.25-inch drive and smartport devices available".
  - Parker p. 19: "I recommend first reading "Beneath Apple DOS", ... and also the SmartPort chapter of the Firmware Reference."
  - Parker p. 25: "Apple IIGS Firmware Reference: Contains a lengthy description of the SmartPort firmware, including some clues as to the functioning of the 3.5 Drive hardware ... You will also need Apple IIGS Technical Note 25". Also "Apple IIGS Hardware Reference ... You will also need Apple IIGS Technical Note 30, which corrects numerous errors in the IWM descriptions."
  - There is no description of the SmartPort bus protocol anywhere in the set.
- **Related device:** Baum 1984 p. 3: "All Disk ][ Controller Cards, sold with either the Disk ][ or the Duodisk, use the state machine."

---

## 3. Not found

None of the fifteen documents covers the following topics:

1. **Write precompensation.** No document mentions it, for any mode or zone.
2. **The 3.5-inch drive's own connector pinout.** None of these documents gives the pinout of the Sony 3.5-inch mechanism connector, a Mac DB-19 pinout, or an IIGS or UniDisk DB-19 pinout with SEL or HDSEL. The only connector pinout is the //c 5.25-inch DB-19.
3. **Electrical specifications on the 3.5-inch drive side.** There are no output types (open collector or totem pole), pull-up values, logic levels or timing for the CA0, CA1, CA2, LSTRB, SEL, RD, WR, /WRREQ, /ENBL or SENSE lines as seen at the mechanism. The only electrical data is for the IWM's own pins (section 2.4).
4. **The words "open collector" or "open drain".** They do not appear in any document.
5. **LSTRB strobe timing.** There is no minimum pulse width, and no setup or hold time from CA0, CA1, CA2 or SEL to LSTRB. Parker gives only the software order (two consecutive soft-switch accesses).
6. **Status settle time after changing CA0, CA1, CA2 or SEL.** No document gives one.
7. **3.5-inch spindle speed.** There are no speed zones, RPM-per-zone tables or speed-control details. Parker gives only "Tachometer. 60 pulses per disk revolution". There is no PWM motor control (the Mac-style variable speed).
8. **3.5-inch timing figures beyond Parker's approximations.** Parker gives step "about 12 msec", eject "about 1/2 sec", and track 0 "valid beginning 12 msec after the step". There are no settling, motor spin-up or head-switch timings.
9. **Status entries $05, $07 and $0D, and control functions other than $00, $01, $03, $04, $08, $09 and $0D.** None are documented.
10. **3.5-inch GCR sector and track format.** There are no address or data field layouts, no sectors-per-track figures and no 2:1 encoding details. Parker defers to Beneath Apple DOS and the IIGS Firmware Reference.
11. **UniDisk 3.5, Liron and Sony.** None of the three is named anywhere.
12. **The SmartPort bus protocol.** It is mentioned only by name (section 2.5).
13. **A dedicated HDSEL pin.** In these documents, head select is done through the SEL line and status reads $01 and $03 (Parker). Parker notes that Apple's references wrongly describe DISKREG bit 7 as a head select.
14. **RD/WR signal polarity and pulse width at the 3.5-inch mechanism.** The only data is the IWM-side RDDATA high time (trdh 300 nS min) and the rule "one = transition on WRDATA".
