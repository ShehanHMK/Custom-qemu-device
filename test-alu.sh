#!/usr/bin/env bash

# Simple ALU test script using devmem
# Usage:
#   ./test-alu.sh add 35 76
#   ./test-alu.sh sub 32 19
#   ./test-alu.sh mul 7 6
#   ./test-alu.sh div 84 2

BASE=0xA0000000

CONTROL=0x0000
OPERATION=0x0004
OPERAND_A=0x0008
OPERAND_B=0x000C
RESULT=0x0010
STATUS=0x0014

CTRL_START=0x1
CTRL_RESET=0x2

addr() {
    printf "0x%X" $((BASE + $1))
}

write_reg() {
    devmem "$(addr "$1")" 32 "$2" >/dev/null
}

read_reg() {
    devmem "$(addr "$1")" 32
}

if [ $# -ne 3 ]; then
    echo "Usage: $0 <add|sub|mul|div> <a> <b>"
    exit 1
fi

OP_NAME="$1"
A="$2"
B="$3"

case "$OP_NAME" in
    add) OP=0 ;;
    sub) OP=1 ;;
    mul) OP=2 ;;
    div) OP=3 ;;
    *)
        echo "Invalid operation: $OP_NAME"
        echo "Use one of: add, sub, mul, div"
        exit 1
        ;;
esac

# Reset device
write_reg "$CONTROL" "$CTRL_RESET"

# Write inputs
write_reg "$OPERATION" "$OP"
write_reg "$OPERAND_A" "$A"
write_reg "$OPERAND_B" "$B"

# Start operation
write_reg "$CONTROL" "$CTRL_START"

# Read back result
RESULT_HEX=$(read_reg "$RESULT")
STATUS_HEX=$(read_reg "$STATUS")

RESULT_DEC=$((RESULT_HEX))
STATUS_DEC=$((STATUS_HEX))

echo "Operation : $OP_NAME"
echo "Operand A : $A"
echo "Operand B : $B"
echo "Result    : $RESULT_DEC"

