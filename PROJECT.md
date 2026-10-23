# Colibry — подробное описание проекта и план разработки

## 1. Видение проекта

**Colibry** — собственная графическая оболочка (композитор) для Linux/NixOS, написанная с нуля на C++ и использующая в качестве рендер-бэкенда библиотеки экосистемы Hyprland (в первую очередь **Aquamarine**). Проект не является форком Hyprland: исходники Hyprland используются исключительно как читаемый референс поведения, а его кодовая база (~97% «чужой» логики: тайлинг-менеджер Hyprland, анимации, IPC-совместимость, куча протоколов и фич) в Colibry не переносится.

**Философия:**
- Минимализм кода: каждый модуль маленький, читаемый, переписываемый одним человеком.
- Всё необходимое — внутри композитора: обои, тени, заголовки, оконный switcher. Ноль внешних «обоичных» демонов (swaybg/swww/hyprpaper как процесс не нужен).
- Паритет ввода: любое действие выполняется и мышью, и клавиатурой.
- Окна по умолчанию плавающие; тайлинг — режим, а не дефолт.

## 2. Технологический стек

| Компонент | Библиотека | Роль |
|---|---|---|
| Бэкенд | **aquamarine** | DRM/KMS, libinput, сессии/VT, GBM/EGL-аллокация буферов, Wayland/X11/nested-бэкенды, damage-tracking. Полная замена wlroots — это и есть «рендер-движок» от Hyprland-проекта в виде библиотеки |
| Утилиты | **hyprutils** | Vector2D/Mat3x3, hyprsignal (событийная шина), os (fd, ФС, сокеты), логирование |
| Графика | **hyprgraphics** | Цвета, EGL-бойлерплейт, загрузка текстур |
| Конфиг | **hyprlang** | Парсер `colibry.conf`, live-reload |
| Курсоры | **hyprcursor** | Темы курсоров |
| Протоколы | **hyprwayland-scanner** | Генерация C++-обёрток Wayland-протоколов из XML — снимает ~70% бойлерплейта протокольного слоя |
| Системный слой | wayland-server, libinput, libudev, libgbm, EGL/GLESv2, libdisplay-info, libffi | Фиксируется через Nix |

**Явно не используется:** исходный код Hyprland (только как референс), wlroots, swaybg/swww/hyprpaper.

**Честная оговорка:** отдельной «рендер-библиотеки» у Hyprland нет — его рендерер внутри композитора. Поэтому в Colibry пишется плоский 2D-композитор на GLES (~1–1.5k строк: текстурные квады, damage, Kawase-blur). Это не «движок», а тонкий слой поверх EGL-поверхностей, которые предоставляет aquamarine. Вся тяжёлая работа (буферы, KMS, ввод, сессии) — в библиотеках.

## 3. Архитектура

```
colibry/
├── flake.nix                        # devShell, overlay, packages
├── nixos/
│   ├── module.nix                   # services.colibry.*
│   └── package.nix                  # meson-сборка
├── meson.build
├── src/
│   ├── main.cpp                     # вход, инициализация, цикл
│   ├── core/
│   │   ├── Compositor.{hpp,cpp}     # глобальное состояние, порядок init
│   │   ├── EventLoop.*              # цикл событий (fd/signal/timers)
│   │   └── Output.*                 # обёртка над aquamarine::COutput
│   ├── protocol/                    # Wayland-протокольный слой
│   │   ├── compositor.*             # wl_compositor, wl_region, wl_surface
│   │   ├── shells/xdg-shell.*       # xdg_wm_base, toplevel, popup
│   │   ├── seat.*  keyboard.*  pointer.*  touch.*
│   │   ├── datadev.*                # wl_data_device (clipboard/dnd)
│   │   ├── dmabuf.*                 # linux-dmabuf (zero-copy клиенты)
│   │   ├── kde-decoration.*         # принудительный SSD
│   │   ├── foreign-toplevel.*       # для панелей/launcher'ов/скриншотеров
│   │   └── layer-shell.*            # панели, обои сторонних виджетов
│   ├── render/
│   │   ├── Renderer.{hpp,cpp}       # GLES: quad batch, текстуры, damage, blur
│   │   ├── passes/
│   │   │   ├── WallpaperPass.*      # обои (треб. 1)
│   │   │   ├── ShadowPass.*         # тени (треб. 2)
│   │   │   └── TitleBarPass.*       # заголовки (треб. 4)
│   │   └── shaders/
│   │       ├── quad.glsl
│   │       ├── kawase_blur.glsl
│   │       └── shadow_gradient.glsl
│   ├── desktop/
│   │   ├── Window.{hpp,cpp}         # геометрия, float-first, fullscreen-правила
│   │   ├── Workspace.{hpp,cpp}      # WS, логика открытия окон
│   │   ├── LayoutFloating.*         # ~200 строк: геометрия плавающих окон
│   │   ├── LayoutTiling.*           # ~300 строк: master/stack
│   │   ├── TitleBar.{hpp,cpp}       # кнопки, hit-rects, drag-зона
│   │   └── Shadow.{hpp,cpp}         # параметры тени окна
│   ├── input/
│   │   ├── InputManager.*           # курсор, hit-test, drag/resize
│   │   ├── Keyboard.*               # бинды, focus-мод
│   │   ├── KeybindManager.*         # разбор биндов из конфига
│   │   └── Gesture.*                # touchpad
│   ├── config/ConfigManager.*       # hyprlang, live-reload по сигналу
│   ├── ipc/IPC.*                    # colibryctl, сокет $XDG_RUNTIME_DIR/colibry.sock
│   ├── wallpaper/
│   │   └── WallpaperEngine.{hpp,cpp}# загрузка, preload, кроссфейд
│   └── xwayland/                    # отдельная фаза, после v0.1
├── configs/default/colibry.conf
├── assets/wallpapers/
├── docs/
│   ├── architecture.md
│   ├── input-parity.md              # таблица мышь ↔ клавиатура
│   ├── protocols.md                 # статус реализации протоколов
│   └── adr/                         # архитектурные решения
└── tests/e2e/                       # скриптовые сценарии в nested-сессии
```

