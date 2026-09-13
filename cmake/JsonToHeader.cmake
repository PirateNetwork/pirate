# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Wrap a .json test vector into a C array header.
#
#   cmake -DJSON_IN=<file>.json -DHEADER_OUT=<file>.json.h -P JsonToHeader.cmake

get_filename_component(_stem ${JSON_IN} NAME_WE)
file(READ ${JSON_IN} _hex HEX)

# Convert the whole hex string in one pass: "5b0a" -> "0x5b, 0x0a, ".
# This must not be done with a while() loop over string(SUBSTRING): each
# iteration re-expands the entire multi-megabyte hex string, which is quadratic
# and does not finish on inputs the size of sighash.json (3.9 MB).
string(REGEX REPLACE "(..)" "0x\\1, " _body "${_hex}")
# Break lines every 16 bytes so the generated header stays readable.
string(REGEX REPLACE "((0x.., ){16})" "\\1\n" _body "${_body}")

file(WRITE ${HEADER_OUT}
"namespace json_tests{
static unsigned const char ${_stem}[] = {
${_body}
};};
")
