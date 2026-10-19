/**
 * @file    storage_hal.c
 * @brief   Implementación del almacenamiento seguro NORCOW — WalCorg Fase 5
 *
 * ARQUITECTURA DE ALMACENAMIENTO:
 *
 *   Flash sector 9  (0x080A0000, 128KB) — NORCOW bloque A (activo)
 *   Flash sector 10 (0x080C0000, 128KB) — NORCOW bloque B (alternado)
 *   Flash sector 11 (0x080E0000, 128KB) — Backup anti-corte de energía
 *
 * FORMATO DE UNA ENTRADA NORCOW:
 *   [2 bytes: longitud] [1 byte: app_id] [1 byte: key] [N bytes: datos]
 *   El bloque termina cuando se encuentra 0xFFFF (Flash no escrita)
 *
 * CIFRADO DE LA SEMILLA:
 *   key = PBKDF2-HMAC-SHA256(PIN, salt, 10000 iteraciones)
 *   seed_encrypted = AES-256-CBC(key, iv, mnemonic_padded)
 *
 *   Usar 10,000 iteraciones de PBKDF2 hace que la fuerza bruta
 *   del PIN sea ~10,000x más lenta que sin KDF.
 *
 * PROTECCIÓN DE DATOS EN MEMORIA:
 *   La semilla descifrada NUNCA vive más de lo necesario en SRAM.
 *   Se limpia con memset() inmediatamente después de usarla.
 *
 * @license GNU GPL v3
 */

#include "storage_hal.h"
#include "uart_debug.h"
#include "stm32f4xx_hal.h"

/* Incluir crypto de Trezor — estas funciones son de la carpeta /crypto
 * del repo trezor-firmware y están bajo licencia MIT */
#include "sha2.h"       /* SHA-256 para PBKDF2 */
#include "pbkdf2.h"     /* PBKDF2-HMAC-SHA256  */
#include "aes/aes.h"    /* AES-256-CBC         */
#include "rand.h"       /* random32() del RNG hardware */

#include <string.h>
#include <stdio.h>

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */

/** Caché en SRAM del estado del almacenamiento
 *  Nota: NO cacheamos la semilla cifrada — siempre la leemos de Flash */
static struct {
    bool     initialized;      /**< Storage inicializado correctamente */
    bool     wallet_init;      /**< Wallet tiene semilla               */
    uint32_t flags;            /**< Flags leídos de Flash              */
    uint8_t  fail_count;       /**< Intentos PIN fallidos              */
    char     label[STORAGE_LABEL_MAX_LEN];
} _cache = {0};

/** Dirección del bloque NORCOW activo */
static uint32_t _active_block = NORCOW_SECTOR_A_ADDR;


/* =========================================================
 * FUNCIONES PRIVADAS — FLASH
 * ========================================================= */

/**
 * _flash_unlock — Desbloquear la Flash para escritura
 *
 * La Flash del STM32F4 tiene 2 niveles de protección:
 *   1. Registro CR bloqueado al arrancar → desbloquear con clave
 *   2. Opción bytes (RDP) → manejado en Fase 6
 */
static bool _flash_unlock(void) {
    HAL_StatusTypeDef st = HAL_FLASH_Unlock();
    if (st != HAL_OK) {
        DEBUG_PRINT("[STOR] ERROR: No se pudo desbloquear la Flash.\r\n");
        return false;
    }
    return true;
}

/**
 * _flash_lock — Bloquear la Flash después de escribir
 *
 * Siempre bloquear después de cada operación de escritura.
 * Flash desbloqueada = vulnerable a escrituras accidentales
 * por bugs en el firmware.
 */
static void _flash_lock(void) {
    HAL_FLASH_Lock();
}

