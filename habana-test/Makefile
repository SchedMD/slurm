IS_CONTROLLER ?= true
TEST_CONTAINER_IMAGE ?= vault.habana.ai/gaudi-docker/1.15.1/ubuntu22.04/habanalabs/pytorch-installer-2.2.0:1.15.1-15
MAKEFILE_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
REPO_DIR := $(shell dirname $(MAKEFILE_DIR))
SHELL := /bin/bash
VERSION ?= ""
SECOND_NODE ?= ""

habana-configure:
	# Intel GAUDI drivers
	if ! hl-smi -Q name --format=csv || ! hl-smi -Q name --format=csv | grep -v name | grep -qv N/A; then \
		wget -nv https://vault.habana.ai/artifactory/gaudi-installer/latest/habanalabs-installer.sh && \
		chmod +x habanalabs-installer.sh && \
		yes | ./habanalabs-installer.sh install --type base || \
		hl-smi -Q name --format=csv; \
	fi
	#
	# Intel GAUDI runtime
	if ! grep -q habana /etc/docker/daemon.json; then \
		sudo apt install -y habanalabs-container-runtime && \
		sudo cp docker-daemon.json /etc/docker/daemon.json && \
		sudo systemctl restart docker.service; \
	fi
	#
	# Intel GAUDI external ports
	wget https://raw.githubusercontent.com/HabanaAI/Setup_and_Install/main/utils/manage_network_ifs.sh && \
	chmod +x ./manage_network_ifs.sh && \
	./manage_network_ifs.sh --up

configure:
	# Slurm pre-requisits
	getent group slurm || sudo groupadd -g 64030 slurm
	id slurm || sudo useradd -u 64030 -g slurm --system --no-create-home slurm
	[ -d /etc/slurm ] || sudo mkdir /etc/slurm
	[ -d /var/log/slurm ] || sudo mkdir /var/log/slurm
	[ -d /var/spool/slurmd ] || sudo mkdir  /var/spool/slurmd
	[ -d /var/spool/slurmctld ] || sudo mkdir /var/spool/slurmctld
	sudo chown slurm:slurm /var/log/slurm /var/spool/slurmd /var/spool/slurmctld
	echo 'export PATH=$$PATH:/usr/local/slurm/bin' | sudo tee /etc/profile.d/slurm.sh
	echo '/usr/lib/habanalabs' | sudo tee /etc/ld.so.conf.d/habanalabs.conf
	sudo ldconfig
	#
	# Slurm config files
	sudo cp slurm.conf /etc/slurm/slurm.conf
	sudo cp gres.conf /etc/slurm/gres.conf
	sudo cp cgroup.conf /etc/slurm/cgroup.conf
	NodeName=$$(hostname -s); \
	CPUs=$$(lscpu | awk '$$1=="CPU(s):" {print $$2}'); \
	RealMemory=$$(free -m | awk '$$1=="Mem:" {print $$NF}'); \
	Sockets=$$(lscpu | awk '$$1=="Socket(s):" {print $$NF}'); \
	CoresPerSocket=$$(lscpu | awk '$$1=="Core(s)" {print $$NF}'); \
	ThreadsPerCore=$$(lscpu | awk '$$1=="Thread(s)" {print $$NF}'); \
	Gres=gpu:$$(set -o pipefail; hl-smi -Q name --format=csv | grep -v name | head -n 1):$$(ls /dev/accel/accel[0-9]* | wc -l); \
	sudo sed -i \
		-e "s/SlurmctldHostPlaceholder/$$NodeName/" \
		-e "s/NodeNamePlaceholder/$$NodeName/" \
		-e "s/CPUsPlaceholder/$$CPUs/" \
		-e "s/RealMemoryPlaceholder/$$RealMemory/" \
		-e "s/SocketsPlaceholder/$$Sockets/" \
		-e "s/CoresPerSocketPlaceholder/$$CoresPerSocket/" \
		-e "s/ThreadsPerCorePlaceholder/$$ThreadsPerCore/" \
		-e "s/GresPlaceholder/$$Gres/" \
		/etc/slurm/slurm.conf

build-and-install-source: build-source install-source

