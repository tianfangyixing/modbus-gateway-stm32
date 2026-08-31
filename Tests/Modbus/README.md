# Modbus Collector codec host tests

The `modbus_collector_codec_tests` target is a black-box check of
`Spec/modbus/modbus_collector.md` sections 3 and 6.

| Contract | Coverage |
| --- | --- |
| FC01/FC02/FC03/FC04 request mapping, configured address, quantity 1, valid CRC | `test_encode_maps_all_sources_to_quantity_one_reads` |
| Coil and Discrete Input payloads are exactly `0` or `1` | `test_decode_bit_values_for_coil_and_discrete_input` |
| UINT16 decimal boundaries and both register sources | `test_decode_uint16_boundaries_for_both_register_sources` |
| INT16 two's-complement values `0`, `1`, `32767`, `-32768`, `-1` | `test_decode_int16_required_values` |
| Wrong function, length, CRC, or slave is rejected without publishing data | `test_decode_rejects_wrong_function_length_crc_and_slave` |
| Insufficient output capacity leaves outputs and inputs unchanged | `test_decode_capacity_failure_preserves_outputs_and_inputs` |
| Failure preserves the request; successful decoding is deterministic | `test_encode_failure_preserves_request`, `test_decode_is_deterministic_and_rejects_invalid_data_type` |

The test target links `modbus_rtu_rs485_port_test_adapter.c` only to isolate the host executable from STM32 UART and
RS485 hardware. The codec itself reuses the production RTU encode/decode APIs.
