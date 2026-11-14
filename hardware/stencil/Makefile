# Stencil fixture and stencil frame for Disk Hamr rev 1.0, generated with
# stencilframer (https://hyperglitch.com/articles/stencilframer, MIT) and
# OpenSCAD. See README.md in this directory.
#
# Local tool paths come from ../../.env.local (copy ../../.env.example), same
# scheme as firmware/Makefile: a value from the environment or the command
# line wins over the file.
ENV_LOCAL := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../.env.local)
ifneq ($(wildcard $(ENV_LOCAL)),)
  $(foreach line,$(shell grep -v '^\#' $(ENV_LOCAL) | grep '='),$(eval $(subst =,?=,$(line))))
endif

STENCILFRAMER ?= ../../../stencilframer/stencilframer.py
OPENSCAD      ?= openscad

# Board outline. The fabrication Gerber, not the .kicad_pcb: the outline has
# two gr_curve (Bezier) edges that stencilframer does not parse, and the
# Gerber is what PCBWay cut the stencil from.
OUTLINE := ../kicad/fabrication/a2m2053-Edge_Cuts.gbr

# PCBWay stencil: 160 x 170 mm, 0.12 mm stainless, board 100 x 110 mm
# centred. Margin = (stencil - board) / 2 per side. Measure the real stencil
# (board aperture to sheet edge) and override on the command line if PCBWay
# did not centre it: make MARGIN_L=28 MARGIN_R=32 ...
MARGIN_L ?= 30
MARGIN_R ?= 30
MARGIN_T ?= 30
MARGIN_B ?= 30
PCB_T    ?= 1.6

# Clearance around the stencil in the holder pocket (per side). The frame is
# cut to the same size on purpose: the press fit against the pocket wall is
# what keeps it in while the squeegee pulls on it, there is no other
# retention. Lower FRAME_OFFSET only if a print comes out too tight to seat.
STENCIL_OFFSET ?= 0.1
FRAME_OFFSET   ?= $(STENCIL_OFFSET)

# MARGIN_T/B are top/bottom as seen in KiCad's board view (Y down), the same
# as check_stencil.py. stencilframer works in Gerber coordinates (Y up) and
# applies its --margin-top to Y-min, which is the KiCad-view bottom, so the
# two are swapped on the way in.
MARGINS := -l $(MARGIN_L) -r $(MARGIN_R) -t $(MARGIN_B) -b $(MARGIN_T)
SF      := python3 $(STENCILFRAMER) --openscad $(OPENSCAD) $(MARGINS)

.PHONY: all preview check clean
all: holder.stl frame.stl
preview: holder.png frame.png

# Expected aperture-to-sheet-edge distances for the margins above, to check
# against the real stencil with calipers before printing. Writes
# check_stencil.svg (and .png if qlmanage is available).
check:
	python3 check_stencil.py -l $(MARGIN_L) -r $(MARGIN_R) -t $(MARGIN_T) -b $(MARGIN_B)
	-qlmanage -t -s 1600 -o . check_stencil.svg >/dev/null 2>&1 && mv check_stencil.svg.png check_stencil.png

# Fixture: pocket for the PCB, pocket for the stencil on top of it.
holder.stl holder.png holder.scad: $(OUTLINE) Makefile
	$(SF) -p $(PCB_T) --stencil-offset $(STENCIL_OFFSET) $(OUTLINE) $@

# Frame that holds the stencil edge down on the fixture.
frame.stl frame.png frame.scad: $(OUTLINE) Makefile
	$(SF) --frame --stencil-offset $(FRAME_OFFSET) $(OUTLINE) $@

clean:
	rm -f holder.stl holder.png holder.scad frame.stl frame.png frame.scad check_stencil.svg check_stencil.png
