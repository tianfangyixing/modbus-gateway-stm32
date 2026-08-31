#include "unity.h"

#include "modbus_collector_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_SLAVE_ADDRESS UINT8_C(17)
#define TEST_ADDRESS UINT16_C(0x1234)

static uint16_t test_crc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = UINT16_C(0xFFFF);
    uint16_t byte_index;

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        uint8_t bit_index;

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < UINT8_C(8); bit_index++)
        {
            if ((crc & UINT16_C(1)) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ UINT16_C(0xA001));
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static void append_crc(modbus_rtu_adu_t *adu)
{
    uint16_t crc = test_crc(adu->data, (uint16_t)(adu->length - UINT16_C(2)));

    adu->data[adu->length - UINT16_C(2)] = (uint8_t)crc;
    adu->data[adu->length - UINT16_C(1)] = (uint8_t)(crc >> 8U);
}

static configuration_collection_point_t make_point(uint8_t source, uint8_t data_type)
{
    configuration_collection_point_t point = {0};

    point.slave_address = TEST_SLAVE_ADDRESS;
    point.source = source;
    point.address = TEST_ADDRESS;
    point.data_type = data_type;
    return point;
}

static modbus_rtu_adu_t make_bit_response(uint8_t slave_address, uint8_t function, uint8_t value)
{
    modbus_rtu_adu_t response = {0};

    response.length = UINT16_C(6);
    response.data[0] = slave_address;
    response.data[1] = function;
    response.data[2] = UINT8_C(1);
    response.data[3] = value;
    append_crc(&response);
    return response;
}

static modbus_rtu_adu_t make_register_response(uint8_t slave_address, uint8_t function, uint16_t value)
{
    modbus_rtu_adu_t response = {0};

    response.length = UINT16_C(7);
    response.data[0] = slave_address;
    response.data[1] = function;
    response.data[2] = UINT8_C(2);
    response.data[3] = (uint8_t)(value >> 8U);
    response.data[4] = (uint8_t)value;
    append_crc(&response);
    return response;
}

static void assert_payload(const configuration_collection_point_t *point, const modbus_rtu_adu_t *response,
                           const char *expected)
{
    char payload[MODBUS_COLLECTOR_PAYLOAD_CAPACITY];
    uint16_t payload_length = UINT16_MAX;
    modbus_collector_codec_result_t result;

    memset(payload, 0x5A, sizeof(payload));
    result = modbus_collector_codec_decode_response(point, response, payload, sizeof(payload), &payload_length);

    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_OK, result);
    TEST_ASSERT_EQUAL_UINT16(strlen(expected), payload_length);
    TEST_ASSERT_EQUAL_STRING(expected, payload);
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_encode_maps_all_sources_to_quantity_one_reads(void)
{
    static const uint8_t sources[] =
    {
        CONFIGURATION_COLLECTION_SOURCE_COIL,
        CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT,
        CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER,
        CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER
    };
    static const uint8_t functions[] =
    {
        UINT8_C(0x01), UINT8_C(0x02), UINT8_C(0x03), UINT8_C(0x04)
    };
    size_t index;

    for (index = 0U; index < sizeof(sources); index++)
    {
        configuration_collection_point_t point = make_point(sources[index], CONFIGURATION_DATA_TYPE_UINT16);
        modbus_rtu_adu_t request;
        uint16_t expected_crc;

        memset(&request, 0xA5, sizeof(request));
        TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_OK, modbus_collector_codec_encode_request(&point, &request));
        TEST_ASSERT_EQUAL_UINT16(8U, request.length);
        TEST_ASSERT_EQUAL_UINT8(TEST_SLAVE_ADDRESS, request.data[0]);
        TEST_ASSERT_EQUAL_UINT8(functions[index], request.data[1]);
        TEST_ASSERT_EQUAL_UINT8(0x12U, request.data[2]);
        TEST_ASSERT_EQUAL_UINT8(0x34U, request.data[3]);
        TEST_ASSERT_EQUAL_UINT8(0U, request.data[4]);
        TEST_ASSERT_EQUAL_UINT8(1U, request.data[5]);

        expected_crc = test_crc(request.data, UINT16_C(6));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)expected_crc, request.data[6]);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(expected_crc >> 8U), request.data[7]);
    }
}

