# Assembly64 response completion regression

Run on Linux with GCC/G++, Make and POSIX sockets:

```sh
make -B -C target/pc/linux/test_assembly test
```

The executable links the production Assembly64 client, HTTP parser, multipart
collector, temporary-file writer, FileManager and FileSystemA64. Link wrappers
redirect DNS and connect calls to an ephemeral loopback listener. A small storage
adapter maps the `/Temp` medium to a private `mkdtemp` directory. No public server,
device, firmware installation or privileged port is used. The command has a
45-second execution guard and enables AddressSanitizer and UndefinedBehaviorSanitizer.

Coverage includes all three JSON callers and the binary download/cache path:

- Empty responses, incomplete headers, short fixed-length bodies and incomplete
  chunked bodies are rejected, even when the received prefix is valid JSON.
- Failed preset requests do not populate the preset cache.
- Repeated failed downloads issue new HTTP requests and leave no partial upload
  or cache files, live writer context or open storage handles.
- Incomplete multipart downloads discard every collected file.
- A complete retry returns the exact bytes; a subsequent cache hit needs no new
  HTTP request. Complete empty and chunked downloads remain usable.

This is a host integration regression, not a hardware or FTP end-to-end test.
The real filesystem entry point used by the download/cache path is exercised;
the physical storage medium is substituted. Firmware networking, scheduling and
storage faults require separate device validation.
