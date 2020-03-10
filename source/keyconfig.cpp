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

#include <keyconfig.hpp>

#include <sstream>
#include <iomanip>

using namespace std::chrono_literals;

const std::chrono::milliseconds KeyConfig::CAPTURE_HOLD_MILLI = 2500ms;

tsl::elm::Element* KeyConfig::createUI() {
    auto COLOR_DEFAULT = a({ 0xE, 0xE, 0xE, 0xF });
    auto COLOR_HEADING = a({ 0xC, 0xC, 0xC, 0xF });
    auto COLOR_PENDING = a({ 0x8, 0x7, 0x1, 0xF });
    auto COLOR_SUCCESS = a({ 0x0, 0xA, 0x2, 0xF });
    auto COLOR_INFO = a({ 0x8, 0x8, 0x8, 0xF });

    return new tsl::elm::CustomDrawer([=](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->fillScreen(a({ 0x0, 0x0, 0x0, 0xD }));

        u32 line_y = 50;
        renderer->drawString("Change Combo Keys", false, 20, line_y, 30, a(0xFFFF));

        line_y = 110;
        renderer->drawString("Input new combo:", false, 20, line_y, 24, COLOR_HEADING);
        auto comboColor = m_state == STATE_CAPTURE ? COLOR_PENDING : COLOR_SUCCESS;
        renderer->drawString(comboGlyphs(m_combo).c_str(), false, 20, line_y + 50, 32, comboColor);
        if (m_state == STATE_CAPTURE && m_combo) {
            double freq = (CAPTURE_HOLD_MILLI - m_remainHoldMilli).count() / 1000;
            double offset = sin(2 * 3.14 * freq) * 10 * m_remainHoldMilli / CAPTURE_HOLD_MILLI;
            renderer->drawString("Hold still...", false, 20, line_y + 100 - offset, 24, COLOR_INFO);
            if (m_remainHoldMilli < CAPTURE_HOLD_MILLI - 500ms) {
                std::stringstream timerStream;
                timerStream << std::fixed << std::setprecision(1) << ((float)m_remainHoldMilli.count() / 1000) << "s";
                renderer->drawString(timerStream.str().c_str(), false, 160, line_y + 100, 30, COLOR_INFO);
            }
        } else if (m_state > STATE_CAPTURE) {
            renderer->drawString("Got it!", false, 20, line_y + 100, 24, COLOR_DEFAULT);
        }

        line_y = 270;
        if (m_state >= STATE_CAPTURE_RELEASE) {
            renderer->drawString("Input combo again to confirm:", false, 20, line_y, 24, COLOR_HEADING);
            if (m_state == STATE_CAPTURE_RELEASE) {
                renderer->drawString("(Release the buttons first)", false, 20, line_y + 40, 20, COLOR_INFO);
            }
            else if (m_state >= STATE_CONFIRM) {
                auto comboConfirmColor = m_state == STATE_CONFIRM ? COLOR_PENDING : COLOR_SUCCESS;
                renderer->drawString(comboGlyphs(m_comboConfirm).c_str(), false, 20, line_y + 50, 32, comboConfirmColor);
            }
        }

        line_y = 400;
        if (m_state >= STATE_DONE) {
            renderer->drawString("We're done!", false, 20, line_y, 24, COLOR_DEFAULT);
            renderer->drawString("Press \uE0A0 to continue", false, 20, line_y + 60, 24, COLOR_DEFAULT);
        }

        line_y = tsl::cfg::FramebufferHeight - 20;
        if (m_state < STATE_DONE)
            renderer->drawString("Press \uE0B5 twice to exit", false, 170, line_y, 24, COLOR_INFO);
    });
}

bool KeyConfig::handleInput(
    u64 keysDown, u64 keysHeld, touchPosition touchInput,
    JoystickPosition leftJoyStick, JoystickPosition rightJoyStick)
{
    handleExitMenuInput(keysDown, keysHeld);

    switch (m_state) {
        case STATE_CAPTURE:
            handleCaptureInput(keysDown, keysHeld);
            return true;
        case STATE_CAPTURE_RELEASE:
            handleCaptureReleaseInput(keysDown, keysHeld);
            return true;
        case STATE_CONFIRM:
            handleConfirmInput(keysDown, keysHeld);
            return true;
        case STATE_DONE:
            handleDoneInput(keysDown, keysHeld);
            return true;
        default:
            return false;
    }
}

void KeyConfig::handleExitMenuInput(u64 keysDown, u64 keysHeld) {
    if (keysDown & KEY_PLUS) {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMilli = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPlusTime);
        if (m_state < STATE_DONE && elapsedMilli < 500ms)
            tsl::goBack();
        else
            m_lastPlusTime = now;
    }
}

void KeyConfig::handleCaptureInput(u64 keysDown, u64 keysHeld) {
    filterAllowedKeys(keysDown, keysHeld);
    auto now = std::chrono::steady_clock::now();
    if (keysHeld != m_combo) {
        m_lastComboTime = now;
        m_remainHoldMilli = CAPTURE_HOLD_MILLI;
        m_combo = keysHeld;
    }
    else if (m_combo) {
        m_remainHoldMilli = CAPTURE_HOLD_MILLI -
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastComboTime);
        if (m_remainHoldMilli <= 0ms) {
            m_state = STATE_CAPTURE_RELEASE;
        }
    }
}

void KeyConfig::handleCaptureReleaseInput(u64 keysDown, u64 keysHeld) {
    filterAllowedKeys(keysDown, keysHeld);
    if (!keysHeld)
        m_state = STATE_CONFIRM;
}

void KeyConfig::handleConfirmInput(u64 keysDown, u64 keysHeld) {
    filterAllowedKeys(keysDown, keysHeld);
    m_comboConfirm = keysHeld;
    // Confirmation must be exact match
    if ((keysHeld == m_combo) && (keysDown & m_combo)) {
        tsl::impl::updateCombo(m_combo);
        m_state = STATE_DONE;
    }
}

void KeyConfig::handleDoneInput(u64 keysDown, u64 keysHeld) {
    if (keysDown & KEY_A)
        tsl::goBack();
}

void KeyConfig::filterAllowedKeys(u64 &keysDown, u64 &keysHeld) {
    u64 allowedKeys = 0;
    for (auto &keyInfo : tsl::impl::KEYS_INFO) {
        allowedKeys |= keyInfo.key;
    }
    keysDown &= allowedKeys;
    keysHeld &= allowedKeys;
}

std::string KeyConfig::comboGlyphs(u64 combo) {
    std::string str;
    for (auto &keyInfo : tsl::impl::KEYS_INFO) {
        if (combo & keyInfo.key) {
            str.append(keyInfo.glyph).append(" ");
        }
    }
    return str;
}