Поток кадра: `libinput (через aquamarine) → обработка ввода → обновление геометрии окон → рендер-пасы: Wallpaper → Shadow (под окнами) → окна + TitleBar → курсор → page-flip через aquamarine`.

## 4. Спецификация требований

### Требование 1 — встроенные обои
- `WallpaperEngine`: загрузка png/jpeg в текстуру (hyprgraphics), preload списка файлов, кэш LRU, per-monitor wallpaper, масштабирование fill/fit/stretch/tile.
- `WallpaperPass` — первый пас рендера, рисуется всегда (отдельный слой, не поверх окон).
- IPC: `colibryctl wallpaper set <monitor> <path>`, `preload <path>`, `list`.
- Кроссфейд при смене (две текстуры, lerp alpha).
- Внешних процессов нет.

### Требование 2 — константная реалистичная тень
- `ShadowPass` после WallpaperPass, до окон: силуэт каждого видимого окна → оффсет → double Kawase blur → градиент прозрачности к краю.
- «Константная»: тень не участвует в анимациях окна, не исчезает, параметры фиксированы конфигом, включена по умолчанию.
- Конфиг `colibry.shadow { enabled, color, opacity, offset, blur_radius, corner_rounding }`.

### Требование 3 — плавающие окна по умолчанию
- `Window` создаётся в режиме `floating`. Тайлинг включается явно (windowrule `tiling`, клавиша `SUPER+T`, правила этапа «оконная логика»).
- При закрытии окна никакого автоперераспределения «дырки» нет (окна свободно лежат, Z-order сверху).
- Раскладка тайлинга — своя простая master/stack.

### Требование 4 — заголовки как в Windows
- SSD-декоратор: полоса высотой `colibry.titlebar.height` (дефолт 32) над окном, стилизуемая.
- Слева: иконка приложения + заголовок. Справа: кнопки «свернуть / развернуть / закрыть» (порядок и набор конфигурируются), состояния hover/press, hit-rects.
- Drag за заголовок — move окна; двойной клик по заголовку — toggle fullscreen (по правилам треб. 8).
- Принудительный SSD всем клиентам через протокол `org_kde_plasma_server_decoration` + отклонение клиентских CSD-запросов.
- Конфиг: высота, размер кнопок, цвета (active/inactive), шрифт, скругление.

### Требование 5 — пустое место окна ведёт себя как заголовок
- В `InputManager::processMouseDown(BTN_LEFT)`: координата внутри окна → проверить, есть ли на этом месте непрозрачный пиксель поверхности/сабсёрфейса (hit-test по регионам поверхности, а не по bounding box).
- Если регион пуст (прозрачная область окна, зазор между сабсёрфейсами) → начать move окна, как за заголовок.
- Fallback: XWayland-окна определяют прозрачность иначе — для них перетаскивание только за заголовок (фиксируется в документации).

