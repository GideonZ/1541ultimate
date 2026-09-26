# Using HTTP and HTTPS on Commodore 64 Ultimate

Both protocols use the existing Ultimate command interface, target 6. Select
HTTPS by changing the request URL from `http://` to `https://`. Request creation,
headers, bodies, exchange commands and response handling keep the existing HTTP
interface. This feature provides outbound connections from a C64 application;
it does not enable HTTPS on the device's management web server.

## The same request over either protocol

These endpoints returned the same `{"ok":true}` response in the recorded device
tests. They are external services, so future availability and responses may vary.

```text
HTTP:  http://httpbingo.org/base64/eyJvayI6dHJ1ZX0=
HTTPS: https://httpbingo.org/base64/eyJvayI6dHJ1ZX0=
```

1. Create a GET request and URL with `HEADER_CREATE`; retain the returned handle.
2. Add any required headers with `HEADER_ADD`. The example sends a User-Agent;
   omitting it produced HTTP 402 from this service in an earlier test.
3. Use `DO_EXCHANGE_OBJ` for a JSON object or `DO_EXCHANGE_RAW` for raw bytes.
   This GET example has no request body.
4. Read the response, then free the handles owned by the request and response.

These are existing command names, not new BASIC functions or a new public
client library.

## Command bytes for a C64 application

All numbers in this table are hexadecimal. Quoted text represents ASCII bytes;
do not send the quotation marks. `H`, `RH` and `RB` are single-byte handles read
from responses, not fixed values. `00` terminates a string. Send URLs as ASCII;
keyboard PETSCII strings do not necessarily contain the same bytes.

| Operation | Command bytes | Result |
| --- | --- | --- |
| Create GET | `06 11 01 "URL" 00` | One byte: `H`; status `000 OK` |
| Add the example header | `06 13 H "User-Agent: Ultimate-HTTPS-hardware-test/1.0" 00` | Status `000 OK` |
| Request a JSON object | `06 31 H FF` | Two bytes: `RH RB`; status such as `200 OK` |
| Query the whole JSON object | `06 2A RB 00` | Typed binary JSON representation |
| Query only `ok` | `06 2A RB "ok" 00` | `02 01`: boolean type and true value |
| Free the request header | `06 12 H` | Status `000 OK` |
| Free the response header | `06 12 RH` | Status `000 OK` |
| Free the response body | `06 22 RB` | Status `000 OK` |

In `HEADER_CREATE`, `01` selects GET. `FF` in the exchange command means there
is no request body. Command `06 10` frees all HTTP handles; an isolated example
can use it before and after execution, but it also releases resources belonging
to other active requests. Check status and response length at every step. Do not
interpret an error response as valid handles.

The whole-object query returned these bytes for both protocols in the device
test:

```text
04 01 02 6F 6B 02 01
```

This is the internal typed representation of `{"ok":true}`, not JSON text.

## Reading raw data

After creating the request header, send this instead of `06 31 H FF`:

```text
06 32 H FF
```

The data channel returns body bytes, such as the text `{"ok":true}`. The status
channel returns the beginning of the HTTP response, including its status line
and headers, up to 255 bytes. For additional response headers in object mode,
use the response header handle with the existing `HEADER_QUERY`/`HEADER_LIST`
commands.

A body can span multiple blocks of up to 895 bytes. Use the existing `DATA_ACC`
and continuation mechanism until the interface reports Data Last. Determine
completion from the interface state, not only from the byte count: an exactly
895-byte response can include a final empty block. Acknowledge the final block
as well. A recorded 2,048-byte response arrived as `895 + 895 + 258` bytes.

Commands and responses use the existing register interface. An executable 6502
test program is in
[`uci_agent.asm`](../tests/e2e/io/command_interface/uci_agent.asm), with its host
driver in [`uci_native.py`](../tests/e2e/lib/uci_native.py). These are test tools,
not a new public C64 library.

## Running the example from a host computer

[`smoke.py`](../tools/c64u_https/smoke.py) runs a small 6502 program on the C64;
the commands originate on the C64, while host Python controls the run and
records results. It replaces the current C64 program and part of its RAM. It
does not install firmware. The test program remains in RAM afterwards; use a
normal C64 reset to leave it.

From the repository root on Linux or WSL, replace `DEVICE_IP` with the device's
address:

```sh
python3 tools/c64u_https/smoke.py --host DEVICE_IP --url http://httpbingo.org/base64/eyJvayI6dHJ1ZX0= --url https://httpbingo.org/base64/eyJvayI6dHJ1ZX0= --expect-hex 0401026f6b0201 --output /tmp/demo-http-https.json
```

This checks the same expected typed data for both protocols. To receive raw
bytes instead, add `--raw` and change `--expect-hex` to
`7b226f6b223a747275657d`, the hexadecimal encoding of the JSON text.

## Configuration and failures

| Property | HTTP | HTTPS |
| --- | --- | --- |
| URL | `http://...` | `https://...` |
| Default port | 80 | 443 |
| Target and commands | Target 6 HTTP commands | The same commands |
| Headers, bodies and handles | Existing interface | The same interface |
| Encryption and server authentication | None | Handled by firmware |
| Synchronized time and trusted certificates | Not needed for TLS | Required; managed by firmware |

Applications do not add certificates or a clock value to each request command.
The device needs working networking, DNS, synchronized time and a compatible
controller. For HTTPS, the hostname must match the server certificate. TLS,
clock and connection failures currently return the generic
`503 SERVICE UNAVAILABLE` status; that status alone does not identify the cause.
An HTTPS failure never triggers automatic fallback to HTTP. The total HTTPS
exchange deadline is 15 seconds; this is not a guarantee for the legacy HTTP
path.

See the [integration description](https-client.md) for implementation details,
the [device validation report](https-device-validation.md) for recorded results,
and [release readiness](https-release-readiness.md) for remaining qualification.