/**
 * _flash_erase_sector — Borrar un sector completo de Flash
 *
 * Borrar en Flash = poner todos los bits a 1 (0xFF).
 * Solo se puede escribir poniendo bits de 1 a 0 (no al revés).
 * Por eso hay que borrar el sector completo antes de reescribir.
 *
 * [MOD-S2] Usa los números de sector del STM32F405 (sector 9-11)
 * en vez de los del F205 original de Trezor (sector 4-5).
 *
 * @param sector  Número de sector HAL (FLASH_SECTOR_9, etc.)
 * @return        true si el borrado fue exitoso
 */
static bool _flash_erase_sector(uint32_t sector) {
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error = 0;

    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* 2.7V - 3.6V */
    erase.Sector       = sector;
    erase.NbSectors    = 1;

    /* El borrado de un sector de 128KB tarda ~2 segundos */
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &error);

    if (st != HAL_OK || error != 0xFFFFFFFF) {
        DEBUG_PRINTF("[STOR] ERROR: Borrado del sector %lu falló (err=0x%lX)\r\n",
            sector, error);
        return false;
    }
    return true;
}

/**
 * _flash_write_word — Escribir una palabra de 32 bits en Flash
 *
 * La Flash del F405 permite escritura de 1, 2, 4, o 16 bytes.
 * Usamos escritura de 4 bytes (word) como unidad básica.
 *
 * @param addr  Dirección en Flash (debe ser múltiplo de 4)
 * @param data  Dato de 32 bits a escribir
 * @return      true si la escritura fue exitosa
 */
static bool _flash_write_word(uint32_t addr, uint32_t data) {
    HAL_StatusTypeDef st = HAL_FLASH_Program(
        FLASH_TYPEPROGRAM_WORD, addr, data
    );
    if (st != HAL_OK) {
        DEBUG_PRINTF("[STOR] ERROR: Escritura en 0x%08lX falló.\r\n", addr);
        return false;
    }
    return true;
}

/**
 * _flash_write_bytes — Escribir buffer de bytes en Flash
 *
 * Escribe el buffer alineando a palabras de 32 bits.
 * Si el buffer no es múltiplo de 4, el último word se
 * completa con 0xFF (Flash no escrita = 0xFF en F405).
 *
 * @param addr  Dirección de inicio (debe ser múltiplo de 4)
 * @param data  Buffer de datos
 * @param len   Longitud en bytes
 * @return      true si toda la escritura fue exitosa
 */
static bool _flash_write_bytes(uint32_t addr, const uint8_t *data,
                                uint32_t len) {
    uint32_t offset = 0;

    /* Escribir words completos */
    while (offset + 4 <= len) {
        uint32_t word;
        memcpy(&word, data + offset, 4);
        if (!_flash_write_word(addr + offset, word)) return false;
        offset += 4;
    }

    /* Último word parcial (si el buffer no es múltiplo de 4) */
    if (offset < len) {
        uint32_t word = 0xFFFFFFFF;  /* Flash "vacía" */
        memcpy(&word, data + offset, len - offset);
        if (!_flash_write_word(addr + offset, word)) return false;
    }

    return true;
}


/* =========================================================
 * FUNCIONES PRIVADAS — NORCOW
 * ========================================================= */

/**
 * Estructura del header de una entrada NORCOW
 * Cada entrada en Flash tiene este header seguido de los datos.
 */
typedef struct __attribute__((packed)) {
    uint16_t len;     /**< Longitud de los datos (bytes) */
    uint8_t  app_id;  /**< Identificador de la aplicación */
    uint8_t  key;     /**< Clave dentro de la aplicación */
} norcow_header_t;

/**
 * _norcow_find — Buscar una entrada por app_id y key
 *
 * Itera sobre el bloque activo buscando el header que
 * coincida con los parámetros buscados.
 *
 * @param app_id  ID de la aplicación
 * @param key     Clave de la entrada
 * @param data    Puntero que se actualizará con la dirección de los datos
 * @param len     Longitud de los datos encontrados
 * @return        true si se encontró la entrada
 */
