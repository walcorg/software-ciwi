# =============================================================================
# Host test bench: CiWi TROPIC01 + Anchor code against the TROPIC01 model.
#
#   make            build ./ciwi_host
#   make test          start the model, run ciwi_host, then verify.js
#   make test-rconfig  same, with the CiWi access policy applied and checked
#
# Needs: gcc, the model server (pip install ts-tvl, see README.md), node + npm for verify.js.
# =============================================================================
ROOT      := ../..
VENDOR    ?= $(ROOT)/vendor
LT        := $(VENDOR)/libtropic
TC        := $(VENDOR)/trezor-firmware/crypto
MODEL_CFG ?= $(LT)/scripts/tropic01_model/model_cfg.yml
MODEL     ?= model_server

LT_SRCS := $(addprefix $(LT)/src/,libtropic.c lt_crc16.c lt_port_wrap.c lt_l1.c libtropic_l2.c \
           lt_l2_frame_check.c lt_l3_process.c libtropic_l3.c lt_hkdf.c lt_asn1_der.c \
           libtropic_default_sh0_keys.c lt_tr01_attrs.c libtropic_secure_memzero.c) \
           $(wildcard $(LT)/cal/trezor_crypto/*.c) \
           $(LT)/hal/posix/tcp/libtropic_port_posix_tcp.c

TC_SRCS := $(addprefix $(TC)/,bignum.c ecdsa.c curves.c secp256k1.c nist256p1.c sha2.c sha3.c \
           hmac.c memzero.c rfc6979.c hmac_drbg.c hasher.c blake256.c blake2b.c groestl.c \
           ripemd160.c rand.c consteq.c address.c base58.c \
           aes/aescrypt.c aes/aeskey.c aes/aestab.c aes/aes_modes.c aes/aesgcm.c aes/gf128mul.c \
           ed25519-donna/ed25519.c ed25519-donna/curve25519-donna-32bit.c \
           ed25519-donna/curve25519-donna-helpers.c ed25519-donna/modm-donna-32bit.c \
           ed25519-donna/ed25519-donna-basepoint-table.c ed25519-donna/ed25519-donna-32bit-tables.c \
           ed25519-donna/ed25519-donna-impl-base.c ed25519-donna/curve25519-donna-scalarmult-base.c)

CIWI_SRCS := $(ROOT)/tropic/tr01_ciwi.c $(ROOT)/tropic/tr01_pin.c \
             $(ROOT)/anchor/anchor_eip712.c $(ROOT)/anchor/anchor_ciwi.c $(ROOT)/anchor/anchor_selftest.c \
             platform_host.c host_main.c

# libtropic configuration (same values as the firmware Makefile, LT_DEFS)
LT_DEFS := -DLT_HELPERS -DLT_SILICON_REV_ACAB -DLT_LOG_ENABLE_ERROR=1 -DLT_LOG_ENABLE_WARN=0 \
           -DLT_LOG_ENABLE_INFO=0 -DLT_LOG_ENABLE_DEBUG=0 -DLT_CRC_ERR_RETRY_ATTEMPTS=3 \
           -DLT_L1_READ_MAX_TRIES=50 -DLT_L1_READ_RETRY_DELAY_MS=25 -DLT_L1_SPI_TIMEOUT_MS=70 \
           -DLT_L1_INT_TIMEOUT_MS=200 -DLT_HAVE_EXPLICIT_BZERO -D_DEFAULT_SOURCE -DAES_VAR

EXTRA_CFLAGS ?=
CFLAGS := -O1 -g -std=gnu11 -Wall -Wno-unused-function -DCIWI_HOST $(LT_DEFS) $(EXTRA_CFLAGS) \
          -I$(ROOT)/tropic -I$(ROOT)/anchor -I$(LT)/include -I$(LT)/src \
          -I$(LT)/cal/trezor_crypto -I$(LT)/hal/posix/tcp -I$(TC) -I$(TC)/aes

ciwi_host: $(CIWI_SRCS) $(LT_SRCS) $(TC_SRCS) $(wildcard $(ROOT)/tropic/*.h $(ROOT)/anchor/*.h)
	$(CC) $(CFLAGS) $(CIWI_SRCS) $(LT_SRCS) $(TC_SRCS) -o $@

test: ciwi_host
	@rm -f ciwi-keystore.bin vectors.json
	@$(MODEL) tcp -c $(MODEL_CFG) > model.log 2>&1 & echo $$! > model.pid; sleep 4
	@./ciwi_host vectors.json; status=$$?; kill `cat model.pid`; rm -f model.pid; \
	 test $$status -eq 0 && node verify.js vectors.json

# Same run with the CiWi access policy written to the R-config, plus checks that it is enforced.
test-rconfig:
	$(MAKE) -B ciwi_host EXTRA_CFLAGS="-DCIWI_TR01_APPLY_RCONFIG=1"
	$(MAKE) test
	$(MAKE) -B ciwi_host

clean:
	rm -f ciwi_host ciwi-keystore.bin vectors.json model.log model.pid

.PHONY: test test-rconfig clean
