.PHONY: default generate install test lint

PIP     ?= pip3
PYTEST  ?= pytest
PYLINT  ?= pylint
PYGEN   ?= ctypesgen

UPMEM_HOME ?= /usr
C_API_DIR ?= $(UPMEM_HOME)/include/dpu
C_API_HEADERS := $(C_API_DIR)/dpu.h $(C_API_DIR)/dpu_error.h

OPTS ?=

default:
	@echo 'No default make target'

generate:
	@$(PYGEN) -o dpu/ffi.py -I $(C_API_DIR) -ldpu $(C_API_HEADERS) --strip-build-path=$(C_API_DIR) --no-undefs --header-template=dpu/ffi_header.txt

install:
	@$(PIP) install -r requirements.txt

test:
	@$(PYTEST) --cov=dpu --cov-branch $(OPTS)

lint:
	@$(PYLINT) dpu --ignore=ffi.py --exit-zero $(OPTS)