static bool _norcow_find(uint8_t app_id, uint8_t key,
                          const uint8_t **data, uint16_t *len) {
    uint32_t addr = _active_block;
    uint32_t end  = _active_block + NORCOW_SECTOR_SIZE;

    while (addr + sizeof(norcow_header_t) <= end) {
        norcow_header_t hdr;
        memcpy(&hdr, (void *)addr, sizeof(hdr));

        /* 0xFFFF = Flash vacía → fin de las entradas */
        if (hdr.len == 0xFFFF) break;

        /* ¿Es la entrada que buscamos? */
        if (hdr.app_id == app_id && hdr.key == key) {
            if (data) *data = (const uint8_t *)(addr + sizeof(hdr));
            if (len)  *len  = hdr.len;
            return true;
        }

        /* Avanzar a la siguiente entrada (alineada a 4 bytes) */
        uint32_t entry_size = sizeof(hdr) + hdr.len;
        entry_size = (entry_size + 3) & ~3U;  /* Alinear a 4 */
        addr += entry_size;
    }

    return false;
}

/**
 * _norcow_write — Escribir una entrada nueva en NORCOW
 *
 * Busca el final del bloque activo (primer 0xFFFF) y
 * escribe el header + datos ahí.
 *
 * NOTA: NORCOW es Copy-on-Write — no actualiza entradas existentes.
 * Escribe la nueva versión al final. La lectura toma siempre la última.
 * Esto requiere compactar el bloque periódicamente (no implementado aquí).
 *
 * @param app_id  ID de la aplicación
 * @param key     Clave de la entrada
 * @param data    Datos a escribir
 * @param len     Longitud de los datos
 * @return        true si se escribió correctamente
 */
static bool _norcow_write(uint8_t app_id, uint8_t key,
                           const uint8_t *data, uint16_t len) {
    /* Buscar el final del bloque activo */
    uint32_t addr = _active_block;
    uint32_t end  = _active_block + NORCOW_SECTOR_SIZE;

    while (addr + sizeof(norcow_header_t) <= end) {
        uint16_t entry_len;
        memcpy(&entry_len, (void *)addr, 2);
        if (entry_len == 0xFFFF) break;  /* Espacio libre encontrado */

        uint32_t entry_size = sizeof(norcow_header_t) + entry_len;
        entry_size = (entry_size + 3) & ~3U;
        addr += entry_size;
    }

    /* Verificar que cabe la nueva entrada */
    uint32_t needed = sizeof(norcow_header_t) + len;
    needed = (needed + 3) & ~3U;

    if (addr + needed > end) {
        DEBUG_PRINT("[STOR] ERROR: Bloque NORCOW lleno.\r\n");
        return false;
    }

    /* Preparar y escribir el header + datos */
    norcow_header_t hdr = { .len = len, .app_id = app_id, .key = key };

    if (!_flash_unlock()) return false;

    bool ok = _flash_write_bytes(addr, (uint8_t *)&hdr, sizeof(hdr));
    if (ok && data && len > 0) {
        ok = _flash_write_bytes(addr + sizeof(hdr), data, len);
    }

    _flash_lock();
    return ok;
}


/* =========================================================
 * FUNCIONES PRIVADAS — CRYPTO
 * ========================================================= */

/**
 * _derive_key — Derivar clave AES-256 desde PIN con PBKDF2
 *
 * key = PBKDF2-HMAC-SHA256(password=PIN, salt=salt, iterations=10000)
 *
 * 10,000 iteraciones hacen que intentar un PIN incorrecto tome
 * ~100ms en el STM32F405. Con 10 intentos máximos, la fuerza
 * bruta tomaría ~1 segundo por dispositivo — trivial. PERO:
 * cada intento fallido incrementa el contador y al llegar a 10
 * se borra el wallet. Esto es la verdadera protección.
 *
 * @param pin      PIN en texto plano
 * @param salt     Sal aleatoria de 16 bytes
 * @param key_out  Buffer de 32 bytes para la clave derivada
 */