static void test_encode_failure_preserves_request(void)
{
    configuration_collection_point_t point = make_point(UINT8_C(0xFF), CONFIGURATION_DATA_TYPE_UINT16);
    modbus_rtu_adu_t request;
    modbus_rtu_adu_t original;

    memset(&request, 0x3C, sizeof(request));
    original = request;
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_SOURCE_INVALID,
                      modbus_collector_codec_encode_request(&point, &request));
    TEST_ASSERT_EQUAL_MEMORY(&original, &request, sizeof(request));
}

static void test_decode_bit_values_for_coil_and_discrete_input(void)
{
    configuration_collection_point_t coil = make_point(CONFIGURATION_COLLECTION_SOURCE_COIL, UINT8_C(0xFF));
    configuration_collection_point_t discrete =
        make_point(CONFIGURATION_COLLECTION_SOURCE_DISCRETE_INPUT, UINT8_C(0xFF));
    modbus_rtu_adu_t coil_false = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x01), UINT8_C(0));
    modbus_rtu_adu_t coil_true = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x01), UINT8_C(1));
    modbus_rtu_adu_t discrete_true = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x02), UINT8_C(1));

    assert_payload(&coil, &coil_false, "0");
    assert_payload(&coil, &coil_true, "1");
    assert_payload(&discrete, &discrete_true, "1");
}

static void test_decode_uint16_boundaries_for_both_register_sources(void)
{
    configuration_collection_point_t holding =
        make_point(CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER, CONFIGURATION_DATA_TYPE_UINT16);
    configuration_collection_point_t input =
        make_point(CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER, CONFIGURATION_DATA_TYPE_UINT16);
    modbus_rtu_adu_t zero = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x03), UINT16_C(0));
    modbus_rtu_adu_t maximum = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x03), UINT16_MAX);
    modbus_rtu_adu_t input_value = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x04), UINT16_C(42));

    assert_payload(&holding, &zero, "0");
    assert_payload(&holding, &maximum, "65535");
    assert_payload(&input, &input_value, "42");
}

static void test_decode_int16_required_values(void)
{
    static const uint16_t values[] =
    {
        UINT16_C(0x0000), UINT16_C(0x0001), UINT16_C(0x7FFF), UINT16_C(0x8000), UINT16_C(0xFFFF)
    };
    static const char *expected[] =
    {
        "0", "1", "32767", "-32768", "-1"
    };
    configuration_collection_point_t point =
        make_point(CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER, CONFIGURATION_DATA_TYPE_INT16);
    size_t index;

    for (index = 0U; index < sizeof(values) / sizeof(values[0]); index++)
    {
        modbus_rtu_adu_t response = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x03), values[index]);

        assert_payload(&point, &response, expected[index]);
    }
}

static void test_decode_rejects_wrong_function_length_crc_and_slave(void)
{
    configuration_collection_point_t point =
        make_point(CONFIGURATION_COLLECTION_SOURCE_COIL, CONFIGURATION_DATA_TYPE_UINT16);
    modbus_rtu_adu_t wrong_function = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x02), UINT8_C(1));
    modbus_rtu_adu_t wrong_length = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x01), UINT8_C(1));
    modbus_rtu_adu_t wrong_crc = make_bit_response(TEST_SLAVE_ADDRESS, UINT8_C(0x01), UINT8_C(1));
    modbus_rtu_adu_t wrong_slave = make_bit_response(UINT8_C(18), UINT8_C(0x01), UINT8_C(1));
    char payload[MODBUS_COLLECTOR_PAYLOAD_CAPACITY] = "keep";
    uint16_t payload_length = UINT16_C(4);

    wrong_length.length = UINT16_C(5);
    wrong_crc.data[wrong_crc.length - UINT16_C(1)] ^= UINT8_C(1);

    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_RTU_ERROR,
                      modbus_collector_codec_decode_response(&point, &wrong_function, payload, sizeof(payload),
                                                             &payload_length));
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_RTU_ERROR,
                      modbus_collector_codec_decode_response(&point, &wrong_length, payload, sizeof(payload),
                                                             &payload_length));
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_RTU_ERROR,
                      modbus_collector_codec_decode_response(&point, &wrong_crc, payload, sizeof(payload),
                                                             &payload_length));
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_SLAVE_MISMATCH,
                      modbus_collector_codec_decode_response(&point, &wrong_slave, payload, sizeof(payload),
                                                             &payload_length));
    TEST_ASSERT_EQUAL_STRING("keep", payload);
    TEST_ASSERT_EQUAL_UINT16(4U, payload_length);
}

