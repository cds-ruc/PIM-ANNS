#!/bin/bash

SCRIPT_DIR="$(readlink -f "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")")"

export UPMEM_HOME="${SCRIPT_DIR}"
echo "Setting UPMEM_HOME to ${UPMEM_HOME} and updating PATH/LD_LIBRARY_PATH/PYTHONPATH"
export LD_LIBRARY_PATH="${UPMEM_HOME}/lib${LD_LIBRARY_PATH+:$LD_LIBRARY_PATH}"
export PATH="${UPMEM_HOME}/bin:${PATH}"

PYTHON_RELATIVE_PATH=local/lib/python3.10/dist-packages

export PYTHONPATH="${UPMEM_HOME}/${PYTHON_RELATIVE_PATH}${PYTHONPATH+:$PYTHONPATH}"

_DEFAULT_BACKEND=$1

if [[ -n "${_DEFAULT_BACKEND}" ]];
then
    echo "Setting default backend to ${_DEFAULT_BACKEND} in UPMEM_PROFILE_BASE"
    export UPMEM_PROFILE_BASE=backend=${_DEFAULT_BACKEND}
fi
