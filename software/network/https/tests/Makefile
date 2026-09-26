CC ?= cc
CFLAGS ?= -O1 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
SANITIZE_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

.PHONY: all test sanitize tls clean deadlines
all: test

deadlines:
	python3 check_tcp_deadlines.py

.PHONY: framing
framing:
	$(CC) -O1 -g -std=c99 -D_DEFAULT_SOURCE -DMHS_PORT=8001 -DRUNS_ON_PC \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	    -I../../../httpd/c-version/lib ../../../httpd/c-version/lib/http_protocol.c \
	    test_http_response.c -o test_http_response
	./test_http_response

test_https_client: ../https_client.c ../https_client.h test_https_client.c
	$(CC) $(CFLAGS) -I.. ../https_client.c test_https_client.c -o $@

test_tls_wire: ../tls_wire.c ../tls_wire.h ../https_client.c ../https_client.h test_tls_wire.c
	$(CC) $(CFLAGS) -I.. ../tls_wire.c ../https_client.c test_tls_wire.c -o $@

test_tls_service: ../tls_service.c ../tls_service.h ../tls_wire.c ../https_client.c test_tls_service.c
	$(CC) $(CFLAGS) -I.. ../tls_service.c ../tls_wire.c ../https_client.c test_tls_service.c -o $@

test_tls_epoch: ../tls_epoch.c ../tls_epoch.h ../tls_wire.c ../https_client.c test_tls_epoch.c
	$(CC) $(CFLAGS) -I.. ../tls_epoch.c ../tls_wire.c ../https_client.c test_tls_epoch.c -o $@

test_tls_restart: ../tls_service.c ../tls_service.h ../tls_wire.c ../https_client.c test_tls_restart.c
	$(CC) $(CFLAGS) -I.. ../tls_service.c ../tls_wire.c ../https_client.c test_tls_restart.c -o $@

test_tls_metrics: ../tls_metrics.h ../https_failure.h test_tls_metrics.c
	$(CC) $(CFLAGS) -I.. test_tls_metrics.c -o $@

test: test_https_client test_tls_wire test_tls_service test_tls_epoch test_tls_restart test_tls_metrics
	./test_https_client
	./test_tls_wire
	./test_tls_service
	./test_tls_epoch
	./test_tls_restart
	./test_tls_metrics

sanitize:
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. ../https_client.c test_https_client.c -o test_https_client_sanitize
	./test_https_client_sanitize
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. ../tls_wire.c ../https_client.c test_tls_wire.c -o test_tls_wire_sanitize
	./test_tls_wire_sanitize
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. ../tls_service.c ../tls_wire.c ../https_client.c test_tls_service.c -o test_tls_service_sanitize
	./test_tls_service_sanitize
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. ../tls_epoch.c ../tls_wire.c ../https_client.c test_tls_epoch.c -o test_tls_epoch_sanitize
	./test_tls_epoch_sanitize
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. ../tls_service.c ../tls_wire.c ../https_client.c test_tls_restart.c -o test_tls_restart_sanitize
	./test_tls_restart_sanitize
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) -I.. test_tls_metrics.c -o test_tls_metrics_sanitize
	./test_tls_metrics_sanitize

clean:
	rm -f test_http_response
	rm -f test_https_client test_https_client_sanitize test_tls_wire test_tls_wire_sanitize test_tls_service test_tls_service_sanitize
	rm -f test_tls_epoch test_tls_epoch_sanitize test_tls_restart test_tls_restart_sanitize
	rm -f test_tls_metrics test_tls_metrics_sanitize

# Supply paths to a standalone build of the same Mbed TLS revision as ESP-IDF.
tls:
	python3 test_tls.py --mbedtls-source "$(MBEDTLS_SOURCE)" --mbedtls-build "$(MBEDTLS_BUILD)"