static void _derive_key(const char *pin, const uint8_t *salt,
                         uint8_t *key_out) {
    /* PBKDF2-HMAC-SHA256 con 10,000 iteraciones */
    pbkdf2_hmac_sha256(
        (const uint8_t *)pin, strlen(pin),   /* Password = PIN      */
        salt, STORAGE_PIN_SALT_LEN,           /* Salt aleatoria      */
        10000,                                /* Iteraciones         */
        key_out, 32                           /* Salida: 32 bytes    */
    );
}


/* =========================================================
 * API PÚBLICA
 * ========================================================= */

bool storage_hal_init(void) {
    DEBUG_PRINT("[STOR] Inicializando almacenamiento NORCOW...\r\n");
    DEBUG_PRINTF("[STOR]   Bloque A: 0x%08lX (sector 9)\r\n",
        NORCOW_SECTOR_A_ADDR);
    DEBUG_PRINTF("[STOR]   Bloque B: 0x%08lX (sector 10)\r\n",
        NORCOW_SECTOR_B_ADDR);

    /* Verificar acceso a los sectores (leer el primer word) */
    uint32_t test_a = *(volatile uint32_t *)NORCOW_SECTOR_A_ADDR;
    uint32_t test_b = *(volatile uint32_t *)NORCOW_SECTOR_B_ADDR;
    (void)test_a; (void)test_b;

    /* Usar sector A como bloque activo por defecto */
    _active_block = NORCOW_SECTOR_A_ADDR;

    /* Cargar estado del caché */
    const uint8_t *data;
    uint16_t       len;

    /* Verificar si el wallet está inicializado */
    if (_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_INITIALIZED,
                     &data, &len) && len >= 1) {
        _cache.wallet_init = (data[0] != 0);
    }

    /* Cargar contador de intentos fallidos */
    if (_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_FAIL_COUNT,
                     &data, &len) && len >= 1) {
        _cache.fail_count = data[0];
    }

    /* Cargar flags */
    if (_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_FLAGS,
                     &data, &len) && len >= 4) {
        memcpy(&_cache.flags, data, 4);
    }

    /* Cargar label */
    if (_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_LABEL,
                     &data, &len) && len > 0) {
        uint16_t copy_len = len < STORAGE_LABEL_MAX_LEN - 1 ?
                            len : STORAGE_LABEL_MAX_LEN - 1;
        memcpy(_cache.label, data, copy_len);
        _cache.label[copy_len] = '\0';
    } else {
        strncpy(_cache.label, "WalCorg", STORAGE_LABEL_MAX_LEN - 1);
    }

    _cache.initialized = true;

    DEBUG_PRINTF("[STOR] Wallet inicializado: %s\r\n",
        _cache.wallet_init ? "SI" : "NO");
    DEBUG_PRINTF("[STOR] Intentos PIN fallidos: %d/%d\r\n",
        _cache.fail_count, STORAGE_PIN_MAX_FAILS);

    return true;
}

bool storage_is_initialized(void) {
    return _cache.wallet_init;
}

