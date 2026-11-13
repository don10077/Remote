# libFuzzer-цели для разборщиков бинарных форматов (только Clang, HARMONY_FUZZ=ON).
# Сборка и прогон: cmake --preset fuzz && cmake --build --preset fuzz && fuzz/run.sh build/fuzz 60
#
# Инструментирование кода ядра (-fsanitize=fuzzer-no-link, ASan, UBSan) задаёт пресет fuzz;
# здесь к целям добавляется только сам libFuzzer (main и драйвер).

function(harmony_add_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${ARGN})
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    harmony_set_warnings(${name})
endfunction()

harmony_add_fuzzer(fuzz_ezhex    ezhexFuzz.cpp    harmony::core)
harmony_add_fuzzer(fuzz_zip      zipFuzz.cpp      harmony::core)
harmony_add_fuzzer(fuzz_irtables irTablesFuzz.cpp harmony::core)
harmony_add_fuzzer(fuzz_usbnet   usbnetFuzz.cpp   harmony::transport)
