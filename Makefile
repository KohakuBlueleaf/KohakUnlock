PYTHON ?= python3
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Werror
SANITIZE ?= -fsanitize=address,undefined
KDIR ?= /lib/modules/$(shell uname -r)/build
ENABLE_EXPERIMENT ?= 0
.PHONY: test test-private module clean
test:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_dma_host.c -o build/test_dma
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_pack_host.c -o build/test_pack
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_flow_host.c -o build/test_flow
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_tu104_dma_host.c -o build/test_tu104_dma
	$(CC) $(CFLAGS) $(SANITIZE) -I. -DKOH_PROFILE='"tests/profiles/tu104_synth.h"' tests/test_tu104_core.c -o build/test_tu104_core
	./build/test_dma
	./build/test_pack
	./build/test_flow
	./build/test_tu104_dma
	./build/test_tu104_core
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

test-private: test
	@test -n "$(ASSETS)" || (echo 'Set ASSETS to your privately obtained inputs'; exit 2)
	$(PYTHON) tools/check_assets.py --assets "$(ASSETS)"
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_core_port.c -o build/test_core
	./build/test_core "$(ASSETS)"

module:
	$(MAKE) -C kernel KDIR="$(KDIR)" ENABLE_EXPERIMENT="$(ENABLE_EXPERIMENT)"

clean:
	$(MAKE) -C kernel KDIR="$(KDIR)" clean