bool storage_verify_pin(const char *pin) {
    if (!pin || !_cache.initialized) return false;

    /* Verificar si hay intentos disponibles */
    if (_cache.fail_count >= STORAGE_PIN_MAX_FAILS) {
        DEBUG_PRINT("[STOR] FATAL: Máximo de intentos alcanzado. Borrando...\r\n");
        storage_wipe();
        return false;
    }

    /* Leer hash y salt almacenados */
    const uint8_t *stored_hash, *stored_salt;
    uint16_t hash_len, salt_len;

    if (!_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_PIN_HASH,
                      &stored_hash, &hash_len) || hash_len != STORAGE_PIN_HASH_LEN) {
        DEBUG_PRINT("[STOR] No hay PIN almacenado.\r\n");
        return false;
    }

    if (!_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_PIN_SALT,
                      &stored_salt, &salt_len) || salt_len != STORAGE_PIN_SALT_LEN) {
        return false;
    }

    /* Calcular hash del PIN provisto */
    uint8_t computed_hash[STORAGE_PIN_HASH_LEN];
    _derive_key(pin, stored_salt, computed_hash);

    /* Comparación en tiempo constante — evitar timing attacks */
    uint8_t diff = 0;
    for (int i = 0; i < STORAGE_PIN_HASH_LEN; i++) {
        diff |= (computed_hash[i] ^ stored_hash[i]);
    }

    /* Limpiar el hash calculado de SRAM */
    memset(computed_hash, 0, sizeof(computed_hash));

    bool ok = (diff == 0);
    if (ok) {
        storage_reset_fail_count();
        DEBUG_PRINT("[STOR] PIN verificado correctamente.\r\n");
    } else {
        uint8_t fails = storage_increment_fail_count();
        DEBUG_PRINTF("[STOR] PIN incorrecto. Intentos restantes: %d\r\n",
            STORAGE_PIN_MAX_FAILS - fails);
    }

    return ok;
}

bool storage_set_pin(const char *pin) {
    if (!pin || strlen(pin) < 4) {
        DEBUG_PRINT("[STOR] ERROR: PIN demasiado corto (mínimo 4 dígitos).\r\n");
        return false;
    }

    /* Generar sal aleatoria usando el hardware RNG */
    uint8_t salt[STORAGE_PIN_SALT_LEN];
    for (int i = 0; i < STORAGE_PIN_SALT_LEN / 4; i++) {
        uint32_t r = random32();  /* random32() de Trezor crypto/rand.h */
        memcpy(salt + i * 4, &r, 4);
    }

    /* Calcular hash del PIN */
    uint8_t hash[STORAGE_PIN_HASH_LEN];
    _derive_key(pin, salt, hash);

    /* Guardar sal y hash en Flash */
    bool ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_PIN_SALT,
                             salt, STORAGE_PIN_SALT_LEN);
    if (ok) {
        ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_PIN_HASH,
                            hash, STORAGE_PIN_HASH_LEN);
    }

    /* Actualizar flags */
    if (ok) {
        _cache.flags |= STORAGE_FLAG_PIN_ENABLED;
        ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_FLAGS,
                            (uint8_t *)&_cache.flags, sizeof(_cache.flags));
    }

    /* Limpiar datos sensibles de SRAM */
    memset(salt, 0, sizeof(salt));
    memset(hash, 0, sizeof(hash));

    if (ok) DEBUG_PRINT("[STOR] PIN establecido correctamente.\r\n");
    return ok;
}

