# Skein Shell

Графическая оболочка оконного уровня для Skein OS. Wayland-композитор,
написанный с нуля на C++ поверх библиотеки Aquamarine (DRM/KMS, libinput,
сессии). Не использует код Hyprland, wlroots, KDE и GNOME: только
Aquamarine, hyprutils и собственный протокольный слой.

Оболочка делает всё сама, без внешних демонов: обои, тени, заголовки окон,
переключатель окон, буфер обмена — внутри одного процесса.

## Состояние (v0.1)

Работает:

- Встроенные обои (PNG/JPEG, cover/fit, билинейная фильтрация),
  смена через `colibryctl wallpaper` или конфиг.
- Плавающие окна по умолчанию; тайлинг master/stack включается явно.
- Рабочие столы 1-10, правила: fullscreen подразумевает тайлинг,
  fullscreen-при-открытии уходит на пустой стол, новые окна на тайловом
  столе открываются тайловыми.
- Серверные заголовки окон в стиле Windows: иконка, текст (FreeType),
  кнопки min/max/close с hover/press, drag за заголовок, двойной клик —
  максимизация. Drag за полностью прозрачные пиксели контента.
- Тень под окнами: размытие силуэта со скруглением, кэшируется по размеру.
- Курсор из темы XCursor, с экспоненциальным сглаживанием движения.
- Damage-driven рендеринг: перерисовывается только изменённое;
  кадр 60 Гц при коммитах каждые 17 мс.
- Буфер обмена (копипаст между клиентами).
- foreign-toplevel (панели видят окна) и wl_output.
- Полное управление с клавиатуры и с мыши (`docs/input-parity.md`).
- IPC: `colibryctl version | exit | wallpaper &lt;path&gt;`.

Ограничения:

- Композитинг на CPU. Для полупрозрачных тяжёлых приложений следующий
  этап — GPU-рендер (EGL).
- Нет layer-shell (панели waybar не закрепить по краю), dmabuf
  (клиенты на shm), XWayland, dnd, жестов тачпада, рендера попапов.
- Один монитор; мультимонитор — косметическая поддержка.

## Зависимости

Arch: `aquamarine hyprutils wayland wayland-protocols libdrm mesa pixman
xkbcommon libpng libjpeg-turbo libxcursor freetype2`

Терминал для автостарта: `foot` или `ghostty` (иначе задайте `terminal=`
или `COLIBRY_TERMINAL`).

## Сборка

    ./build.sh --clean          # debug
    ./build.sh --clean --asan   # с санитайзерами

## Установка

    sudo ./install.sh                    # /usr/local, конфиг из assets/
    sudo ./install.sh --prefix /usr

NixOS: `services.colibry.enable = true;` (модуль в `nixos/module.nix`).

## Запуск

Из TTY, одной командой — терминал откроется сам:

    colibry

Выход: Ctrl+Alt+Del или `colibryctl exit`. Переключение TTY:
Ctrl+Alt+F1..F12. Если запуск не показал кадр, процесс завершится сам
(watchdog 30 с), консоль вернёт seatd.

## Конфигурация

`/etc/colibry/colibry.toml`, поверх читается
`~/.config/colibry/colibry.toml`:

    wallpaper = "@PREFIX@/share/colibry/wallpapers/default.png"
    wallpaper_fit = "cover"        # или "fit"
    cursor_theme = "Adwaita"
    cursor_size = 24
    terminal = "ghostty"
    follow_mouse = 1
    shadow_enabled = 1
    shadow_offset_x = 0
    shadow_offset_y = 12
    shadow_radius = 24
    shadow_rounding = 8
    shadow_opacity = 0.45
    titlebar_height = 32
    titlebar_button_width = 46
    titlebar_font = "/path/to/font.ttf"
    monitors = ["eDP-1,1920x1200,1", ",preferred,1.5"]
    bind = ["SUPER+B=exec:firefox"]

## Клавиши (по умолчанию)

    SUPER+Return, Alt+F1   терминал
    SUPER+T                ghostty
    SUPER+E                nautilus
    SUPER+D                launcher (fuzzel || wofi)
    SUPER+Tab              переключатель окон (Enter/Esc)
    SUPER+J / SUPER+K      фокус следующее/предыдущее окно
    SUPER+C, SUPER+Q, Alt+F4  закрыть
    SUPER+F                fullscreen        SUPER+M  свернуть
    SUPER+Shift+T          float/tiling    SUPER+Shift+ стрелки — ресайз
    SUPER+стрелки          перемещение
    SUPER+1..0 / SUPER+Shift+1..0   столы / отправить окно
    SUPER+drag             двигать окно за любую точку
    Ctrl+Alt+Del           выход

Мышь: drag за заголовок, за края/углы — ресайз, кнопки в заголовке,
focus-follows-mouse (отключается `follow_mouse = 0`).

## Тестирование

    tests/e2e/smoke.sh ./build/colibry   # из TTY или nested
    ./build/colibry & sleep 2
    python3 tests/fuzz_ipc.py $(pgrep -n colibry)
    ./build/colibryctl exit

## Репозиторий

    src/core/       координатор, цикл событий, выход (сцена + damage)
    src/protocol/   Wayland: display, shm, surface, xdg-shell, seat,
                    clipboard, wl_output, foreign-toplevel
    src/desktop/    окна, раскладка, заголовки, тень, курсор, бинды,
                    switcher, drag
    src/wallpaper/  загрузка и отрисовка обоев
    src/render/     линейные буферы (GBM)
    src/config/     TOML-конфиг
    src/ipc/        сокет и colibryctl
    tests/          smoke и fuzz
    protocols/      вендоренные XML
    assets/         обои и шаблон colibry.toml
    nixos/          пакет и модуль

Дорожная карта после v0.1: GPU-композитинг, layer-shell, dmabuf,
XWayland, dnd, жесты, мультимонитор, попапы.

Лицензия: BSD-3-Clause.
