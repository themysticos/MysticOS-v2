# windd.py - запись raw-образа на физический диск в Windows (аналог dd).
#
# Использование:
#   python windd.py --list                        # список физических дисков
#   python windd.py IMAGE.img 1                   # записать образ на PHYSICALDRIVE1
#   python windd.py IMAGE.img 1 --yes             # без подтверждения
#
# ВАЖНО: запись УНИЧТОЖАЕТ все данные на диске. Скрипт пишет ТОЛЬКО на
# физические диски (\\.\PHYSICALDRIVEn), НЕ на буквы томов (C:, D:).
#
# Windows не даёт писать в сектора СМОНТИРОВАННОГО тома (WinError 5).
# Поэтому перед записью все тома на диске БЛОКИРУЮТСЯ и РАЗМОНТИРУЮТСЯ
# (FSCTL_LOCK_VOLUME + FSCTL_DISMOUNT_VOLUME) и держатся открытыми до конца.
# Требуется запуск от АДМИНИСТРАТОРА.

import sys
import os
import struct
import ctypes
import ctypes.wintypes as wt
import argparse

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)

GENERIC_READ  = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_SHARE_READ  = 0x00000001
FILE_SHARE_WRITE = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
FILE_BEGIN = 0

IOCTL_DISK_GET_LENGTH_INFO = 0x0007405C
IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS = 0x00560000
FSCTL_LOCK_VOLUME     = 0x00090018
FSCTL_UNLOCK_VOLUME   = 0x0009001C
FSCTL_DISMOUNT_VOLUME = 0x00090020
# Удалить таблицу разделов на уровне драйвера (аналог diskpart clean).
IOCTL_DISK_DELETE_DRIVE_LAYOUT = 0x0007C100
# Обновить сведения о геометрии/разделах после изменения.
IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140

kernel32.CreateFileW.restype = wt.HANDLE
kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
                                 wt.DWORD, wt.DWORD, wt.HANDLE]
kernel32.DeviceIoControl.restype = wt.BOOL
kernel32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, ctypes.c_void_p, wt.DWORD,
                                     ctypes.c_void_p, wt.DWORD,
                                     ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
kernel32.WriteFile.restype = wt.BOOL
kernel32.SetFilePointerEx.restype = wt.BOOL
kernel32.CloseHandle.restype = wt.BOOL
kernel32.FindFirstVolumeW.restype = wt.HANDLE
kernel32.FindFirstVolumeW.argtypes = [wt.LPWSTR, wt.DWORD]
kernel32.FindNextVolumeW.restype = wt.BOOL
kernel32.FindNextVolumeW.argtypes = [wt.HANDLE, wt.LPWSTR, wt.DWORD]
kernel32.FindVolumeClose.argtypes = [wt.HANDLE]


def err(msg):
    e = ctypes.get_last_error()
    print(f"[ОШИБКА] {msg} (WinError {e}: {ctypes.FormatError(e).strip()})")
    sys.exit(1)


def list_physical_drives():
    import subprocess
    ps = ("Get-CimInstance Win32_DiskDrive | "
          "Select-Object Index,Model,Size,InterfaceType,MediaType | "
          "Format-Table -AutoSize")
    print("Физические диски (выбирай по Index):")
    try:
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", ps],
            text=True, stderr=subprocess.STDOUT)
        print(out)
    except Exception as e:
        print(f"  (не удалось получить список: {e})")
        print("  Используй 'diskmgmt.msc' вручную.")