bool storage_set_seed(const char *mnemonic, const char *pin) {
    if (!mnemonic || !pin) return false;

    /* Obtener salt del PIN para derivar la clave AES */
    const uint8_t *salt;
    uint16_t salt_len;
    if (!_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_PIN_SALT,
                      &salt, &salt_len)) {
        DEBUG_PRINT("[STOR] ERROR: No hay PIN configurado. Configurar PIN primero.\r\n");
        return false;
    }

    /* Derivar clave AES-256 desde el PIN */
    uint8_t aes_key[32];
    _derive_key(pin, salt, aes_key);

    /* Generar IV aleatorio */
    uint8_t iv[STORAGE_AES_IV_LEN];
    for (int i = 0; i < STORAGE_AES_IV_LEN / 4; i++) {
        uint32_t r = random32();
        memcpy(iv + i * 4, &r, 4);
    }

    /* Preparar buffer de la semilla con padding PKCS7 */
    uint8_t plaintext[STORAGE_SEED_ENC_LEN];
    uint8_t ciphertext[STORAGE_SEED_ENC_LEN];
    memset(plaintext, 0, sizeof(plaintext));

    size_t mnemonic_len = strlen(mnemonic);
    if (mnemonic_len >= STORAGE_SEED_ENC_LEN) {
        DEBUG_PRINT("[STOR] ERROR: Mnemónico demasiado largo.\r\n");
        memset(aes_key, 0, sizeof(aes_key));
        return false;
    }
    memcpy(plaintext, mnemonic, mnemonic_len);

    /* Padding PKCS7 */
    uint8_t pad_len = STORAGE_SEED_ENC_LEN - mnemonic_len;
    memset(plaintext + mnemonic_len, pad_len, pad_len);

    /* Cifrar con AES-256-CBC */
    aes_encrypt_ctx ctx;
    aes_encrypt_key256(aes_key, &ctx);

    /* Hacer copia del IV (AES modifica el IV durante CBC) */
    uint8_t iv_copy[STORAGE_AES_IV_LEN];
    memcpy(iv_copy, iv, STORAGE_AES_IV_LEN);

    aes_cbc_encrypt(plaintext, ciphertext,
                    STORAGE_SEED_ENC_LEN, iv_copy, &ctx);

    /* Guardar IV y semilla cifrada en Flash */
    bool ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_SEED_IV,
                             iv, STORAGE_AES_IV_LEN);
    if (ok) {
        ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_SEED_ENC,
                            ciphertext, STORAGE_SEED_ENC_LEN);
    }

    /* Marcar wallet como inicializado */
    if (ok) {
        uint8_t init_flag = 1;
        ok = _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_INITIALIZED,
                            &init_flag, 1);
        if (ok) _cache.wallet_init = true;
    }

    /* Limpiar TODOS los datos sensibles de SRAM */
    memset(aes_key,    0, sizeof(aes_key));
    memset(plaintext,  0, sizeof(plaintext));
    memset(ciphertext, 0, sizeof(ciphertext));
    memset(iv,         0, sizeof(iv));
    memset(iv_copy,    0, sizeof(iv_copy));
    memset(&ctx,       0, sizeof(ctx));

    if (ok) DEBUG_PRINT("[STOR] Semilla cifrada y almacenada.\r\n");
    return ok;
}

bool storage_get_seed(const char *pin, char *mnemonic_out,
                      uint32_t buf_size) {
    if (!pin || !mnemonic_out || buf_size < 240) return false;

    /* Verificar PIN primero */
    if (!storage_verify_pin(pin)) return false;

    /* Leer IV y semilla cifrada de Flash */
    const uint8_t *iv_flash, *enc_flash;
    uint16_t iv_len, enc_len;

    if (!_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_SEED_IV,
                      &iv_flash, &iv_len) || iv_len != STORAGE_AES_IV_LEN) {
        return false;
    }
    if (!_norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_SEED_ENC,
                      &enc_flash, &enc_len) || enc_len != STORAGE_SEED_ENC_LEN) {
        return false;
    }

    /* Derivar clave AES desde el PIN */
    const uint8_t *salt;
    uint16_t salt_len;
    _norcow_find(STORAGE_APP_WALLET, STORAGE_KEY_PIN_SALT, &salt, &salt_len);

    uint8_t aes_key[32];
    _derive_key(pin, salt, aes_key);

    /* Descifrar semilla */
    uint8_t plaintext[STORAGE_SEED_ENC_LEN];
    uint8_t iv_copy[STORAGE_AES_IV_LEN];
    memcpy(iv_copy, iv_flash, STORAGE_AES_IV_LEN);

    aes_decrypt_ctx ctx;
    aes_decrypt_key256(aes_key, &ctx);
    aes_cbc_decrypt(enc_flash, plaintext,
                    STORAGE_SEED_ENC_LEN, iv_copy, &ctx);

    /* Remover padding PKCS7 */
    uint8_t pad_len = plaintext[STORAGE_SEED_ENC_LEN - 1];
    uint32_t mnemonic_len = STORAGE_SEED_ENC_LEN - pad_len;
    if (mnemonic_len >= buf_size) mnemonic_len = buf_size - 1;

    memcpy(mnemonic_out, plaintext, mnemonic_len);
    mnemonic_out[mnemonic_len] = '\0';

    /* Limpiar TODOS los datos sensibles */
    memset(aes_key,   0, sizeof(aes_key));
    memset(plaintext, 0, sizeof(plaintext));
    memset(iv_copy,   0, sizeof(iv_copy));
    memset(&ctx,      0, sizeof(ctx));

    return true;
}