build-source:
	$(call update-version)
	sudo apt install munge libmunge-dev libglib2.0-dev libdbus-1-dev -y
	cd $(REPO_DIR); \
	autoreconf; \
	./configure --prefix=/usr/local/slurm --sysconfdir=/etc/slurm --with-systemdsystemunitdir=/lib/systemd/system; \
	make

install-source: configure
	cd $(REPO_DIR); \
	sudo make install
	if [ "$(IS_CONTROLLER)" == true ]; then \
		sudo systemctl enable --now slurmctld.service; \
	fi
	sudo systemctl enable --now slurmd.service

build-and-install-debian-package: build-debian-package install-debian-package

build-debian-package:
	$(call update-version)
	sudo apt install build-essential fakeroot devscripts equivs libglib2.0-dev libdbus-1-dev libmunge-dev libgtk2.0-dev libpam0g-dev libperl-dev liblua5.3-dev dh-exec librrd-dev libipmimonitoring-dev hdf5-helpers libfreeipmi-dev libhdf5-dev man2html-base libcurl4-openssl-dev libhttp-parser-dev libyaml-dev libjson-c-dev libjwt-dev liblz4-dev librdkafka-dev -y
	cd $(REPO_DIR); \
	mkdir debian-packages/; \
	autoreconf; \
	yes | mk-build-deps -i debian/control; \
	debuild -b -uc -us; \
	mv ../slurm-smd*.deb debian-packages

install-debian-package: configure
	cd $(REPO_DIR)/debian-packages; \
	sudo apt install ./slurm-smd_*.deb ./slurm-smd-slurmd_*.deb ./slurm-smd-client_*.deb -y
	if [ "$(IS_CONTROLLER)" == true ]; then \
		sudo apt install $(REPO_DIR)/debian-packages/slurm-smd-slurmctld_*.deb -y; \
	fi

test: test-single-card test-8-cards test-16-cards

test-single-card:
	. /etc/profile.d/slurm.sh; \
	TEST_CARDS_NUMBER=$$(srun --gres=gpu:1 bash -c 'docker run --init --rm --runtime=habana -e HABANA_VISIBLE_DEVICES $(TEST_CONTAINER_IMAGE) /bin/bash -c "hl-smi -Q name --format=csv | grep -v name | wc -l"'); \
	test "$$TEST_CARDS_NUMBER" -eq 1
	$(call prompt-status)

test-8-cards:
	. /etc/profile.d/slurm.sh; \
	srun --gres=gpu:8 --nodes=1 --ntasks-per-node=1 bash -c 'docker run --init --rm --runtime=habana -e HABANA_VISIBLE_DEVICES -e SLURM_NODEID -e SLURM_LAUNCH_NODE_IPADDR --net=host $(TEST_CONTAINER_IMAGE) /bin/bash -c "cd hccl_demo; HCCL_COMM_ID=$$SLURM_LAUNCH_NODE_IPADDR:5555 python3 run_hccl_demo.py --nranks 8 --node_id $$SLURM_NODEID --size 32m --test all_reduce --loop 1000 --ranks_per_node 8"'; \
	$(call prompt-status)

