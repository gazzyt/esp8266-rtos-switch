#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_EXTRA_CLEAN := default_generated.h

default_html.o: default_generated.h

default_generated.h: $(COMPONENT_PATH)/default.html
	xxd -i $^ | sed 's/unsigned char/const char/; s/unsigned int/const unsigned int/' > $@
