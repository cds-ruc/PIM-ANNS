#!/usr/bin/env bash
AE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || exit 1

echo "Running all experiments..."

echo "Running exp 1..."
bash "$AE_DIR/exps/exp1.sh" || exit $?

echo "Running exp 2..."
bash "$AE_DIR/exps/exp2.sh" || exit $?

echo "Running exp 3..."
bash "$AE_DIR/exps/exp3.sh" || exit $?

echo "Running exp 4..."
bash "$AE_DIR/exps/exp4.sh" || exit $?

echo "Running exp 5..."
bash "$AE_DIR/exps/exp5.sh" || exit $?

echo "Running exp 6..."
bash "$AE_DIR/exps/exp6.sh" || exit $?

echo "Running exp 7..."
bash "$AE_DIR/exps/exp7.sh" || exit $?

echo "Running exp 8..."
bash "$AE_DIR/exps/exp8.sh" || exit $?