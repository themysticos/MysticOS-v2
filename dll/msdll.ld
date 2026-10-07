/* msdll.ld - linker script для .msdll (MysticOS 2).
 *
 * База DLL-региона — 0x64000000 (PDE[25], identity во всех задачах).
 * Все RVA в заголовке .msdll — от этой базы.
 *
 * Секции:
 *   .text          - код + rodata
 *   .data          - данные
 *   .bss           - неинициализированные данные
 *   .msdll_exports - таблица экспортов (msdll_export_t[])
 *   .msdll_imports - таблица импортов (msdll_import_t[])
 *   .msdll_relocs  - таблица релокаций (msdll_reloc_t[])
 *
 * Заголовок .msdll (64 байта) НЕ здесь — его добавляет пост-линкер
 * tools/msdll_pack.py, который читает символы _msdll_*_start/_end.
 */
ENTRY(_start)
SECTIONS
{
    . = 0x64000000;

    .text : {
        *(.text*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    .msdll_ropool : {
        *(.msdll_ropool)
    }

    .msdll_exports : {
        _msdll_exports_start = .;
        KEEP(*(.msdll_exports))
        _msdll_exports_end = .;
    }

    .msdll_imports : {
        _msdll_imports_start = .;
        KEEP(*(.msdll_imports))
        _msdll_imports_end = .;
    }

    .msdll_relocs : {
        _msdll_relocs_start = .;
        KEEP(*(.msdll_relocs))
        _msdll_relocs_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.rel.*)
    }
}