test-16-cards:
	# Test connection to the second node with seemless SSH
	if [ -z "$(SECOND_NODE)" ]; then \
		echo "SECOND_NODE variable was not provided"; \
		exit 1; \
	fi
	ssh $(SECOND_NODE) exit 0
	#
	# Ensure that habana ENV is already configured on the second node
	ssh $(SECOND_NODE) "hl-smi -Q name --format=csv"
	ssh $(SECOND_NODE) grep habana /etc/docker/daemon.json
	MANAGE_NETWORK_IFS=$$(ssh $(SECOND_NODE) "curl -s https://raw.githubusercontent.com/HabanaAI/Setup_and_Install/main/utils/manage_network_ifs.sh | bash -s -- --status"); \
	if [[ "$${MANAGE_NETWORK_IFS}" =~ down ]]; then \
		exit 1; \
	fi
	#
	# Install slurm the way it was installed on the first node as a second single node cluster
	ssh $(SECOND_NODE) "mkdir -p $(REPO_DIR)"; \
	scp -rp $(REPO_DIR)/* $(SECOND_NODE):$(REPO_DIR); \
	INSTALLATION_TYPE=$$(test -d $(REPO_DIR)/debian-packages && echo debian-package || echo source); \
	ssh $(SECOND_NODE) "cd $(MAKEFILE_DIR); make install-$$INSTALLATION_TYPE $(filter-out --,$(MAKEFLAGS)) IS_CONTROLLER='false'"
	#
	# Configure slurm as a single two node cluster
	# /etc/slurm/slurm.conf
	SLURM_SECONDE_NODE_DEFINITION=$$(ssh $(SECOND_NODE) "grep NodeName /etc/slurm/slurm.conf"); \
	SLURM_SECONDE_NODE_NAME=$$(echo $${SLURM_SECONDE_NODE_DEFINITION} | sed -r 's/^.*NodeName=([^ ]*).*$$/\1/g'); \
	sudo sed -i "/$${SLURM_SECONDE_NODE_NAME}/d" /etc/slurm/slurm.conf; \
	echo $${SLURM_SECONDE_NODE_DEFINITION} | sudo tee -a /etc/slurm/slurm.conf; \
	sudo cp /etc/slurm/slurm.conf /tmp/slurm.conf; \
	sudo chown $$(whoami) /tmp/slurm.conf; \
	scp -p /tmp/slurm.conf $(SECOND_NODE):/tmp; \
	rm /tmp/slurm.conf; \
	ssh $(SECOND_NODE) "sudo mv /tmp/slurm.conf /etc/slurm/slurm.conf"
	#
	# /etc/munge/munge.key
	sudo cp -p /etc/munge/munge.key /tmp; \
	sudo chown $$(whoami) /tmp/munge.key; \
	scp -p /tmp/munge.key $(SECOND_NODE):/tmp; \
	rm /tmp/munge.key; \
	ssh $(SECOND_NODE) "sudo mv /tmp/munge.key /etc/munge/munge.key; sudo chown munge:munge /etc/munge/munge.key"; \
	#
	# Restart and reload configuration
	ssh $(SECOND_NODE) "sudo systemctl restart munge slurmd"; \
	sudo scontrol reconfigure
	#
	# Test 16 cards
	export NODE_0_IPADDRESS=$$(srun hostname -i); \
	srun --gres=gpu:8 --nodes=2 --ntasks-per-node=1 --export ALL bash -c 'docker run --init --rm --runtime=habana -e HABANA_VISIBLE_DEVICES -e SLURM_NODEID -e SLURM_LAUNCH_NODE_IPADDR --net=host $(TEST_CONTAINER_IMAGE) /bin/bash -c "cd hccl_demo; HCCL_COMM_ID=$$NODE_0_IPADDRESS:5555 python3 run_hccl_demo.py --nranks 16 --node_id $$SLURM_NODEID --size 32m --test all_reduce --loop 1000 --ranks_per_node 8"'; \
	$(call prompt-status)

define prompt-status
	if [ $$? -eq 0 ]; then \
		echo "SUCCESS: $@"; \
	else \
		echo "FAILED: $@"; \
		false; \
	fi
endef

define update-version
	if [ -n "$(VERSION)" ]; then \
		if [[ ! "$(VERSION)" =~ [0-9]+\.[0-9]+\.[0-9]+ ]]; then \
			echo "Version is not in the format of [MAJOR].[MINOR].[MICRO]"; \
			exit 1; \
		fi; \
		cp debian-changelog $(REPO_DIR)/debian/changelog; \
		MAJOR_VERSION=$$(echo $(VERSION) | cut -d . -f 1); \
		MINOR_VERSION=$$(echo $(VERSION) | cut -d . -f 2); \
		MICRO_VERSION=$$(echo $(VERSION) | cut -d . -f 3); \
		cd $(REPO_DIR); \
		sed -i -r 's/(Version:[ \t]+)[0-9]+\.[0-9]+\.[0-9]+$$/\1$(VERSION)/' slurm.spec META; \
		sed -i -r 's/(Major:[ \t]+)[0-9]+$$/\1'"$${MAJOR_VERSION}"'/' META; \
		sed -i -r 's/(Minor:[ \t]+)[0-9]+$$/\1'"$${MINOR_VERSION}"'/' META; \
		sed -i -r 's/(Micro:[ \t]+)[0-9]+$$/\1'"$${MICRO_VERSION}"'/' META; \
		sed -i -r 's/VERSION_PLACE_HOLDER/$(VERSION)/' debian/changelog; \
	fi
endef
