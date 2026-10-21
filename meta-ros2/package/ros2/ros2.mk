################################################################################
#
# ros2
#
################################################################################

ROS2_VERSION = jazzy
ROS2_SITE = $(BR2_EXTERNAL_ROS2_PATH)/src
ROS2_SITE_METHOD = local
ROS2_LICENSE = Apache-2.0, EPL-2.0 (cyclonedds)
ROS2_DEPENDENCIES = libyaml spdlog

ROS2_PREFIX = /opt/ros/$(ROS2_VERSION)
ROS2_VENV = $(HOST_DIR)/ros2-venv

# colcon and the rosidl code generators run on the build host
define ROS2_HOST_TOOLS
	/usr/bin/python3 -m venv $(ROS2_VENV)
	$(ROS2_VENV)/bin/pip install -q -r $(BR2_EXTERNAL_ROS2_PATH)/host-requirements.txt
endef
ROS2_PRE_CONFIGURE_HOOKS += ROS2_HOST_TOOLS

# Each ROS package builds against the ones before it, so colcon installs
# straight into staging. Cyclone options: no host-only tools, no iceoryx/SSL.
define ROS2_BUILD_CMDS
	cd $(@D) && $(TARGET_MAKE_ENV) RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
		$(ROS2_VENV)/bin/colcon --log-base log build \
		--merge-install --install-base $(STAGING_DIR)$(ROS2_PREFIX) \
		--packages-up-to rmw_cyclonedds_cpp miltech_counter \
		--event-handlers console_cohesion+ \
		--cmake-args \
			-DCMAKE_TOOLCHAIN_FILE=$(HOST_DIR)/share/buildroot/toolchainfile.cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_TESTING=OFF \
			-DPython3_EXECUTABLE=$(ROS2_VENV)/bin/python3 \
			-DBUILD_IDLC=OFF -DBUILD_DDSPERF=OFF -DENABLE_SHM=OFF -DENABLE_SSL=OFF
endef

# runtime files only: no headers, CMake files or Python modules
define ROS2_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)$(ROS2_PREFIX)
	rsync -a --exclude include --exclude cmake --exclude 'python3*' \
		$(STAGING_DIR)$(ROS2_PREFIX)/ $(TARGET_DIR)$(ROS2_PREFIX)/
	$(INSTALL) -D -m 0644 $(ROS2_PKGDIR)/ros2.sh $(TARGET_DIR)/etc/profile.d/ros2.sh
endef

$(eval $(generic-package))
