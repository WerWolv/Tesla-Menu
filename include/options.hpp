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
#include <keyconfig.hpp>

class OptionsMenu : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto frame = new tsl::elm::OverlayFrame("Tesla Options", "");

        auto list = new tsl::elm::List();

        auto keysConfig = new tsl::elm::ListItem("Change combo keys");
        keysConfig->setClickListener([](u64 keys) {
            if (keys & KEY_A) {
                tsl::changeTo<KeyConfig>();
                return true;
            }
            return false;
        });

        list->addItem(keysConfig);
        frame->setContent(list);

        return frame;
    }
};
