zephyr_library()

zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

zephyr_library_sources_ifdef(CONFIG_TENTALICE_RUNTIME_INPUT
  src/behavior_tentalice.c
  src/input_processor_tentalice.c
)
