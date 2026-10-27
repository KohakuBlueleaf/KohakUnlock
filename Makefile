PYTHON ?= python3
CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Werror
SANITIZE ?= -fsanitize=address,undefined
KDIR ?= /lib/modules/$(shell uname -r)/build
ENABLE_EXPERIMENT ?= 0
DIAG_PROFILE ?= historical
EXEC_PROFILE ?= historical
.PHONY: test test-private test-private-nvidia module clean
test:
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_dma_host.c -o build/test_dma
	$(CC) $(CFLAGS) $(SANITIZE) -DTX_PROFILE_NVIDIA_1E07_12A4 -I. tests/test_dma_host.c -o build/test_dma_nvidia
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_pack_host.c -o build/test_pack
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_flow_host.c -o build/test_flow
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_core_port.c -o build/test_core_synthetic
	$(CC) $(CFLAGS) $(SANITIZE) -DTX_PROFILE_NVIDIA_1E07_12A4 -I. tests/test_core_port.c -o build/test_core_nvidia_synthetic
	./build/test_dma
	./build/test_dma_nvidia
	./build/test_pack
	./build/test_flow
	./build/test_core_synthetic --synthetic
	./build/test_core_nvidia_synthetic --synthetic
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

test-private: test
	@test -n "$(ASSETS)" || (echo 'Set ASSETS to your privately obtained inputs'; exit 2)
	$(PYTHON) tools/check_assets.py --assets "$(ASSETS)"
	$(CC) $(CFLAGS) $(SANITIZE) -I. tests/test_core_port.c -o build/test_core
	./build/test_core "$(ASSETS)"

# Private CPU model; hardware trials require a separate explicit kernel build.
test-private-nvidia: test
	@test -n "$(ASSETS)" || (echo 'Set ASSETS to your privately obtained candidate inputs'; exit 2)
	$(PYTHON) tools/check_assets.py --assets "$(ASSETS)" --profile profiles/nvidia-tu102-300a-22g.json
	$(CC) $(CFLAGS) $(SANITIZE) -DTX_PROFILE_NVIDIA_1E07_12A4 -I. tests/test_core_port.c -o build/test_core_nvidia
	./build/test_core_nvidia "$(ASSETS)"

module:
	$(MAKE) -C kernel KDIR="$(KDIR)" ENABLE_EXPERIMENT="$(ENABLE_EXPERIMENT)" DIAG_PROFILE="$(DIAG_PROFILE)" EXEC_PROFILE="$(EXEC_PROFILE)"

clean:
	$(MAKE) -C kernel KDIR="$(KDIR)" clean
