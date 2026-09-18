idf_component_register(
    SRCS "utils/crc/custom_crc16.c"
         "utils/crc/custom_crc32.c"
         "protocol/dji_protocol_parser.c"
         "protocol/dji_protocol_data_processor.c"
         "protocol/dji_protocol_data_descriptors.c"
         "protocol/dji_protocol_data_structures.c"
         "ble/ble.c"
         "data/data.c"
         "logic/connect_logic.c"
         "logic/command_logic.c"
         "logic/status_logic.c"
         "logic/enums_logic.c"
    INCLUDE_DIRS "utils/crc" "protocol" "ble" "data" "logic"
    REQUIRES bt nvs_flash
)

