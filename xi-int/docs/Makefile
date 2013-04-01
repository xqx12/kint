all: linux.js

linux.js:
	./cvegen ../tests/linux/*.c > $@~
	mv $@~ $@

clean:
	rm -f linux.js

.PHONY: linux.js clean
