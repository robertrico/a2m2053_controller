; HAMRTEST.SYSTEM: block-level test of a UniDisk 3.5 chain from the Apple IIe.
;
; ProDOS 8 system program, ACME syntax. Boots from a 5.25 floppy in slot 6
; and talks to the 3.5 drives through ProDOS READ_BLOCK / WRITE_BLOCK, the
; same path ProDOS itself uses (slot 2 remap of the third drive included).
;
;   drive 0  S5D1  stock UniDisk     light: 48 blocks written, a few reads
;   drive 1  S5D2  rebuilt drive     heavy
;   drive 2  S2D1  rebuilt drive     heavy
;   drive 3  S6D1  the 5.25 boot floppy (read, plus one scratch file)
;
; All three 3.5 disks must be blank ProDOS 800K disks. S5D2 and S2D1 are
; overwritten end to end; blocks 0-6 are saved first and put back at the
; end, so they finish as blank ProDOS disks again.
;
; Every stamped block holds its own block number and drive id, followed by
; pseudo-random bytes seeded from both, so a block written to the wrong
; place or the wrong drive fails its compare.
;
; Build: tools/apple/mkhamrtest.sh. Host test: tools/tests/test_hamrtest.py.

        !cpu 6502
        * = $2000

MLI     = $bf00
INIT    = $fb2f
HOME    = $fc58
RDKEY   = $fd0c
CROUT   = $fd8e
PRBYTE  = $fdda
COUT    = $fded
SETNORM = $fe84
SETKBD  = $fe89
SETVID  = $fe93

BUFA    = $4000         ; expected data / source block
BUFB    = $4200         ; data read back
IOBUF   = $4400         ; ProDOS file buffer, 1K
SAVE    = $5000         ; blocks 0-6 of drive 1, then of drive 2 ($0e00 each)

PA      = $06           ; compare pointers
PB      = $08
PS      = $fa           ; inline string pointer

NBLK    = 1600
MAXERR  = 8             ; a step gives up after this many errors

!macro ldw .v, .dst {
        lda #<.v
        sta .dst
        lda #>.v
        sta .dst+1
}

!macro setrange .start, .count, .stride {
        +ldw .start, curblk
        +ldw .count, r_cnt
        +ldw .stride, r_stride
        lda #200
        sta dotctr
}

!macro copy .src, .sblk, .sstride, .dst, .dblk, .cnt {
        lda #.src
        sta c_src
        lda #.dst
        sta c_dst
        +ldw .sblk, c_sblk
        +ldw .dblk, c_dblk
        +ldw .cnt, c_cnt
        +ldw .sstride, c_sstride
        jsr copy_step
}

; ---------------------------------------------------------------- main

        cld
        ldx #$ff
        txs
        jsr SETNORM
        jsr INIT
        jsr SETVID
        jsr SETKBD

run     jsr HOME
        lda #0
        sta failcnt
        jsr print
        !text "DISK HAMR CHAIN TEST", 13, 13, 0

        lda #0
        sta curd
-       jsr t_roll
        inc curd
        lda curd
        cmp #3
        bne -
        lda failcnt
        beq +
        jsr print
        !text "ROLL CALL FAILED. NOTHING WRITTEN.", 13, 0
        jmp again
+       jsr print
        !text "S5D2 AND S2D1 GET OVERWRITTEN.", 13
        !text "S5D1 GETS 48 BLOCKS. Y TO GO: ", 0
        jsr RDKEY
        and #$df
        cmp #$d9
        beq go
        jsr print
        !text 13, "STOPPED. NOTHING WRITTEN.", 13, 0
        jmp again

