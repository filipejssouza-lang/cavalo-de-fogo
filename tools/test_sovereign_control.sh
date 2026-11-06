#!/bin/bash
set -e

echo "=========================================="
echo " Running Sovereign Control Tests..."
echo "=========================================="

mkdir -p build
gcc -O3 -Wall -DMOCK_HAL tools/lupin_slot_ctl.c -o build/lupin_slot_ctl_test -ldl

BIN="./build/lupin_slot_ctl_test"

export LUPIN_DATA_DIR=/tmp
rm -f /tmp/lupin_fails_a.bin /tmp/lupin_fails_b.bin

# Test 1: check-rollback e persistência
$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_fails_a.bin 2>/dev/null || echo 0)
if [ "$FAIL_COUNT" != "1" ]; then
    echo "Falhou! Esperado 1, encontrado: $FAIL_COUNT"
    false
fi

$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_fails_a.bin 2>/dev/null || echo 0)
if [ "$FAIL_COUNT" != "2" ]; then
    echo "Falhou! Esperado 2, encontrado: $FAIL_COUNT"
    false
fi

# Test 2: Rollback apos 3 falhas, deve voltar para 0
$BIN check-rollback
FAIL_COUNT=$(cat /tmp/lupin_fails_a.bin 2>/dev/null || echo 0)
if [ "$FAIL_COUNT" != "0" ]; then
    echo "Rollback não reiniciou contador. Encontrado: $FAIL_COUNT"
    false
fi

# Test 3: mark-success deve limpar falhas
$BIN check-rollback
$BIN mark-success
FAIL_COUNT=$(cat /tmp/lupin_fails_a.bin 2>/dev/null || echo 0)
if [ "$FAIL_COUNT" != "0" ]; then
    echo "mark-success não limpou falhas! Esperado 0, encontrado: $FAIL_COUNT"
    false
fi

# Test 4: verify dry-run doesn't write
$BIN check-rollback
$BIN check-rollback --dry-run
FAIL_COUNT=$(cat /tmp/lupin_fails_a.bin 2>/dev/null || echo 0)
if [ "$FAIL_COUNT" != "1" ]; then
    echo "dry-run modificou persistencia! Esperado 1, encontrado: $FAIL_COUNT"
    false
fi

rm -f /tmp/lupin_fails_a.bin /tmp/lupin_fails_b.bin
echo "[✓] Todos os testes da Soberania passaram com 100% sucesso."
