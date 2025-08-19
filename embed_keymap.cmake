# Convert keymap.png to C header file for embedding
# Usage: cmake -DPNG_FILE=path/to/keymap.png -DHEADER_FILE=path/to/output.h -P embed_keymap.cmake

if(NOT PNG_FILE OR NOT HEADER_FILE)
    message(FATAL_ERROR "Usage: cmake -DPNG_FILE=<input.png> -DHEADER_FILE=<output.h> -P embed_keymap.cmake")
endif()

if(NOT EXISTS "${PNG_FILE}")
    message(FATAL_ERROR "PNG file does not exist: ${PNG_FILE}")
endif()

# Read the PNG file as binary
file(READ "${PNG_FILE}" PNG_DATA HEX)

# Convert hex string to C array format
string(LENGTH "${PNG_DATA}" DATA_LENGTH)
math(EXPR BYTE_COUNT "${DATA_LENGTH} / 2")

# Generate C array
set(C_ARRAY "")
set(BYTES_PER_LINE 12)
set(BYTE_INDEX 0)

while(BYTE_INDEX LESS BYTE_COUNT)
    # Start new line
    if(BYTE_INDEX GREATER 0)
        string(APPEND C_ARRAY "\n")
    endif()
    string(APPEND C_ARRAY "    ")
    
    # Add up to BYTES_PER_LINE bytes per line
    set(LINE_BYTE_COUNT 0)
    while(LINE_BYTE_COUNT LESS BYTES_PER_LINE AND BYTE_INDEX LESS BYTE_COUNT)
        math(EXPR HEX_START "${BYTE_INDEX} * 2")
        string(SUBSTRING "${PNG_DATA}" ${HEX_START} 2 HEX_BYTE)
        
        string(APPEND C_ARRAY "0x${HEX_BYTE}")
        
        math(EXPR NEXT_BYTE "${BYTE_INDEX} + 1")
        if(NEXT_BYTE LESS BYTE_COUNT)
            string(APPEND C_ARRAY ", ")
        endif()
        
        math(EXPR BYTE_INDEX "${BYTE_INDEX} + 1")
        math(EXPR LINE_BYTE_COUNT "${LINE_BYTE_COUNT} + 1")
    endwhile()
endwhile()

# Generate header file
get_filename_component(PNG_NAME "${PNG_FILE}" NAME_WE)
string(TOUPPER "${PNG_NAME}" PNG_NAME_UPPER)

file(WRITE "${HEADER_FILE}" "/* Generated from ${PNG_FILE} */\n")
file(APPEND "${HEADER_FILE}" "#ifndef KEYMAP_EMBEDDED_H\n")
file(APPEND "${HEADER_FILE}" "#define KEYMAP_EMBEDDED_H\n\n")
file(APPEND "${HEADER_FILE}" "static const unsigned char embedded_keymap_data[] = {\n")
file(APPEND "${HEADER_FILE}" "${C_ARRAY}\n")
file(APPEND "${HEADER_FILE}" "};\n\n")
file(APPEND "${HEADER_FILE}" "static const unsigned int embedded_keymap_size = ${BYTE_COUNT};\n\n")
file(APPEND "${HEADER_FILE}" "#endif /* KEYMAP_EMBEDDED_H */\n")

message(STATUS "Generated ${HEADER_FILE} from ${PNG_FILE} (${BYTE_COUNT} bytes)")