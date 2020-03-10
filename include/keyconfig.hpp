/**
 * Copyright (C) 2020 diwo
 * 
 * This file is part of Tesla Menu.
 * 
 * Tesla Menu is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Tesla Menu is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Tesla Menu.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <tesla.hpp>
#include <chrono>

class KeyConfig : public tsl::Gui {
public:
    KeyConfig() : m_state(STATE_CAPTURE), m_combo(0), m_comboConfirm(0) {}

    tsl::elm::Element* createUI() override;

    bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput, JoystickPosition leftJoyStick, JoystickPosition rightJoyStick) override;

protected:
    void handleExitMenuInput(u64 keysDown, u64 keysHeld);
    void handleCaptureInput(u64 keysDown, u64 keysHeld);
    void handleCaptureReleaseInput(u64 keysDown, u64 keysHeld);
    void handleConfirmInput(u64 keysDown, u64 keysHeld);
    void handleDoneInput(u64 keysDown, u64 keysHeld);

private:
    enum ComboConfigState { STATE_CAPTURE, STATE_CAPTURE_RELEASE, STATE_CONFIRM, STATE_DONE };

    void filterAllowedKeys(u64 &keysDown, u64 &keysHeld);
    std::string comboGlyphs(u64 combo);

private:
    static const std::chrono::milliseconds CAPTURE_HOLD_MILLI;

    ComboConfigState m_state;
    u64 m_combo;
    u64 m_comboConfirm;
    std::chrono::steady_clock::time_point m_lastPlusTime;
    std::chrono::steady_clock::time_point m_lastComboTime;
    std::chrono::milliseconds m_remainHoldMilli;
};
