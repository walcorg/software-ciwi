# =============================================================================
# CiWi interface on a PC: scripted input, checks and screenshots.
#
#   make test     build, run, convert the screenshots to PNG and a contact sheet
# =============================================================================
ROOT := ../..
SRCS := $(ROOT)/ciwi_ui/ui_app.c $(ROOT)/ciwi_ui/ui_gfx.c $(ROOT)/ciwi_ui/ui_input.c \
        $(ROOT)/ciwi_ui/ui_strings.c $(ROOT)/drivers/display/font5x7.c ui_test.c
CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-missing-field-initializers -I$(ROOT)/ciwi_ui

ui_test: $(SRCS) $(wildcard $(ROOT)/ciwi_ui/*.h)
	$(CC) $(CFLAGS) $(SRCS) -o $@

test: ui_test
	rm -rf shots && ./ui_test shots
	python3 sheet.py shots

clean:
	rm -rf ui_test shots

.PHONY: test clean