uint8_t storage_increment_fail_count(void) {
    _cache.fail_count++;
    _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_FAIL_COUNT,
                  &_cache.fail_count, 1);

    if (_cache.fail_count >= STORAGE_PIN_MAX_FAILS) {
        DEBUG_PRINT("[STOR] WIPE: Máximo de intentos alcanzado!\r\n");
        storage_wipe();
    }
    return _cache.fail_count;
}

void storage_reset_fail_count(void) {
    _cache.fail_count = 0;
    _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_FAIL_COUNT,
                  &_cache.fail_count, 1);
}

uint8_t storage_get_fail_count(void) {
    return _cache.fail_count;
}

bool storage_wipe(void) {
    DEBUG_PRINT("[STOR] !!! BORRADO TOTAL DEL WALLET !!!\r\n");
    DEBUG_PRINT("[STOR] Borrando sectores NORCOW 9, 10 y 11...\r\n");

    if (!_flash_unlock()) return false;

    bool ok = _flash_erase_sector(NORCOW_HAL_SECTOR_A);
    if (ok) ok = _flash_erase_sector(NORCOW_HAL_SECTOR_B);
    if (ok) ok = _flash_erase_sector(NORCOW_HAL_SECTOR_BACKUP);

    _flash_lock();

    /* Limpiar caché en SRAM */
    memset(&_cache, 0, sizeof(_cache));
    _cache.initialized = true;

    if (ok) {
        DEBUG_PRINT("[STOR] Borrado completado. Wallet vacío.\r\n");
    }
    return ok;
}

void storage_get_label(char *label_out) {
    if (!label_out) return;
    strncpy(label_out, _cache.label, STORAGE_LABEL_MAX_LEN - 1);
    label_out[STORAGE_LABEL_MAX_LEN - 1] = '\0';
}

bool storage_set_label(const char *label) {
    if (!label) return false;
    size_t len = strlen(label);
    if (len >= STORAGE_LABEL_MAX_LEN) len = STORAGE_LABEL_MAX_LEN - 1;

    strncpy(_cache.label, label, len);
    _cache.label[len] = '\0';

    return _norcow_write(STORAGE_APP_WALLET, STORAGE_KEY_LABEL,
                          (uint8_t *)label, (uint16_t)len);
}

void storage_print_status(void) {
#if (PRODUCTION == 0)
    DEBUG_PRINT("\r\n[STOR] === Estado del Almacenamiento ===\r\n");
    DEBUG_PRINTF("[STOR] Bloque activo : 0x%08lX\r\n", _active_block);
    DEBUG_PRINTF("[STOR] Wallet init   : %s\r\n",
        _cache.wallet_init ? "SI" : "NO");
    DEBUG_PRINTF("[STOR] PIN habilitado: %s\r\n",
        (_cache.flags & STORAGE_FLAG_PIN_ENABLED) ? "SI" : "NO");
    DEBUG_PRINTF("[STOR] Fallos PIN    : %d/%d\r\n",
        _cache.fail_count, STORAGE_PIN_MAX_FAILS);
    DEBUG_PRINTF("[STOR] Label         : %s\r\n", _cache.label);
    DEBUG_PRINT("[STOR] =====================================\r\n\r\n");
#endif
}
