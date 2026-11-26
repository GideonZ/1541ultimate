# Using HTTP and HTTPS

Both protocols use command-interface target 6. Select the protocol in the URL:
`http://example.test/path` or `https://example.test/path`. HTTPS is outbound client
support; it does not secure the device's management web server. The revised
firmware remains experimental and is not yet qualified on physical hardware.

Commands below are hexadecimal bytes; `H`, `RH` and `RB` are returned byte handles.
Strings are ASCII and NUL-terminated. Check each status before using its data.

| Operation | Bytes | Successful result |
| --- | --- | --- |
| Create GET | `06 11 01 "URL" 00` | Header handle `H` |
| Add header | `06 13 H "User-Agent: C64-HTTP/1.0" 00` | `000 OK` |
| Exchange JSON | `06 31 H FF` | Response header/body handles `RH RB` |
| Query JSON | `06 2A RB "key" 00` | Existing typed JSON representation |
| Exchange raw body | `06 32 H FF` | Body bytes and HTTP status/header prefix |
| Free header | `06 12 H` | `000 OK` |
| Free body | `06 22 RB` | `000 OK` |

`FF` selects no request body. Free returned response handles as well as request
handles. Raw bodies use the existing continuation mechanism and Data Last flag;
acknowledge the final block even when it is empty. The command interface is not a
new BASIC function or client library.

HTTPS defaults to port 443 and requires a matching certificate hostname, trusted
chain, qualified clock, entropy and negotiated provider. Applications do not send
certificates or clock values with each request. Failures return the existing
`503 SERVICE UNAVAILABLE` status without automatic HTTP fallback. The opt-in
native provider uses a 30-second total exchange budget; this is not a timing
guarantee for ordinary HTTP.

See [transport and framing](http-transport.md),
[provisioning](../target/pc/linux/test_http_tls/PROVISIONING.md) and
[readiness](https-release-readiness.md) before enabling firmware options.