go      jsr print
        !text "Y", 13, 0

        jsr t_light
        ldx #<t_full
        ldy #>t_full
        jsr both
        ldx #<t_pat
        ldy #>t_pat
        jsr both
        ldx #<t_seek
        ldy #>t_seek
        jsr both

        ; 3.5 to 3.5: 200 blocks each way between the rebuilt drives, then
        ; 16 blocks between the stock drive and each rebuilt one.
        +copy 1, 400, 1, 2, 400, 200
        +copy 2, 1000, 1, 1, 1000, 200
        +copy 0, 8, 100, 1, 1300, 16
        +copy 0, 8, 100, 2, 1300, 16
        +copy 1, 300, 1, 0, 340, 16
        +copy 2, 320, 1, 0, 360, 16

        ldx #<t_from525
        ldy #>t_from525
        jsr both
        ldx #<t_file
        ldy #>t_file
        jsr both
        jsr t_chain
        ldx #<t_rest
        ldy #>t_rest
        jsr both

        jsr CROUT
        lda failcnt
        bne +
        jsr print
        !text "ALL PASS", 13, 0
        jmp again
+       jsr prdec
        jsr print
        !text " STEPS FAILED", 13, 0

again   jsr print
        !text "ANY KEY RERUNS", 0
        jsr RDKEY
        jmp run

; Run the step at X/Y for drive 1, then for drive 2.
both    stx bvec+1
        sty bvec+2
        lda #1
        sta curd
        jsr bvec
        lda #2
        sta curd
bvec    jmp $ffff

; --------------------------------------------------------------- steps

; Roll call: block 2 must be a ProDOS volume header saying 1600 blocks.
; Prints the volume name. Saves blocks 0-6 of the rebuilt drives.
!zone
t_roll  jsr print
        !text "ROLL CALL ", 0
        jsr prcurd
        jsr step_start
        lda curd
        sta iounit
        +ldw 2, curblk
        jsr x_rdB
        bcs .end
        lda BUFB+4
        and #$f0
        cmp #$f0
        bne .bad
        lda BUFB+$29
        cmp #<NBLK
        bne .bad
        lda BUFB+$2a
        cmp #>NBLK
        beq .good
.bad    lda #$29
        sta cmpoff
        lda #0
        sta cmpoff+1
        jsr err_cmp
        jmp .end
.good   lda #$af                ; "/"
        jsr COUT
        lda BUFB+4
        and #$0f
        sta fcnt
        lda #0
        sta ni
-       ldx ni
        cpx fcnt
        beq +
        lda BUFB+5,x
        ora #$80
        jsr COUT
        inc ni
        bne -
+       lda curd
        beq .end
        jsr savebuf
        +ldw 0, curblk
-       jsr rdblk
        bcc +
        jsr err_io
        jmp .end
+       inc bp_buf+1
        inc bp_buf+1
        inc curblk
        lda curblk
        cmp #7
        bne -
.end    jmp step_end

; Stock drive: 16 blocks spread over the disk, written then read back.
!zone
t_light jsr print
        !text "LIGHT WR/RD S5D1 ", 0
        jsr step_start
        lda #0
        sta genid
        sta iounit
        +setrange 8, 16, 100
        jsr wr_range
        +setrange 8, 16, 100
        jsr vf_range
        jmp step_end

; Rebuilt drive: write all 1600 blocks, then read all 1600 back.
!zone
t_full  jsr print
        !text "FULL WRITE ", 0
        jsr prcurd
        jsr step_start
        lda curd
        sta genid
        sta iounit
        +setrange 0, NBLK, 1
        jsr wr_range
        jsr step_end
        jsr print
        !text "FULL READ  ", 0
        jsr prcurd
        jsr step_start
        +setrange 0, NBLK, 1
        jsr vf_range
        jmp step_end

; Rebuilt drive: $00, $FF, $AA, $55 on 20 blocks spread over the disk.
; The blocks get their stamps back afterwards.
!zone
t_pat   jsr print
        !text "PATTERNS ", 0
        jsr prcurd
        jsr step_start
        lda curd
        sta genid
        sta iounit
        lda #3
        sta pidx
.next   ldx pidx
        lda pats,x
        ldy #0
-       sta BUFA,y
        sta BUFA+$100,y
        iny
        bne -
        +setrange 40, 20, 80
-       jsr x_wrA
        jsr range_next
        bne -
        +setrange 40, 20, 80
