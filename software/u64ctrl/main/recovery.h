/*
 * recovery.h
 *
 * Network recovery: with the machine powered by the button handler, this asks
 * a recovery server which package fits the board, streams the runtime FPGA
 * bitstream into the FPGA and the update image into its memory, and starts the
 * updater. Nothing is started that fails a check: a bitstream whose hash is
 * wrong never leaves the configuration logic, and an image whose hash is wrong
 * is never jumped to. On any failure the machine is switched off again and the
 * red LED blinks the number of the step that failed:
 *
 *   1  no Wi-Fi connection
 *   2  no server had a package for this board
 *   3  no manifest passed its checks (signature, format, FPGA, serial)
 *   4  no Xilinx FPGA found on the JTAG chain
 *   5  the bitstream did not download or did not match its manifest
 *   6  the FPGA did not start
 *   7  the update image did not download, load, or match its manifest
 *
 * The green LED blinks while a recovery runs, and stays on for a while once
 * the updater has been started.
 */

#ifndef SOFTWARE_U64CTRL_MAIN_RECOVERY_H_
#define SOFTWARE_U64CTRL_MAIN_RECOVERY_H_

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Starts a recovery, unless one runs already. The machine must be powered. */
void recovery_start(void);

/* The machine was switched off: a running recovery stops at the next piece. */
void recovery_cancel(void);

/* Whether a recovery task is running. While it is, the machine's own firmware
   may not take over the Wi-Fi interface the recovery downloads through. */
bool recovery_running(void);

/* Whether the LEDs show a recovery, running or just finished; the main loop
   leaves them alone while they do. */
bool recovery_owns_leds(void);

/* The server tried before the built-in ones, stored in NVS. An empty URL
   removes it. ESP_ERR_INVALID_ARG for a URL recovery_url_valid() refuses. */
esp_err_t recovery_set_url(const char *url);
esp_err_t recovery_get_url(char *url, size_t size);

/* recovery_trust.c: the built-in servers, in the order they are tried, and the
   PEM public keys a manifest may be signed with. Both end with NULL. */
extern const char *const recovery_builtin_servers[];
extern const char *const recovery_trusted_keys[];

/* recovery_trust.c: the access point tried while a recovery runs, once the
   stored ones have failed. */
extern const char *const recovery_hotspot_ssid;
extern const char *const recovery_hotspot_password;

#endif /* SOFTWARE_U64CTRL_MAIN_RECOVERY_H_ */