### Требование 6 — полное управление мышью ИЛИ клавиатурой
- Мышь: focus-follows-mouse, drag за заголовок/пустоту/края, кнопки заголовка, колесо по кнопками WS, края окон для resize.
- Клавиатура (дефолтные бинды, всё переопределяется):
  - `SUPER+Enter` — терминал, `SUPER+D` — launcher, `SUPER+Q` — закрыть.
  - `SUPER+J/K` или `SUPER+H/L` — цикл фокуса по окнам; `SUPER+Tab` — оконный switcher (оверлей-рендер в композиторе).
  - `SUPER+стрелки` — перемещение окна; `SUPER+Shift+стрелки` — ресайз (шаг конфигурируется).
  - `SUPER+1..9,0` — рабочие столы; `SUPER+Shift+1..9` — отправить окно.
  - `SUPER+F` — fullscreen; `SUPER+M` — минимизировать (сворачивание в switcher); `SUPER+T` — тайлинг-режим.
- `docs/input-parity.md` — таблица: каждое мышиное действие ↔ клавиатурный эквивалент. Приёмка: день работы без мыши по чек-листу.

### Требование 7 — без показа размера при ресайзе
- Индикатор «WxH» просто не реализуется. Resize — чистая геометрия окна.

### Требование 8 — fullscreen → тайловый режим
- `Window::setFullscreen(true)`: запоминается предыдущий режим (float/tiling), окно переводится в тайлинг, разворачивается на весь монитор.
- `setFullscreen(false)`: возврат в запомненный режим и геометрию.

### Требование 9 — открывающиеся на весь экран окна → отдельный рабочий стол
- В обработчике map `xdg_toplevel`: если запрошено fullscreen/maximized ИЛИ windowrule `fullscreen` → создать/выбрать пустой WS (первый свободный или новый), переместить окно туда, сфокусироваться на нём.

### Требование 10 — на WS с тайловым окном новые окна открываются тайловыми
- В map-обработчике: целевой WS содержит ≥1 тайловое окно → новое окно тайловое; иначе float. Правило работает в обе стороны (float-окна вставать в тайл-схему не обязаны).

## 5. Конфигурация по умолчанию (`configs/default/colibry.conf`)

```ini
monitor=,preferred,auto,1

colibry {
    wallpaper {
        preload = ~/Pictures/wall1.png
        wallpaper = ,~/Pictures/wall1.png        # пустой монитор = все
        transition = crossfade
    }
    shadow {
        enabled = true
        color = rgba(0,0,0,0.55)
        offset = 0, 10
        blur_radius = 32
        corner_rounding = 8
    }
    titlebar {
        height = 32
        buttons = minimize,maximize,close
        show_icon = true
        show_title = true
        font = Sans 11
    }
    windows {
        default_mode = floating
        fullscreen_mode = tiling
        fullscreen_new_workspace = true
        drag_empty_area = true
        resize_step = 20
    }
}

input {
    kb_layout = us,ru
    kb_options = grp:alt_shift_toggle
    follow_mouse = 1
}

bind = SUPER, Return, exec, kitty
bind = SUPER, D, exec, fuzzel
bind = SUPER, Q, killactive
bind = SUPER, F, fullscreen
bind = SUPER, M, minimize
bind = SUPER, T, togglefloating
bind = SUPER, J, colibry:focus, next
bind = SUPER, K, colibry:focus, prev
bind = SUPER, Tab, colibry:switcher
bind = SUPER, left,  colibry:move, -20 0
bind = SUPER, right, colibry:move, 20 0
bind = SUPER, up,    colibry:move, 0 -20
bind = SUPER, down,  colibry:move, 0 20
bind = SUPER SHIFT, left,  colibry:resize, -20 0
bind = SUPER SHIFT, right, colibry:resize, 20 0
bind = SUPER SHIFT, up,    colibry:resize, 0 -20
bind = SUPER SHIFT, down,  colibry:resize, 0 20
bind = SUPER, 1, workspace, 1
bind = SUPER SHIFT, 1, movetoworkspace, 1
bindm = SUPER, mouse:272, movewindow
bindm = SUPER, mouse:273, resizewindow
```

## 6. Этапы разработки

### Этап 0 — Скелет и Nix-инфраструктура (1 неделя)
- Репозиторий `colibrywm/colibry`, meson-скелет, CI (сборка + clang-tidy).
- `flake.nix`: devShell (meson, ninja, clang, pkg-config, все dev-пакеты стека), overlay экспортирующий `colibry`.
- `main.cpp`: инициализация aquamarиевого бэкенда (сначала nested), пустой вывод (очистка экрана в цвет), корректный page-flip.
- `nixos/module.nix`: `services.colibry.enable`, `settings` (генерация конфига), интеграция с greetd.
- **Приёмка:** `nix build` → сессия `colibry` запускается из greetd, показывает залитый фон, выходит без падений.

