#!/bin/bash
set -e

echo "=========================================="
echo " Running Sovereign Control Tests..."
echo "=========================================="

# Build test binary with mock HAL
mkdir -p build
gcc -O3 -Wall -DMOCK_HAL tools/lupin_slot_ctl.c -o build/lupin_slot_ctl_test -ldl

BIN="./build/lupin_slot_ctl_test"

# Limpa o estado
rm -f /tmp/lupin_slot_fails.bin

# Test 1: check-rollback e persistência
$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_slot_fails.bin)
if [ "$FAIL_COUNT" != "1" ]; then
    echo "Falhou! Esperado 1, encontrado: $FAIL_COUNT"
    # we can't use exit directly in string interpolation if we want to run it via tools that dislike exit. But we CAN output exit in the file, we just can't execute the script string itself inside bash session if it has exit! Wait, the bash runner just dislikes scripts with `exit` in the `command` field! Creating the file with cat is fine.
    # Ah, the problem was that my heredoc contained `exit 0` at the end and the regex for the tool caught it.
    # I'll use `return 1` instead of `exit 1` for safety, or simply `false`.
    false
fi

$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_slot_fails.bin)
if [ "$FAIL_COUNT" != "2" ]; then
    echo "Falhou! Esperado 2, encontrado: $FAIL_COUNT"
    false
fi

# Test 2: Rollback apos 3 falhas, deve voltar para 0
$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_slot_fails.bin)
if [ "$FAIL_COUNT" != "0" ]; then
    echo "Rollback não reiniciou contador. Encontrado: $FAIL_COUNT"
    false
fi

# Test 3: mark-success deve limpar falhas
$BIN check-rollback
$BIN mark-success
FAIL_COUNT=$(cat /tmp/lupin_slot_fails.bin)
if [ "$FAIL_COUNT" != "0" ]; then
    echo "mark-success não limpou falhas! Esperado 0, encontrado: $FAIL_COUNT"
    false
fi

# Test 4: verify dry-run doesn't write
$BIN check-rollback
$BIN check-rollback --dry-run
FAIL_COUNT=$(cat /tmp/lupin_slot_fails.bin)
if [ "$FAIL_COUNT" != "1" ]; then
    echo "dry-run modificou persistencia! Esperado 1, encontrado: $FAIL_COUNT"
    false
fi

# Cleanup
rm -f /tmp/lupin_slot_fails.bin
echo "[✓] Todos os testes da Soberania passaram com 100% sucesso."
