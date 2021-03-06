set(TARGET_NAME libbitcoin_wallet)

file(GLOB SOURCES "*.cpp")
file(GLOB PARSE_ENCRYPTED_SOURCES "parse_encrypted_keys/*.cpp")
file(GLOB HEADERS "${LIBBITCOIN_INCLUDE_DIR}/wallet/*.hpp")

add_library(${TARGET_NAME} OBJECT ${SOURCES} ${PARSE_ENCRYPTED_SOURCES} ${HEADERS})

if(MSVC)
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4244")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4245")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4505")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4458")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4996")
    target_compile_options(${TARGET_NAME} PUBLIC "/wd4251")
elseif(APPLE)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-missing-braces)
elseif(ANDROID)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-const-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-tautological-constant-compare)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-variable)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-mismatched-tags)
else()
    target_compile_options(${TARGET_NAME} PUBLIC -fpermissive)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-reorder)
    target_compile_options(${TARGET_NAME} PUBLIC -Wno-unused-but-set-variable)
endif()

if(Boost_FOUND)
    target_link_libraries(${TARGET_NAME} boost)
endif()
