# Client integration

Include `moonmic.h`, populate `moonmic_config_t`, and keep the returned client
alive while streaming.

```cpp
#include "moonmic.h"

moonmic_config_t config = {};
config.host_ip = "192.168.1.100";
config.port = MOONMIC_DEFAULT_PORT;
config.sample_rate = MOONMIC_DEFAULT_SAMPLE_RATE;
config.channels = MOONMIC_DEFAULT_CHANNELS;
config.bitrate = MOONMIC_DEFAULT_BITRATE;
config.gain = 1.0f;
config.auto_start = true;

moonmic_client_t* client = moonmic_create(&config);
if (!client) {
    return;
}

// Keep the client alive while the stream is active.

moonmic_destroy(client);
```

`moonmic_create()` copies `uniqueid` and `devicename`; the other pointers in the
configuration must remain valid only for the duration of the call.

## Pairing data

Moonlight integrations can pass Sunshine pairing state in the initial
handshake:

```cpp
config.uniqueid = client_id;
config.devicename = device_name;
config.pair_status = paired ? 1 : 0;
```

The host can reject clients whose handshake does not report a paired state.
This field is supplied by the integrating client; it is not independent host-side
certificate authentication.

## Stream control

```cpp
moonmic_start(client);
moonmic_stop(client);
moonmic_set_gain(client, 2.0f);

bool connected = moonmic_is_connected(client);
int rtt_ms = moonmic_client_get_rtt(client);
```

Callbacks are optional:

```cpp
moonmic_set_error_callback(client, on_error, userdata);
moonmic_set_status_callback(client, on_status, userdata);
```

## Protocol access

Applications that send setup or typing-focus requests without starting capture
should include `moonmic_protocol.h`. Do not include `moonmic_internal.h`; it is
private to the client implementation.

## PS Vita

The Vita implementation uses a 16 kHz mono input and batches the hardware's
256-sample reads into valid 20 ms Opus frames. The parent project must provide
VitaSDK, Opus, `SceNet_stub`, and `SceAudio_stub`.
