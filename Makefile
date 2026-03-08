all: kmd uma overlay

kmd:
	$(MAKE) -C kmd

uma:
	$(MAKE) -C uma

overlay: uma
	$(MAKE) -C overlay

clean:
	$(MAKE) -C kmd clean
	$(MAKE) -C uma clean
	$(MAKE) -C overlay clean

install: kmd
	$(MAKE) -C kmd install

uninstall:
	$(MAKE) -C kmd uninstall

.PHONY: all kmd uma overlay clean install uninstall
