CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -I.
OBJS = config.o overlay.o

liboverlay.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) liboverlay.a

.PHONY: clean