-       jsr x_rdB
        bcs +
        jsr x_cmp
+       jsr range_next
        bne -
        lda abortf
        bne .end
        dec pidx
        bpl .next
        +setrange 40, 20, 80
        jsr wr_range
.end    jmp step_end

; Rebuilt drive, read only: block 0 / 1599 butterfly closing inward, 50
; pairs, then 100 blocks scattered over the disk. Every read is compared.
!zone
t_seek  jsr print
        !text "SEEK ", 0
        jsr prcurd
        jsr step_start
        lda curd
        sta genid
        sta iounit
        +ldw 0, lo
        +ldw NBLK-1, hi
        lda #50
        sta fcnt
.bfly   lda lo
        sta curblk
        lda lo+1
        sta curblk+1
        jsr vf1
        lda hi
        sta curblk
        lda hi+1
        sta curblk+1
        jsr vf1
        lda abortf
        bne .end
        clc
        lda lo
        adc #16
        sta lo
        bcc +
        inc lo+1
+       sec
        lda hi
        sbc #16
        sta hi
        bcs +
        dec hi+1
+       dec fcnt
        bne .bfly
        +ldw 280, curblk
        lda #100
        sta fcnt
.scat   jsr vf1
        lda abortf
        bne .end
        clc
        lda curblk
        adc #<617
        sta curblk
        lda curblk+1
        adc #>617
        sta curblk+1
        lda curblk
        cmp #<NBLK
        lda curblk+1
        sbc #>NBLK
        bcc +
        lda curblk
        sbc #<NBLK
        sta curblk
        lda curblk+1
        sbc #>NBLK
        sta curblk+1
+       dec fcnt
        bne .scat
.end    jmp step_end

; The 5.25 boot floppy's 280 blocks onto blocks 20-299 of a rebuilt drive.
!zone
t_from525
        lda #3
        sta c_src
        lda curd
        sta c_dst
        +ldw 0, c_sblk
        +ldw 20, c_dblk
        +ldw 280, c_cnt
        +ldw 1, c_sstride
        jmp copy_step

; 32 blocks from a rebuilt drive into a scratch file on the 5.25, then the
; file read back and compared with the same blocks. The file is deleted.
!zone
t_file  jsr print
        !text "TO 5.25 FILE ", 0
        jsr prcurd
        jsr step_start
        lda #0
        sta curblk
        sta curblk+1
        sta phase
        jsr MLI
        !byte $c1
        !word p_destroy         ; left over from a run that was reset
        jsr MLI
        !byte $c0
        !word p_create
        bcc +
        jsr err_io
        jmp step_end
+       jsr MLI
        !byte $c8
        !word p_open
        bcc +
        jsr err_io
        jmp .destroy
+       lda p_open+5
        sta p_rw+1
        sta p_mark+1
        sta p_close+1
        lda curd
        sta iounit
.phase  +ldw 600, curblk
        lda #32
        sta fcnt
        ldx #>BUFA
        lda phase
        beq +
        ldx #>BUFB
+       stx p_rw+3
.loop   jsr x_rdA
        bcs .adv
        lda phase
        bne .read
        jsr MLI
        !byte $cb
        !word p_rw
        bcc .adv
        jsr err_io
        jmp .adv
.read   jsr MLI
        !byte $ca
        !word p_rw
        bcc +
        jsr err_io
        jmp .adv
+       jsr x_cmp
.adv    lda abortf
        bne .close
        inc curblk
        bne +
        inc curblk+1
+       dec fcnt
        bne .loop
        lda phase
        bne .close
        inc phase
        jsr MLI
        !byte $ce
        !word p_mark
        bcc .phase
        jsr err_io
.close  jsr MLI
        !byte $cc
        !word p_close
.destroy
        jsr MLI
        !byte $c1
        !word p_destroy
        jmp step_end

; One block from each of the three drives in turn, 50 rounds.
!zone
t_chain jsr print
        !text "CHAIN SWITCH ALL ", 0
        jsr step_start
        lda #0
        sta ni
        +ldw 8, lo
