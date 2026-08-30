# Configuration API 黑盒测试追踪

规范来源：`Spec/configuration/configuration.md`。测试不以实现代码作为判据。

## `configuration_set_defaults()`

| 规范条款 | 测试 |
| --- | --- |
| `NULL` 安全返回 | `test_null_is_safe` |
| 精确默认值、所有其他公开字段和数组尾部清零 | `test_sets_every_documented_default_and_clears_all_other_public_fields` |
| 结果不受原内容影响 | `test_result_is_independent_of_original_contents` |
| 连续调用幂等 | `test_call_is_idempotent`、固定种子 defaults 属性组 |
| 不写目标对象之外的内存 | `test_does_not_write_outside_target_object` |
| 默认配置通过校验 | `test_default_configuration_is_valid` |

默认值断言逐个比较公开字段，不断言 C 结构体 padding。

## `configuration_validate()`

`assert_validation_result()` 在每次确定性校验前后逐字节比较同一输入对象，因此下表所有测试同时覆盖
“不得修改输入”。每个无效用例从有效基线只破坏对应规则，不依赖多错误时的内部优先级。

| 规范区域 | 测试组 |
| --- | --- |
| `NULL` 与有效基线 | `test_null_returns_invalid_argument`、`test_valid_default_and_static_configurations_are_accepted` |
| Network mode、DHCP 不生效字段 | `test_network_mode_and_dhcp_inactive_fields` |
| 子网掩码 `/1～/30` 与非连续掩码 | `test_static_subnet_mask_boundaries` |
| 静态 IP 与 gateway 单播、host、同网段、不同地址 | `test_static_ip_address_rules`、`test_static_gateway_rules` |
| DNS 必填、单播、不同地址、可跨网段 | `test_static_dns_rules` |
| RTU 波特率、帧格式、超时边界 | `test_all_documented_rtu_baud_rates_are_accepted`、`test_undocumented_rtu_baud_rates_are_rejected`、`test_rtu_frame_format_and_timeout_boundaries` |
| Modbus TCP 端口 | `test_modbus_tcp_port_boundaries` |
| SNTP 类型与 IPv4 | `test_sntp_endpoint_types_and_valid_ipv4_addresses`、`test_sntp_ipv4_restrictions` |
| hostname 长度、标签、字符、NUL、尾部 | `test_sntp_hostname_length_and_label_boundaries`、`test_sntp_hostname_syntax_and_byte_representation` |
| SNTP 地址去重与类型差异 | `test_sntp_duplicate_and_mixed_address_rules` |
| MQTT mode 与禁用字段 | `test_mqtt_mode_and_disabled_inactive_fields` |
| broker hostname 与端口 | `test_mqtt_enabled_baseline_broker_and_port_rules` |
| client ID mode、长度、字符、NUL、尾部 | `test_mqtt_client_id_modes_boundaries_and_characters` |
| 用户名和密码长度、可打印 ASCII、NUL、尾部 | `test_mqtt_username_boundaries_and_characters`、`test_mqtt_password_boundaries_and_characters` |
| keep-alive | `test_mqtt_keep_alive_boundaries` |
| 禁用消息字段与非法 mode | `test_disabled_mqtt_messages_ignore_their_other_fields` |
| CUSTOM topic 的全部边界和字符规则 | `test_custom_mqtt_topic_boundaries_and_allowed_forms`、`test_custom_mqtt_topic_rejects_invalid_bytes_and_honors_sentinel` |
| payload 长度、合法控制字符、UTF-8、NUL、尾部 | `test_custom_mqtt_payload_boundaries_and_valid_utf8`、`test_custom_mqtt_payload_rejects_invalid_utf8_and_honors_sentinel` |
| 消息 QoS、retain、相同 topic | `test_custom_mqtt_qos_retain_and_message_topic_relationships` |
| 有效根、无 Key Usage、有效期忽略、RSA/ECDSA | `test_certificate_accepts_all_documented_valid_forms` |
| PEM/DER/bundle 表示 | `test_certificate_rejects_invalid_encodings_and_multiple_certificates` |
| CA、自签名、Key Usage、签名和算法 | `test_certificate_rejects_non_root_and_unsupported_certificates` |
| 证书长度、NUL、尾部 | `test_certificate_byte_field_boundaries_and_inactive_tail` |
| mbedTLS 临时内存不足 | `test_certificate_allocation_failure_is_resource_unavailable` |
| MQTT mode 与 point count | `test_collection_mode_and_point_count_boundaries` |
| 未使用采集点 | `test_collection_ignores_unused_points` |
| slave、地址、source、data type | `test_collection_slave_address_and_register_address_boundaries`、`test_collection_source_and_data_type_rules` |
| poll interval 与 first-byte timeout | `test_collection_poll_and_timeout_boundaries_are_independent` |
| 采集 topic 与 QoS | `test_collection_topic_boundaries_and_characters`、`test_collection_qos_boundaries` |
| 重复地址允许、topic 唯一性 | `test_collection_allows_duplicate_addresses_with_unique_topics`、`test_collection_topic_uniqueness_is_case_sensitive` |
| 与上线/遗嘱消息 topic 冲突 | `test_collection_topic_conflicts_with_only_enabled_messages` |
| 总线利用率等于/超过 50%、帧位数和响应长度 | `test_bus_utilization_accepts_exactly_half_and_rejects_more`、`test_bus_utilization_accounts_for_frame_format_and_response_size` |
| `19200` 阈值及以上、最大点数 | `test_high_baud_rate_utilization_is_within_limit_for_maximum_points` |
| 任意完整对象安全返回、结果枚举、确定性 | 两个固定种子 validate 属性组 |