def open_disk(path):
    # path: r'\\.\PHYSICALDRIVE1'
    # ВАЖНО: открываем ЭКСКЛЮЗИВНО (share=0) — иначе Windows разрешает
    # открыть диск, но блокирует запись в сектора тома (WinError 5).
    h = kernel32.CreateFileW(
        path, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE or h is None:
        # fallback: с разделяемым доступом (может понадобиться, если том занят).
        h = kernel32.CreateFileW(
            path, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE or h is None:
        err(f"не удалось открыть '{path}' (нужны права администратора?)")
    return h


def lock_dismount_disk(handle):
    """Заблокировать и размонтировать САМ физический диск.
    Это главный способ снять защиту Windows (WinError 5): том, привязанный к
    диску, перестаёт удерживать сектора."""
    ret = wt.DWORD(0)
    kernel32.FlushFileBuffers(handle)
    lk = kernel32.DeviceIoControl(handle, FSCTL_LOCK_VOLUME, None, 0, None, 0,
                                  ctypes.byref(ret), None)
    dm = kernel32.DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, None, 0, None, 0,
                                  ctypes.byref(ret), None)
    print(f"  lock={'OK' if lk else 'fail'} dismount={'OK' if dm else 'fail'}")


def delete_drive_layout(handle):
    """Удалить таблицу разделов напрямую через IOCTL (аналог diskpart
    clean, но без внешнего процесса и без 'отказано в доступе')."""
    ret = wt.DWORD(0)
    ok = kernel32.DeviceIoControl(
        handle, IOCTL_DISK_DELETE_DRIVE_LAYOUT, None, 0, None, 0,
        ctypes.byref(ret), None)
    kernel32.DeviceIoControl(
        handle, IOCTL_DISK_UPDATE_PROPERTIES, None, 0, None, 0,
        ctypes.byref(ret), None)
    print(f"  delete drive layout: {'OK' if ok else 'fail'}")
    return ok


def diskpart_clean(disk_num):
    """Очистить диск через diskpart (стирает таблицу разделов и тома).
    После этого том исчезает и raw-запись на диск идёт без блокировок
    (WinError 5). ВНИМАНИЕ: уничтожает ВСЕ данные на диске."""
    import tempfile, subprocess
    script = f"select disk {disk_num}\nclean\n"
    tf = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    tf.write(script)
    tf.close()
    try:
        r = subprocess.run(["diskpart", "/s", tf.name],
                           capture_output=True, text=True)
        ok = ("successfully cleaned" in r.stdout.lower()
              or r.returncode == 0)
        print(f"  diskpart clean disk {disk_num}: "
              f"{'OK' if ok else 'fail'}")
        if not ok:
            print("  diskpart output:", r.stdout.strip()[-200:])
        return ok
    finally:
        try:
            os.unlink(tf.name)
        except Exception:
            pass


def disk_size(handle):
    buf = ctypes.c_ulonglong(0)
    returned = wt.DWORD(0)
    ok = kernel32.DeviceIoControl(
        handle, IOCTL_DISK_GET_LENGTH_INFO, None, 0,
        ctypes.byref(buf), ctypes.sizeof(buf),
        ctypes.byref(returned), None)
    return buf.value if ok else None


def all_volumes():
    """Список логических дисков (букв), например том E.
    ВАЖНО: GUID-пути из FindFirstVolume на практике НЕ открываются для
    записи, а буквы (например E:) — открываются и размонтируются успешно."""
    import string
    kernel32.GetLogicalDrives.restype = wt.DWORD
    mask = kernel32.GetLogicalDrives()
    vols = []
    bs = chr(92)
    for i, ch in enumerate(string.ascii_uppercase):
        if mask & (1 << i):
            vols.append(bs + bs + "." + bs + ch + ":")
    return vols


def volume_disks(vol_path):
    """Номера физических дисков, которым принадлежит том."""
    # Открываем с РЕАЛЬНЫМИ правами (иначе DeviceIoControl падает и список
    # томов пуст, как было раньше).
    h = kernel32.CreateFileW(vol_path, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             None, OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE or h is None:
        return []
    try:
        buf = ctypes.create_string_buffer(1024)
        ret = wt.DWORD(0)
        ok = kernel32.DeviceIoControl(
            h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, None, 0,
            buf, 1024, ctypes.byref(ret), None)
        if not ok:
            return []
        num = struct.unpack_from('<I', buf.raw, 0)[0]
        disks = []
        for i in range(num):
            off = 8 + i * 24   # VOLUME_DISK_EXTENTS: 4+4 pad, then 24-byte extents
            disks.append(struct.unpack_from('<I', buf.raw, off)[0])
        return disks
    finally:
        kernel32.CloseHandle(h)


def lock_and_dismount(vol_path):
    """Заблокировать и размонтировать том. Возвращает handle (держать открытым)."""
    h = kernel32.CreateFileW(vol_path, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             None, OPEN_EXISTING, 0, None)
    if h == INVALID_HANDLE_VALUE or h is None:
        return None
    ret = wt.DWORD(0)
    kernel32.FlushFileBuffers(h)
    # Блокировка (если том занят — может не удаться, но пробуем).
    kernel32.DeviceIoControl(h, FSCTL_LOCK_VOLUME, None, 0, None, 0,
                             ctypes.byref(ret), None)
    # Размонтирование — снимает точку монтирования, Windows отпускает сектора.
    kernel32.DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, None, 0, None, 0,
                             ctypes.byref(ret), None)
    return h


def write_image(handle, img_path, total_size):
    CHUNK = 1024 * 1024
    written = 0
    kernel32.SetFilePointerEx(handle, ctypes.c_longlong(0), None, FILE_BEGIN)
    wrote = wt.DWORD(0)
    with open(img_path, "rb") as f:
        while True:
            data = f.read(CHUNK)
            if not data:
                break
            buf = ctypes.create_string_buffer(data, len(data))
            ok = kernel32.WriteFile(handle, buf, len(data),
                                    ctypes.byref(wrote), None)
            if not ok:
                err(f"ошибка записи на смещении {written}")
            if wrote.value != len(data):
                err(f"записано {wrote.value} из {len(data)} байт")
            written += wrote.value
            pct = written * 100 // total_size if total_size else 0
            sys.stdout.write(f"\r  записано {written//1024} КиБ / "
                             f"{total_size//1024} КиБ ({pct}%)")
            sys.stdout.flush()
    print()
    return written


def main():
    ap = argparse.ArgumentParser(
        description="Запись raw-образа на физический диск Windows (аналог dd).")
    ap.add_argument("image", nargs="?")
    ap.add_argument("disk", nargs="?")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--no-clean", action="store_true",
                    help="не вызывать diskpart clean (если диск уже очищен)")
    args = ap.parse_args()

    if args.list or not args.image or not args.disk:
        list_physical_drives()
        if not args.list:
            print("\nПример: python windd.py mysticos2.img 1 --yes")
        return

    if not os.path.isfile(args.image):
        err(f"файл образа не найден: {args.image}")
    img_size = os.path.getsize(args.image)

    d = args.disk.strip()
    if d.isdigit():
        disk_num = int(d)
        d = r"\\.\PHYSICALDRIVE" + d
    elif d.upper().startswith(r"\\.\PHYSICALDRIVE"):
        disk_num = int(d[len(r"\\.\PHYSICALDRIVE"):])
    else:
        err("указывай ФИЗИЧЕСКИЙ диск (номер или \\\\.\\PHYSICALDRIVEn), "
            "не букву тома C:/D:")

    print(f"Образ:  {args.image} ({img_size} байт)")
    print(f"Диск:   {d}")
    print("\n!!! ВСЕ ДАННЫЕ НА ЭТОМ ДИСКЕ БУДУТ УНИЧТОЖЕНЫ !!!")
    if not args.yes:
        if input("Продолжить? введи ЗАПИСАТЬ: ").strip() != "ЗАПИСАТЬ":
            print("Отменено.")
            return

    # --- ШАГ 1: размонтировать ВСЕ тома диска (главное: снять том E:). ---
    # Тома открываем с реальными правами, иначе DeviceIoControl не сработает.
    held = []
    print("Размонтирую тома диска...")
    for vol in all_volumes():
        if disk_num in volume_disks(vol):
            vh = lock_and_dismount(vol)
            if vh:
                held.append(vh)
                print(f"  размонтирован: {vol}")
    if not held:
        print("  (томов не найдено)")

    # --- ШАГ 2: открыть диск и удалить таблицу разделов. ---
    h = open_disk(d)
    try:
        if not args.no_clean:
            print("Удаляю таблицу разделов диска...")
            delete_drive_layout(h)

        # --- ШАГ 3: блокировать и размонтировать сам диск. ---
        lock_dismount_disk(h)
        size = disk_size(h)
        if size:
            print(f"Размер носителя: {size} байт ({size//1024//1024} МиБ)")
            if img_size > size:
                err(f"образ ({img_size}) больше носителя ({size})")
        n = write_image(h, args.image, img_size)
        kernel32.FlushFileBuffers(h)
        print(f"Готово: записано {n} байт на {d}")
    finally:
        kernel32.CloseHandle(h)
        for vh in held:
            ret = wt.DWORD(0)
            kernel32.DeviceIoControl(vh, FSCTL_UNLOCK_VOLUME, None, 0, None, 0,
                                     ctypes.byref(ret), None)
            kernel32.CloseHandle(vh)


if __name__ == "__main__":
    main()