.loop   lda #0
        sta genid
        sta iounit
        lda lo
        sta curblk
        lda lo+1
        sta curblk+1
        jsr vf1
        lda #1
        sta genid
        sta iounit
        jsr .blk
        jsr vf1
        lda #2
        sta genid
        sta iounit
        jsr .blk
        jsr vf1
        lda abortf
        bne .end
        clc
        lda lo
        adc #100
        sta lo
        bcc +
        inc lo+1
+       lda lo
        cmp #<1608
        lda lo+1
        sbc #>1608
        bcc +
        +ldw 8, lo
+       inc ni
        lda ni
        cmp #50
        bne .loop
.end    jmp step_end
.blk    clc
        lda ni
        adc #<700
        sta curblk
        lda #>700
        adc #0
        sta curblk+1
        rts

; Put blocks 0-6 back on a rebuilt drive and read them back.
!zone
t_rest  jsr print
        !text "RESTORE 0-6 ", 0
        jsr prcurd
        jsr step_start
        lda curd
        sta iounit
        jsr savebuf
        +ldw 0, curblk
-       jsr wrblk
        bcc +
        jsr err_io
        jmp .end
+       inc bp_buf+1
        inc bp_buf+1
        inc curblk
        lda curblk
        cmp #7
        bne -
        jsr savebuf
        lda bp_buf+1
        sta savep
        +ldw 0, curblk
-       jsr x_rdB
        bcs +
        lda #0
        sta PA
        sta PB
        lda savep
        sta PA+1
        lda #>BUFB
        sta PB+1
        jsr cmp512
        bcc +
        jsr err_cmp
+       inc savep
        inc savep
        inc curblk
        lda curblk
        cmp #7
        bne -
.end    jmp step_end

; Copy c_cnt blocks from drive c_src (from c_sblk, step c_sstride) to drive
; c_dst (from c_dblk, step 1). Then read both sides again and compare.
!zone
copy_step
        jsr print
        !text "COPY ", 0
        ldx c_src
        jsr prdrv
        lda #$be                ; ">"
        jsr COUT
        ldx c_dst
        jsr prdrv
        lda #$a0
        jsr COUT
        jsr step_start
        lda #0
        sta phase
.phase  ldx #5
-       lda c_sblk,x
        sta w_sblk,x
        dex
        bpl -
        lda #200
        sta dotctr
.loop   lda c_src
        sta iounit
        lda w_sblk
        sta curblk
        lda w_sblk+1
        sta curblk+1
        jsr x_rdA
        bcs .adv
        lda c_dst
        sta iounit
        lda w_dblk
        sta curblk
        lda w_dblk+1
        sta curblk+1
        lda phase
        bne .ver
        jsr x_wrA
        jmp .adv
.ver    jsr x_rdB
        bcs .adv
        jsr x_cmp
.adv    lda abortf
        bne .end
        jsr dot
        clc
        lda w_sblk
        adc c_sstride
        sta w_sblk
        lda w_sblk+1
        adc c_sstride+1
        sta w_sblk+1
        inc w_dblk
        bne +
        inc w_dblk+1
+       lda w_cnt
        bne +
        dec w_cnt+1
+       dec w_cnt
        lda w_cnt
        ora w_cnt+1
        beq +
        jmp .loop
+       lda phase
        bne .end
        inc phase
        jmp .phase
.end    jmp step_end

; ------------------------------------------------------- stamped blocks

; Write stamped blocks over the range set by +setrange.
!zone
wr_range
-       jsr wr1
        jsr range_next
        bne -
        rts

; Read the range back and compare each block with its stamp.
vf_range
-       jsr vf1
        jsr range_next
        bne -
        rts

wr1     jsr gen
        jmp x_wrA

vf1     jsr gen
        jsr x_rdB
        bcs +
        jsr x_cmp
+       rts

