#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

# This script is for formating all C and CXX code within this project

C_FILES=`find . -type f -name "*.c"`
CXX_FILES=`find . -type f -name "*.cpp"`
H_FILES=`find . -type f -name "*.h"`

ALL_FILES="${C_FILES} ${CXX_FILES} ${H_FILES}"

clang-format -i ${ALL_FILES}
