IS_CONTROLLER ?= true
TEST_CONTAINER_IMAGE ?= artifactory-kfs.habana-labs.com/docker-developers/users/qauser/docker_qa/ubuntu22.04/pytorch_2.2.0_gaudi/1.16.0:31_hccl_e2586c4d2a05
MAKEFILE_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
REPO_DIR := $(shell dirname $(MAKEFILE_DIR))
SHELL := /bin/bash

habana-configure:
	# Habana driver and runtime
	if ! hl-smi -Q name --format=csv || ! hl-smi -Q name --format=csv | grep -v name | grep -qv N/A; then \
		wget -nv https://vault.habana.ai/artifactory/gaudi-installer/latest/habanalabs-installer.sh && \
		chmod +x habanalabs-installer.sh && \
		yes | ./habanalabs-installer.sh install --type base || \
		hl-smi -Q name --format=csv; \
	fi

	if ! grep -q habana /etc/docker/daemon.json; then \
		sudo apt install -y habanalabs-container-runtime && \
		sudo cp docker-daemon.json /etc/docker/daemon.json && \
		sudo systemctl restart docker.service; \
	fi

	# Habana external ports
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
	sudo apt install munge libmunge-dev libglib2.0-dev libdbus-1-dev -y
	cd $(REPO_DIR); \
	autoreconf; \
	./configure --prefix=/usr/local/slurm --sysconfdir=/etc/slurm --with-systemdsystemunitdir=/lib/systemd/system; \
	make

install-source: configure
	cd $(REPO_DIR); \
	sudo make install
ifeq ($(IS_CONTROLLER), true)
	sudo systemctl enable --now slurmctld.service
endif
	sudo systemctl enable --now slurmd.service

build-and-install-debian-package: build-debian-package install-debian-package

build-debian-package:
	sudo apt install build-essential fakeroot devscripts equivs libglib2.0-dev libdbus-1-dev libmunge-dev libgtk2.0-dev libpam0g-dev libperl-dev liblua5.3-dev dh-exec librrd-dev libipmimonitoring-dev hdf5-helpers libfreeipmi-dev libhdf5-dev man2html-base libcurl4-openssl-dev libhttp-parser-dev libyaml-dev libjson-c-dev libjwt-dev liblz4-dev librdkafka-dev -y
	cd $(REPO_DIR); \
	mkdir debian-packages/; \
	autoreconf; \
	yes | mk-build-deps -i debian/controll; \
	debuild -b -uc -us; \
	mv ../slurm-smd*.deb debian-packages

install-debian-package: configure
	cd $(REPO_DIR)/debian-packages; \
	sudo apt install ./slurm-smd_*.deb ./slurm-smd-slurmd_*.deb ./slurm-smd-client_*.deb -y
ifeq ($(IS_CONTROLLER), true)
	sudo apt install $(REPO_DIR)/debian-packages/slurm-smd-slurmctld_*.deb -y
endif

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
	VM=$$($(call hlctl-create)); \
	INSTALLATION_TYPE=$$(test -d $(REPO_DIR)/debian-packages && echo debian-package || echo source); \
	until timeout 10s ssh -o StrictHostKeyChecking=no -o BatchMode=yes $${VM} exit 0; do sleep 5s; done; \
	ssh $${VM} "mkdir -p $(REPO_DIR)"; \
	scp -rp $(REPO_DIR)/* $${VM}:$(REPO_DIR); \
	ssh $${VM} "cd $(MAKEFILE_DIR); make habana-configure install-$$INSTALLATION_TYPE $(filter-out --,$(MAKEFLAGS)) IS_CONTROLLER='false'"; \
	ssh $${VM} "grep NodeName /etc/slurm/slurm.conf" | sudo tee -a /etc/slurm/slurm.conf; \
	sudo cp -p /etc/slurm/slurm.conf /etc/munge/munge.key /tmp; \
	sudo chown $$(id -u) /tmp/slurm.conf /tmp/munge.key; \
	scp -p /tmp/slurm.conf /tmp/munge.key $${VM}:/tmp; \
	ssh $${VM} "sudo mv /tmp/slurm.conf /etc/slurm/slurm.conf"; \
	ssh $${VM} "sudo mv /tmp/munge.key /etc/munge/munge.key; sudo chown munge:munge /etc/munge/munge.key"; \
	ssh $${VM} "sudo systemctl restart munge slurmd"; \
	sudo scontrol reconfigure
	export NODE_0_IPADDRESS=$$(srun hostname -i); \
	srun --gres=gpu:8 --nodes=2 --ntasks-per-node=1 --export ALL bash -c 'docker run --init --rm --runtime=habana -e HABANA_VISIBLE_DEVICES -e SLURM_NODEID -e SLURM_LAUNCH_NODE_IPADDR --net=host $(TEST_CONTAINER_IMAGE) /bin/bash -c "cd hccl_demo; HCCL_COMM_ID=$$NODE_0_IPADDRESS:5555 python3 run_hccl_demo.py --nranks 16 --node_id $$SLURM_NODEID --size 32m --test all_reduce --loop 1000 --ranks_per_node 8"'; \
	$(call prompt-status)

create-vm:
	$(call hlctl-create)

define hlctl-create
	[ -d ~/.ssh ] || mkdir ~/.ssh; \
	[ $$(stat -c '%a' ~/.ssh) -eq 700 ] || chown 700 ~/.ssh; \
	[ -f ~/.ssh/authorized_keys ] || touch ~/.ssh/authorized_keys; \
	[ $$(stat -c '%a' ~/.ssh/authorized_keys) -eq 600 ] || chown 600 ~/.ssh/authorized_keys; \
	[ -f ~/.ssh/id_rsa.pub ] || ssh-keygen -t rsa -N "" -f ~/.ssh/authorized_keys/id_rsa; \
	grep -q ~/.ssh/id_rsa.pub ~/.ssh/authorized_keys || cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys; \
	/home/mwalter/gerrit/hlctl/dist/hlctl create vm --namespace devops --flavor g1.l --image u22 --disable-autofs=false \
		| awk '$$1=="VM:" {print $$2}' | grep '.'
endef

define prompt-status
	if [ $$? -eq 0 ]; then \
		echo "SUCCESS: $@"; \
	else \
		echo "FAILED: $@"; \
		false; \
	fi
endef