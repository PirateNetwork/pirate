#!/bin/bash

# Function to convert a JSON file to a C++ header
convert_json_to_header() {
    local input_file=$1
    local output_file="${input_file}.h"
    local var_name=$(basename "$input_file" .json)
    
    echo "// Auto-generated file from $input_file" > "$output_file"
    echo "#ifndef TEST_DATA_${var_name^^}_H" >> "$output_file"
    echo "#define TEST_DATA_${var_name^^}_H" >> "$output_file"
    echo "" >> "$output_file"
    echo "namespace json_tests {" >> "$output_file"
    echo "static const char ${var_name}[] = {" >> "$output_file"
    
    # Convert file content to hex bytes
    xxd -i < "$input_file" >> "$output_file"
    
    echo "};" >> "$output_file"
    echo "} // namespace json_tests" >> "$output_file"
    echo "" >> "$output_file"
    echo "#endif // TEST_DATA_${var_name^^}_H" >> "$output_file"
}

# Convert merkle_roots.json
convert_json_to_header "merkle_roots.json" 