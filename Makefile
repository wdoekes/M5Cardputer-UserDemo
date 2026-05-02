DOCKER_HOST=unix:///home/docker.sock
HOST_ROOT=/var/lib/incus/storage-pools/incus-zfs/containers/claude-sandbox/rootfs
INSTANCE_ROOT=$(abspath $(shell pwd)/../M5Cardputer-UserDemo)

# Weird. I need script -qec here, because docker won't give us output. Not sure
# what I'm doing wrong here, but this works and give the claude agent output.
.PHONY: agent-build
agent-build:
	script -qec '$(MAKE) build 2>&1'

.PHONY: build
build:
	sudo /usr/bin/env DOCKER_HOST=$(DOCKER_HOST) /usr/bin/docker run --rm -i -v $(HOST_ROOT)$(INSTANCE_ROOT):/project -w /project espressif/idf:v5.4.2 idf.py build
	sudo chown -R $(shell whoami) ../M5Cardputer-UserDemo/build

.PHONY: clean
clean:
	$(RM) -r ../M5Cardputer-UserDemo/build