## `configuration_equals()`

`assert_equals_result()` 在每次比较前确认两侧均通过校验，双向调用以检查对称性，并逐字节确认输入未被
修改。没有针对 `NULL` 或无效配置的用例，因为规范明确将其定义为前置条件违例和未定义行为。

| 规范条款 | 测试组 |
| --- | --- |
| 自反、对称、传递 | `test_relation_is_reflexive_symmetric_and_transitive`、固定种子 equality 属性组 |
| padding 与默认配置所有不生效字段 | `test_padding_and_all_inactive_default_fields_are_ignored` |
| DHCP/STATIC 网络语义 | `test_network_mode_is_significant_but_dhcp_static_fields_are_ignored`、`test_each_static_network_field_is_significant` |
| RTU 与 TCP 精确比较 | `test_rtu_and_modbus_tcp_fields_are_significant` |
| SNTP hostname 大小写、类型、IPv4、位置 | 三个 `test_sntp_*` comparison 测试组 |
| SNTP union 非当前存储与 hostname 尾部 | `test_sntp_inactive_union_storage_and_hostname_tail_are_ignored` |
| MQTT disabled 与 mode | `test_mqtt_mode_is_significant_but_disabled_fields_are_ignored` |
| broker 大小写和端口 | `test_mqtt_broker_comparison_and_port` |
| client ID mode 与派生模式不生效字段 | `test_mqtt_client_id_mode_and_effective_value` |
| 用户名、密码、PEM 精确字节和 keep-alive | `test_mqtt_authentication_certificate_and_keep_alive_are_exact` |
| 可变字段有效内容后的尾部 | `test_mqtt_variable_field_tails_are_ignored` |
| MQTT 消息 mode、字段和角色 | 三个 message comparison 测试组 |
| point count 与未使用点 | `test_collection_point_count_is_significant_and_unused_points_are_ignored` |
| 采集点无序比较 | `test_collection_order_is_ignored` |
| 采集点生效字段 | `test_each_effective_collection_point_field_is_significant` |
| Coil/Discrete Input data type 不生效 | `test_coil_and_discrete_input_data_type_is_ignored` |

## Configuration Binary Codec

规范来源：`Spec/configuration/configuration_binary.md`。最短 21 字节与默认 48 字节 payload 使用独立手写
golden vector；复杂 payload 由测试侧仅提供基本字节写入操作的 wire composer 按规范组装，不读取
`configuration_t`，也不调用生产 encoder 生成 decoder 输入。

所有 decode 调用均检查 payload 不变和输出对象前后 canary；成功结果逐个比较公开字段、文本 NUL 与数组
尾部，不比较结构体 padding。所有 encode 调用均先确认模型前置条件成立，再检查输入对象不变和容量外
canary。失败用例不读取规范未定义的 configuration、payload 或 `payload_length` 内容。

