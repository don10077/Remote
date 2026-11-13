# Единый набор предупреждений компилятора для целей проекта.
#
# Подключается только к нашим целям (не к сторонним библиотекам),
# поэтому чужой код не засоряет вывод сборки.

# harmony_set_warnings(<target>)
#   Включает строгие предупреждения для указанной цели.
#   При HARMONY_WARNINGS_AS_ERRORS=ON предупреждения становятся ошибками (удобно для CI).
function(harmony_set_warnings target)
    if(MSVC)
        set(warnings /W4 /permissive-)
        if(HARMONY_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    else()
        # GCC и Clang (в том числе MinGW из MSYS2)
        # -Wnull-dereference оставляем только без оптимизации: на -O2 GCC строит
        # ложные выводы из инлайна (доступ к d_ptr в Qt, operator== через this),
        # и предупреждение срабатывает на совершенно корректном коде.
        set(warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Woverloaded-virtual
            "$<$<CONFIG:Debug>:-Wnull-dereference>"
            -Wimplicit-fallthrough
        )
        if(HARMONY_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