; Step to the next block of the range. Z set when the range is done or the
; step has given up.
!zone
range_next
        lda abortf
        bne .stop
        jsr dot
        clc
        lda curblk
        adc r_stride
        sta curblk
        lda curblk+1
        adc r_stride+1
        sta curblk+1
        lda r_cnt
        bne +
        dec r_cnt+1
+       dec r_cnt
        lda r_cnt
        ora r_cnt+1
        rts
.stop   lda #0
        rts

; A dot on the screen every 200 blocks.
dot     dec dotctr
        bne +
        lda #200
        sta dotctr
        lda #$ae
        jsr COUT
+       rts

; Fill BUFA with the stamp for drive genid, block curblk: block number,
; drive id, inverted low byte, then 5x+43 mod 256 from a seed built from
; drive and block. The second half restarts from the last value EOR $5A.
!zone
gen     lda curblk+1
        asl
        asl
        asl
        asl
        ora curblk+1
        eor curblk
        ldx genid
        eor seeds,x
        ldy #0
-       sta gx
        asl
        asl
        clc
        adc gx
        clc
        adc #$2b
        sta BUFA,y
        iny
        bne -
        eor #$5a
-       sta gx
        asl
        asl
        clc
        adc gx
        clc
        adc #$2b
        sta BUFA+$100,y
        iny
        bne -
        lda curblk
        sta BUFA
        eor #$ff
        sta BUFA+3
        lda curblk+1
        sta BUFA+1
        lda genid
        sta BUFA+2
        rts

; ------------------------------------------------------------ block I/O

; Read or write block curblk on drive iounit, buffer at bp_buf.
; Carry set on error, ProDOS error code in A.
rdblk   lda #$80
        !byte $2c               ; BIT abs: skip the next instruction
wrblk   lda #$81
        sta blkcmd
        ldx iounit
        lda units,x
        sta bp_unit
        lda curblk
        sta bp_blk
        lda curblk+1
        sta bp_blk+1
        jsr MLI
blkcmd  !byte $80
        !word bparm
        rts

; The same, with the buffer chosen and the error recorded. Carry set on error.
!zone
x_rdA   jsr bufa
        jsr rdblk
        bcc +
        jmp err_io
+       rts
x_rdB   lda #>BUFB
        jsr setbuf
        jsr rdblk
        bcc +
        jmp err_io
+       rts
x_wrA   jsr bufa
        jsr wrblk
        bcc +
        jmp err_io
+       rts
bufa    lda #>BUFA
setbuf  sta bp_buf+1
        lda #0
        sta bp_buf
        rts

; bp_buf = save area for drive curd (1 or 2).
savebuf lda #0
        sta bp_buf
        ldx curd
        lda savehi,x
        sta bp_buf+1
        rts

; Compare BUFA with BUFB and record a mismatch. Carry set on mismatch.
!zone
x_cmp   lda #0
        sta PA
        sta PB
        lda #>BUFA
        sta PA+1
        lda #>BUFB
        sta PB+1
        jsr cmp512
        bcc +
        jmp err_cmp
+       rts

; Compare 512 bytes at (PA) and (PB). Carry set on mismatch, offset in cmpoff.
!zone
cmp512  lda #0
        sta cmpoff+1
        ldy #0
-       lda (PA),y
        cmp (PB),y
        bne .bad
        iny
        bne -
        inc PA+1
        inc PB+1
        inc cmpoff+1
        lda cmpoff+1
        cmp #2
        bne -
        clc
        rts
.bad    sty cmpoff
        sec
        rts

; ------------------------------------------------------- step reporting

step_start
        lda #0
        sta errcnt
        sta abortf
        rts

; Record an I/O error (code in A) or a compare error (offset in cmpoff) at
; curblk. Only the first error of a step is kept; the rest are counted.
; Both return with carry set.
!zone
err_io  ldx errcnt
        bne .count
        sta e_code
        lda #0
        sta e_type
        beq .blk
err_cmp ldx errcnt
        bne .count
        lda #1
        sta e_type
        lda cmpoff
        sta e_off
        lda cmpoff+1
        sta e_off+1
