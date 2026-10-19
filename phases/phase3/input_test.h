/**
 * @file    input_test.h
 * @brief   Test interactivo del click-wheel y menú — WalCorg Fase 3
 *
 * Suite de tests que verifican que el click-wheel navega
 * correctamente los menús en el TFT.
 *
 * CRITERIO DE ÉXITO DE LA FASE 3:
 *   ✓ Test 1: todos los eventos del wheel se detectan y muestran
 *   ✓ Test 2: menú de 5 ítems navegable con el wheel
 *   ✓ Test 3: submenú anidado (menú dentro de menú)
 *   ✓ Test 4: entrada de número con el wheel (simula PIN)
 *   ✓ Test 5: compatibilidad BTN_YES / BTN_NO con Trezor
 *
 * @license GNU GPL v3
 */

#ifndef INPUT_TEST_H
#define INPUT_TEST_H

#include <stdbool.h>

/**
 * @brief Ejecuta todos los tests del click-wheel en secuencia.
 * @return true si todos los tests completaron sin errores críticos
 */
bool input_run_all_tests(void);

/** Test 1: detector de eventos — muestra cada evento en pantalla */
void input_test_event_detector(void);

/** Test 2: menú principal navegable */
void input_test_main_menu(void);

/** Test 3: submenú anidado (navegar dentro de una opción) */
void input_test_nested_menu(void);

/** Test 4: entrada de número con el wheel (simulador de PIN) */
void input_test_number_entry(void);

/** Test 5: compatibilidad BTN_YES/BTN_NO con Trezor */
void input_test_trezor_compat(void);

#endif /* INPUT_TEST_H */
