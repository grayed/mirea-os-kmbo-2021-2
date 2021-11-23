PROGS =			avrg avrg-mt avrg-mt-chan
CFLAGS-mt =		-pthread
CFLAGS-mt-chan =	-pthread

all: foo ${PROGS}

clean:
	rm -f -- ${PROGS} foo

.for _p in ${PROGS}
CFLAGS${_p:C/^avrg//} ?=
${_p}: ${_p}.c
	${CC} ${CFLAGS} ${CFLAGS${_p:C/^avrg//}} -o $@ $@.c
.endfor

foo:
	jot -r 1000000 >$@

.PHONY: clean
