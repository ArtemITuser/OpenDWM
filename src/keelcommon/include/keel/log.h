/*
 * KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
 * Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <windows.h>

namespace keel {

enum class Level { Trace, Info, Warn, Error };

void LogInit(const wchar_t* component);
void Log(Level level, const wchar_t* fmt, ...);

}

#define KEEL_TRACE(...) ::keel::Log(::keel::Level::Trace, __VA_ARGS__)
#define KEEL_INFO(...)  ::keel::Log(::keel::Level::Info,  __VA_ARGS__)
#define KEEL_WARN(...)  ::keel::Log(::keel::Level::Warn,  __VA_ARGS__)
#define KEEL_ERROR(...) ::keel::Log(::keel::Level::Error, __VA_ARGS__)
