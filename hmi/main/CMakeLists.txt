idf_component_register(SRCS "bist_hmi.c"
                       INCLUDE_DIRS "."
                       REQUIRES m5stack_tab5 modbus_bist_master)
