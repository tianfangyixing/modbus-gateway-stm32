"""Compile the production capacity guard, including deliberate bad configurations."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    app = directory.parents[1]
    includes = [directory / "include", app / "LWIP/Target", app / "Configuration/include",
                app / "MQTT/include", app / "SNTP/include", app / "Middlewares/Third_Party/LwIP/src/include"]
    cases = [
        ("production", "", None),
        ("old_512", "#undef MQTT_OUTPUT_RINGBUF_SIZE\n#define MQTT_OUTPUT_RINGBUF_SIZE 512", "mqtt_capacity_connect_fits"),
        ("small_1024", "#undef MQTT_OUTPUT_RINGBUF_SIZE\n#define MQTT_OUTPUT_RINGBUF_SIZE 1024", "mqtt_capacity_connect_fits"),
        ("full_ring_1047", "#undef MQTT_OUTPUT_RINGBUF_SIZE\n#define MQTT_OUTPUT_RINGBUF_SIZE 1047", "mqtt_capacity_connect_fits"),
        ("non_power_two", "#undef MQTT_OUTPUT_RINGBUF_SIZE\n#define MQTT_OUTPUT_RINGBUF_SIZE 2049", "mqtt_capacity_ring_power_of_two"),
        ("dns_without_nul", "#undef DNS_MAX_NAME_LENGTH\n#define DNS_MAX_NAME_LENGTH 253", "mqtt_capacity_dns_hostname"),
        ("will_u8_overflow", "#undef CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH\n#define CONFIGURATION_MESSAGE_PAYLOAD_MAX_LENGTH 256", "mqtt_capacity_will_payload"),
    ]
    with tempfile.TemporaryDirectory(prefix="mqtt-capacity-") as temporary:
        source = Path(temporary) / "capacity.c"
        for name, override, diagnostic in cases:
            source.write_text('#include "configuration.h"\n#include "lwip/opt.h"\n' + override + '\n#include "mqtt_capacity.h"\n', encoding="utf-8")
            command = [args.compiler, "-std=c99", "-fsyntax-only", *[f"-I{path}" for path in includes], str(source)]
            result = subprocess.run(command, text=True, capture_output=True)
            if diagnostic is None:
                if result.returncode:
                    raise RuntimeError(result.stderr)
            elif result.returncode == 0 or diagnostic not in result.stderr:
                raise RuntimeError(f"Expected {diagnostic} rejection for {name}: {result.stderr}")
            print(f"PASS {name}")


if __name__ == "__main__":
    main()
