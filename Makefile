DOCKER_HOST=unix:///home/docker.sock
HOST_ROOT=/var/lib/incus/storage-pools/incus-zfs/containers/claude-sandbox/rootfs
INSTANCE_ROOT=$(abspath $(shell pwd)/../M5Cardputer-UserDemo)
OWNER=$(shell id -u):$(shell id -g)

# Weird. I need script -qec here, because docker won't give us output. Not sure
# what I'm doing wrong here, but this works and give the claude agent output.
.PHONY: agent-build
agent-build:
	script -qec '$(MAKE) remote-docker-build 2>&1'

.PHONY: local-docker-build
local-docker-build:
	mkdir -p $(INSTANCE_ROOT)/.cache  # mkdir before docker -v does with wrong perms
	/usr/bin/docker run --rm -i --user $(OWNER) -v $(INSTANCE_ROOT):/project -v $(INSTANCE_ROOT)/.cache:/.cache -w /project espressif/idf:v5.4.2 idf.py build

.PHONY: local-docker-flash
local-docker-flash:
	mkdir -p $(INSTANCE_ROOT)/.cache  # mkdir before docker -v does with wrong perms
	/usr/bin/docker run --rm -i --device /dev/ttyACM0 -v $(INSTANCE_ROOT):/project -v $(INSTANCE_ROOT)/.cache:/.cache -w /project espressif/idf:v5.4.2 esptool.py --port /dev/ttyACM0 write_flash 0x10000 build/cardputer-adv.bin

.PHONY: remote-docker-build
remote-docker-build:
	mkdir -p $(INSTANCE_ROOT)/.cache  # mkdir before docker -v does with wrong perms
	sudo /usr/bin/env DOCKER_HOST=$(DOCKER_HOST) /usr/bin/docker run --rm -i --user $(OWNER) -v $(HOST_ROOT)$(INSTANCE_ROOT):/project -v $(HOST_ROOT)$(INSTANCE_ROOT)/.cache:/.cache -w /project espressif/idf:v5.4.2 idf.py build

.PHONY: clean
clean:
	$(RM) -r ../M5Cardputer-UserDemo/build ../M5Cardputer-UserDemo/managed_components ../M5Cardputer-UserDemo/sdkconfig