.blk    lda curblk
        sta e_blk
        lda curblk+1
        sta e_blk+1
.count  inc errcnt
        lda errcnt
        cmp #MAXERR
        bcc +
        lda #1
        sta abortf
+       sec
        rts

!zone
step_end
        lda errcnt
        bne .fail
        jsr print
        !text " PASS", 13, 0
        rts
.fail   inc failcnt
        jsr print
        !text 13, "  ERR BLK=", 0
        lda e_blk+1
        jsr PRBYTE
        lda e_blk
        jsr PRBYTE
        lda e_type
        bne .cmp
        jsr print
        !text " CODE=", 0
        lda e_code
        jsr PRBYTE
        jmp .n
.cmp    jsr print
        !text " OFFS=", 0
        lda e_off+1
        jsr PRBYTE
        lda e_off
        jsr PRBYTE
.n      jsr print
        !text " N=", 0
        lda errcnt
        jsr PRBYTE
        jmp CROUT

; -------------------------------------------------------------- screen

; Print the zero-terminated string that follows the JSR.
!zone
print   pla
        sta PS
        pla
        sta PS+1
-       inc PS
        bne +
        inc PS+1
+       ldy #0
        lda (PS),y
        beq +
        ora #$80
        jsr COUT
        jmp -
+       lda PS+1
        pha
        lda PS
        pha
        rts

; Print the name of drive curd and a space.
prcurd  ldx curd
        jsr prdrv
        lda #$a0
        jmp COUT

; Print the four-character name of drive X.
!zone
prdrv   txa
        asl
        asl
        sta ni
        lda #4
        sta fcnt
-       ldx ni
        lda names,x
        ora #$80
        jsr COUT
        inc ni
        dec fcnt
        bne -
        rts

; Print failcnt in decimal (0-99).
!zone
prdec   lda failcnt
        ldx #0
-       cmp #10
        bcc +
        sbc #10
        inx
        bne -
+       pha
        txa
        beq +
        ora #$b0
        jsr COUT
+       pla
        ora #$b0
        jmp COUT

; ---------------------------------------------------------------- data

units   !byte $50, $d0, $20, $60        ; ProDOS unit numbers
names   !text "S5D1S5D2S2D1S6D1"
seeds   !byte $3c, $a7, $59
savehi  !byte 0, >SAVE, (>SAVE)+$0e
pats    !byte $55, $aa, $ff, $00

bparm   !byte 3
bp_unit !byte 0
bp_buf  !word BUFA
bp_blk  !word 0

path    !byte 17
        !text "/HAMRTEST/SCRATCH"
p_destroy
        !byte 1
        !word path
p_create
        !byte 7
        !word path
        !byte $c3, $06          ; full access, BIN
        !word 0
        !byte 1                 ; seedling
        !word 0, 0
p_open  !byte 3
        !word path
        !word IOBUF
        !byte 0
p_rw    !byte 4, 0
        !word BUFA, 512, 0
p_mark  !byte 2, 0, 0, 0, 0
p_close !byte 1, 0

curd    !byte 0         ; drive a step is working on
iounit  !byte 0         ; drive for the next block call
genid   !byte 0         ; drive id in the stamp
curblk  !word 0
r_cnt   !word 0
r_stride !word 0
dotctr  !byte 0
lo      !word 0
hi      !word 0
fcnt    !byte 0
ni      !byte 0
pidx    !byte 0
phase   !byte 0
savep   !byte 0
gx      !byte 0
cmpoff  !word 0
errcnt  !byte 0
abortf  !byte 0
failcnt !byte 0
e_type  !byte 0
e_code  !byte 0
e_blk   !word 0
e_off   !word 0
c_src   !byte 0
c_dst   !byte 0
c_sstride !word 0
c_sblk  !word 0         ; c_sblk, c_dblk, c_cnt stay together, in this order
c_dblk  !word 0
c_cnt   !word 0
w_sblk  !word 0         ; working copies, same order
w_dblk  !word 0
w_cnt   !word 0