### Этап 1 — Протокольный минимум (4–5 недель) ⚠️ критический путь
- `wl_compositor`/`wl_surface`/`wl_region`: commit-цикл, damage, буферы wl_shm → текстуры.
- `xdg_wm_base`, `xdg_toplevel`, `xdg_popup`: map/unmap, геометрия, протокольные состояния (activate/configure), close.
- `wl_seat`, `wl_keyboard`, `wl_pointer`: раскладки (xkbcommon), focus, энтер/лив поверхностей.
- Генерация обёрток через hyprwayland-scanner; ведение `docs/protocols.md` (статус каждого протокола).
- Отладка строго в nested-бэкенде (запуск в окне под существующим композитором).
- **Приёмка:** weston-terminal и любое GTK-приложение открываются, ввод работает, окна перемещаются SUPER+drag.

### Этап 2 — Обои и рендер-пасы (2 недели)
- `WallpaperEngine` + `WallpaperPass`, preload/кэш, fill/fit/stretch/tile, кроссфейд.
- IPC `colibryctl wallpaper set/preload/list`.
- Конфиг-секция `colibry.wallpaper`.
- **Приёмка:** треб. 1 закрыто — смена обоев без мерцания, без внешних процессов.

### Этап 3 — Оконная логика (2 недели)
- Float-first (треб. 3), рабочие столы, focus-follows-mouse, клавиатурные бинды (базовый набор).
- Реализация треб. 8, 9, 10 в map/fullscreen-логике. LayoutTiling (master/stack).
- E2E-сценарии в `tests/e2e/`: скрипты, гоняемые в nested-сессии CI.
- **Приёмка:** все сценарии треб. 3, 8, 9, 10 проходят; треб. 7 выполнено по построению.

### Этап 4 — Заголовки как в Windows (2–3 недели)
- `TitleBar` + `TitleBarPass`: кнопки (hit-rects, hover/press, min/max/close), иконка, текст (FreeType), drag-зона, double-click → fullscreen.
- Принудительный SSD (kde-decoration), конфиг `colibry.titlebar`.
- **Приёмка:** любое Wayland-окно получает идентичный заголовок; кнопки работают мышью и с клавиатуры (через focus/switcher).

### Этап 5 — Константная тень (1–2 недели)
- `ShadowPass`: double Kawase blur, градиент, оффсет; конфиг `colibry.shadow`.
- Перф-тест: 20 окон, целевые 60/144 FPS; авто-деградация blur на слабом железе.
- **Приёмка:** треб. 2 закрыто; тень не «прыгает», не участвует в анимациях.

### Этап 6 — Перетаскивание за пустоту + полное клавиатурное управление (2 недели)
- Hit-test пустых регионов в `InputManager` (треб. 5, Wayland-окна; XWayland — fallback в документации).
- Focus-мод, оконный switcher (оверлей-рендер), полный набор биндов, touchpad-жесты.
- Составление и прохождение `docs/input-parity.md` (треб. 6).
- **Приёмка:** день работы без мыши по чек-листу; день работы только мышью.

### Этап 7 — Протокольный хвост (2–3 недели)
- `linux-dmabuf` (zero-copy), `wl_data_device` (clipboard/dnd), `wlr-foreign-toplevel`, `layer-shell`.
- Релизная полировка: дефолтный конфиг, обои, логотип, desktop-файл, документация.
- **Приёмка:** Firefox (Wayland), файловый менеджер, панель waybar работают.

### Этап 8 — Стабилизация и релиз v0.1 (1–2 недели)
- E2E в CI (GitHub Actions + Nix, nested), ASan-билд, фаззинг IPC-сокета.
- Релиз-ноуты, анонс.

**Итого: ~15–19 недель одним разработчиком.** XWayland — отдельная фаза после v0.1.

## 7. Риски и митигация

| Риск | Вероятность | Митигация |
|---|---|---|
| Протокольный слой разрастается (главный риск) | высокая | hyprwayland-scanner; чтение aquamarine-референсов; протоколы реализуются строго по мере необходимости, ведётся protocols.md |
| Дрейф версий библиотек | средняя | flake.lock, всё через Nix, обновления — явным коммитом после CI |
| Перф теней/blur на слабом железе | средняя | параметры blur, авто-деградация по FPS-метрике |
| CSD-приложения ломаются с принудительным SSD | средняя | kde-decoration + per-app windowrules `ssdxwayland`/`allows_csd` |
| Объём работ одним человеком | высокая | жёсткие критерии приёмки на каждом этапе; v0.1 без XWayland и избыточных протоколов |

## 8. Критерии готовности v0.1
1. Все 10 требований закрыты и покрыты E2E-сценариями.
2. Сборка и запуск одной командой: `nix run github:colibrywm/colibry` + модуль `services.colibry.enable = true`.
3. Паритет ввода задокументирован и пройден.
4. Стабильность: неделя ежедневного использования без крашей рендера/ввода.
