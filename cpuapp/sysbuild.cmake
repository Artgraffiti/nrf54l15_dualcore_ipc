if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
	message(FATAL_ERROR "REMOTE_BOARD must be set to a valid board name")
endif()

message(STATUS "Building CPUFLPR image for ${SB_CONFIG_REMOTE_BOARD}")

ExternalZephyrProject_Add(
	APPLICATION cpuflpr
	SOURCE_DIR ${APP_DIR}/../cpuflpr
	BOARD ${SB_CONFIG_REMOTE_BOARD}
	BOARD_REVISION ${BOARD_REVISION}
)

set_property(GLOBAL APPEND PROPERTY PM_DOMAINS CPUFLPR)
set_property(GLOBAL APPEND PROPERTY PM_CPUFLPR_IMAGES cpuflpr)
set_property(GLOBAL PROPERTY DOMAIN_APP_CPUFLPR cpuflpr)
set(CPUFLPR_PM_DOMAIN_DYNAMIC_PARTITION cpuflpr CACHE INTERNAL "")

sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} cpuflpr)
sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} cpuflpr)
