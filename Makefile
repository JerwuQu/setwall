setwall.exe: setwall.c
	x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -pedantic -O2 -static -mconsole -o $@ $^

.PHONY: clean
clean:
	rm -f setwall.exe
