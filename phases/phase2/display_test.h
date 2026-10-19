/**
 * @file    display_test.h
 * @brief   Tests visuales del display TFT — WalCorg Fase 2
 *
 * Suite de pruebas visuales para verificar que el driver
 * display_tft.c funciona correctamente. Cada test verifica
 * una capacidad diferente del driver.
 *
 * CRITERIO DE ÉXITO DE LA FASE 2:
 *   ✓ Test 1: pantalla se llena de colores sólidos sin artefactos
 *   ✓ Test 2: líneas y formas geométricas aparecen correctas
 *   ✓ Test 3: texto legible en múltiples tamaños
 *   ✓ Test 4: dirección Bitcoin ejemplo aparece completa
 *   ✓ Test 5: barra de progreso animada funciona
 *   ✓ Test 6 (opcional): el touch reporta coordenadas al tocar
 *
 * @license GNU GPL v3
 */

#ifndef DISPLAY_TEST_H
#define DISPLAY_TEST_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Ejecuta todos los tests visuales en secuencia.
 *
 * Muestra cada test 2 segundos antes de pasar al siguiente.
 * Imprime el resultado de cada test por UART.
 *
 * @return true si todos los tests completaron sin errores
 */
bool display_run_all_tests(void);

/** Test 1: barras de color — verifica fill_rect y colores RGB565 */
void display_test_color_bars(void);

/** Test 2: formas geométricas — líneas, rectángulos, círculos */
void display_test_shapes(void);

/** Test 3: texto en múltiples tamaños — verifica font rendering */
void display_test_text(void);

/** Test 4: pantalla de dirección Bitcoin — caso de uso real */
void display_test_bitcoin_address(void);

/** Test 5: barra de progreso animada — verifica timing y redibujado */
void display_test_progress_bar(void);

/** Test 6: calibración de touch — toca los círculos en pantalla */
void display_test_touch_calibration(void);

#endif /* DISPLAY_TEST_H */