| 规范区域 | 测试组 |
| --- | --- |
| 最短、默认、完整条件分支与派生/禁用分支的规范编码 | 四个 `test_decode_accepts_*golden_vector` 与四个 `test_encode_matches_*golden_vector` |
| 生效文本的可构造最小/最大模型有效边界 | `test_decode_accepts_effective_text_length_boundaries`、`test_encode_matches_effective_text_length_boundaries` |
| decode 的 `NULL`、长度 0、21、7826、7827 与 schema 版本 | `test_decode_rejects_each_null_argument`、`test_decode_enforces_payload_length_boundaries`、`test_decode_rejects_unsupported_schema` |
| 每个 golden 的所有非零真前缀、尾随字节 | `test_decode_rejects_every_truncated_golden_prefix`、`test_decode_rejects_trailing_bytes` |
| 所有布局判别值非法与 point count 超限 | `test_decode_rejects_illegal_layout_discriminants`、`test_decode_rejects_point_count_above_schema_limit` |
| 每类 wire text 的最小/最大结构边界 | `test_decode_rejects_out_of_range_text_lengths` |
| 不改变布局的非法 baud、frame、data type、QoS、retain 及 wire-valid 空 CUSTOM payload | `test_decode_maps_non_layout_values_to_model_invalid` |
| Network、RTU、TCP、SNTP、MQTT、Certificate、Collection、总线利用率错误映射 | `test_decode_maps_each_model_validation_category_to_model_invalid` |
| 证书校验资源不足映射 | `test_decode_maps_allocator_failure_to_resource_unavailable` |
| decode 输出与调用前内容无关 | `test_decode_result_is_independent_of_output_contents` |
| encode 的三个 `NULL` 参数与容量 0、少一、恰好、加一、大于 schema 最大值 | `test_encode_rejects_each_null_argument`、`test_encode_enforces_payload_capacity_boundaries` |
| 不生效字段省略、相同生效字段的唯一稳定编码 | `test_encode_ignores_inactive_fields_and_is_deterministic` |

本主机单元测试不覆盖 ISR、并发/重入时序、固件硬件副作用，也不调用 encode 处理模型无效对象或构造重叠
内存区域；这些行为分别不适合主机同步单元测试或属于 API 前置条件之外。测试不要求模型有效配置必须能够
编码到理论最大 7826 字节；7826 边界由结构完整且仅证书模型无效的 decode payload 覆盖。

## Configuration Service

规范来源：`Spec/configuration/configuration_service.md`。测试只通过 service interface 观察 active 与跨重启结果；
内存 Flash adapter 仅负责提供可替换的本地 I/O 和故障注入。

| 规范区域 | 测试组 |
| --- | --- |
| 初始化前、空槽 defaults、重复初始化 | `test_active_is_null_before_init_and_defaults_after_empty_init` |
| 单槽、generation 新旧与回绕 | `test_init_loads_the_only_valid_slot`、`test_init_selects_newest_generation_and_handles_wrap` |
| 新槽无效回退、回退后写入、旧格式与损坏拒绝 | `test_init_falls_back_from_newer_invalid_record`、`test_write_replaces_newer_invalid_slot_from_selected_generation`、`test_init_rejects_old_format_and_corruption` |
| generation 相同、半圈歧义初始化失败及仅一个配置有效 | 两个 `test_init_*ambiguous*` 测试组 |
| 全部读取阶段及 codec 资源故障初始化失败 | `test_init_reports_storage_error_for_read_failures`、`test_init_reports_codec_resource_failure` |
| write 生命周期和参数错误 | `test_write_rejects_lifecycle_and_argument_errors` |
| payload/header/magic 顺序、active 不变、重启生效 | `test_write_commits_in_order_without_changing_active` |
| 相同 payload 每次提交并交替槽 | `test_repeated_identical_writes_always_commit_and_alternate` |
| write 拒绝不支持的 schema、畸形结构和无效模型，且不访问 Flash | `test_write_rejects_invalid_payload_without_storage_side_effects` |
| write 解码资源不足、不访问 Flash 及恢复后重试 | `test_write_reports_codec_resource_failure_without_storage_side_effects` |
| 空 Flash 初始化状态直接驱动首次写入且不重扫 | `test_write_uses_empty_init_state_without_rescanning` |
| 擦除、读取、三阶段编程、回读校验故障及同运行期重试 | 三个 `test_write_*preserve*` 测试组、`test_failed_write_retries_same_slot_and_generation_without_reboot` |
| magic 已提交但最终 read 失败 | `test_final_read_error_keeps_old_slot_even_if_new_record_committed` |