static void test_decode_capacity_failure_preserves_outputs_and_inputs(void)
{
    configuration_collection_point_t point =
        make_point(CONFIGURATION_COLLECTION_SOURCE_INPUT_REGISTER, CONFIGURATION_DATA_TYPE_INT16);
    configuration_collection_point_t original_point = point;
    modbus_rtu_adu_t response = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x04), UINT16_C(0x8000));
    modbus_rtu_adu_t original_response = response;
    char payload[MODBUS_COLLECTOR_PAYLOAD_CAPACITY] = "stable";
    uint16_t payload_length = UINT16_C(6);

    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_PAYLOAD_CAPACITY_INSUFFICIENT,
                      modbus_collector_codec_decode_response(&point, &response, payload, UINT16_C(6),
                                                             &payload_length));
    TEST_ASSERT_EQUAL_STRING("stable", payload);
    TEST_ASSERT_EQUAL_UINT16(6U, payload_length);
    TEST_ASSERT_EQUAL_MEMORY(&original_point, &point, sizeof(point));
    TEST_ASSERT_EQUAL_MEMORY(&original_response, &response, sizeof(response));
}

static void test_decode_is_deterministic_and_rejects_invalid_data_type(void)
{
    configuration_collection_point_t point =
        make_point(CONFIGURATION_COLLECTION_SOURCE_HOLDING_REGISTER, CONFIGURATION_DATA_TYPE_UINT16);
    modbus_rtu_adu_t response = make_register_response(TEST_SLAVE_ADDRESS, UINT8_C(0x03), UINT16_C(12345));
    char first[MODBUS_COLLECTOR_PAYLOAD_CAPACITY];
    char second[MODBUS_COLLECTOR_PAYLOAD_CAPACITY];
    uint16_t first_length;
    uint16_t second_length;

    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_OK,
                      modbus_collector_codec_decode_response(&point, &response, first, sizeof(first), &first_length));
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_OK,
                      modbus_collector_codec_decode_response(&point, &response, second, sizeof(second),
                                                             &second_length));
    TEST_ASSERT_EQUAL_UINT16(first_length, second_length);
    TEST_ASSERT_EQUAL_STRING(first, second);

    point.data_type = UINT8_C(0xFF);
    TEST_ASSERT_EQUAL(MODBUS_COLLECTOR_CODEC_DATA_TYPE_INVALID,
                      modbus_collector_codec_decode_response(&point, &response, second, sizeof(second),
                                                             &second_length));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_maps_all_sources_to_quantity_one_reads);
    RUN_TEST(test_encode_failure_preserves_request);
    RUN_TEST(test_decode_bit_values_for_coil_and_discrete_input);
    RUN_TEST(test_decode_uint16_boundaries_for_both_register_sources);
    RUN_TEST(test_decode_int16_required_values);
    RUN_TEST(test_decode_rejects_wrong_function_length_crc_and_slave);
    RUN_TEST(test_decode_capacity_failure_preserves_outputs_and_inputs);
    RUN_TEST(test_decode_is_deterministic_and_rejects_invalid_data_type);
    return UNITY_END();
}
