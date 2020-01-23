/**
 * Copyright (C) 2020 WerWolv
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <tesla.hpp>
#include <filesystem>

#include <switch/nro.h>
#include <switch/nacp.h>

#include "logo_bin.h"

constexpr int Module_OverlayLoader  = 348;

constexpr Result ResultSuccess      = MAKERESULT(0, 0);
constexpr Result ResultParseError   = MAKERESULT(Module_OverlayLoader, 1);

std::pair<Result, std::string> getOverlayName(std::string filePath) {
    FILE *file = fopen(filePath.c_str(), "r");

    NroHeader header;
    NroAssetHeader assetHeader;
    NacpStruct nacp;

    fseek(file, sizeof(NroStart), SEEK_SET);
    if (fread(&header, sizeof(NroHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "" };
    }

    fseek(file, header.size, SEEK_SET);
    if (fread(&assetHeader, sizeof(NroAssetHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "" };
    }

    fseek(file, header.size + assetHeader.nacp.offset, SEEK_SET);
    if (fread(&nacp, sizeof(NacpStruct), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "" };
    }
    
    fclose(file);

    return { ResultSuccess, std::string(nacp.lang[0].name, sizeof(nacp.lang[0].name)) };
}

static tsl::element::Frame *rootFrame = nullptr;

static void rebuildUI() {
    auto *overlayList = new tsl::element::List();  
    auto header = new tsl::element::CustomDrawer(0, 0, 100, FB_WIDTH, [](u16 x, u16 y, tsl::Screen *screen){
        screen->drawRGBA8Image(20, 20, 84, 31, logo_bin);
        screen->drawString(envGetLoaderInfo(), false, 20, 68, 15, tsl::a(0xFFFF));
    });

    auto noOverlaysError = new tsl::element::CustomDrawer(0, 0, 100, FB_WIDTH, [](u16 x, u16 y, tsl::Screen *screen) {
        screen->drawString("\uE150", false, (FB_WIDTH - 90) / 2, 300, 90, tsl::a(0xFFFF));
        screen->drawString("No Overlays found!", false, 105, 380, 25, tsl::a(0xFFFF));
    });

    u16 entries = 0;
    for (const auto &entry : std::filesystem::directory_iterator("sdmc:/switch/.overlays")) {
        if (entry.path().filename() == "ovlmenu.ovl")
            continue;

        auto [result, name] = getOverlayName(entry.path());
        if (result != ResultSuccess)
            continue;

        auto *listEntry = new tsl::element::ListItem(name);
        listEntry->setClickListener([entry, entries](s64 key) {
            if (key & KEY_A) {
                tsl::Overlay::setNextLoadPath(entry.path().c_str());
                
                tsl::Gui::closeGui();
                return true;
            }

            return false;
        });

        overlayList->addItem(listEntry);
        entries++;
    }

    rootFrame->addElement(header);

    if (entries == 0) {
        rootFrame->addElement(noOverlaysError);
        delete overlayList;
    } else {
        rootFrame->addElement(overlayList);
    }
}

class GuiMain : public tsl::Gui {
public:
    GuiMain() { }
    ~GuiMain() { }

    virtual tsl::Element* createUI() {
        rootFrame = new tsl::element::Frame();
        
        rebuildUI();

        return rootFrame;
    }

    virtual void update() { }
};

class TeslaOverlay : public tsl::Overlay {
public:
    TeslaOverlay() { }
    ~TeslaOverlay() { }

    tsl::Gui* onSetup() { return new GuiMain(); }
    void onOverlayShow(tsl::Gui *gui) override { 
        if (rootFrame != nullptr) {
            rootFrame->clear();
            rebuildUI();
            rootFrame->layout();
        }
        
        tsl::Gui::playIntroAnimation();
    }

    void onOverlayHide(tsl::Gui *gui) override { tsl::Gui::playOutroAnimation(); }
    void onOverlayExit(tsl::Gui *gui) override { tsl::Gui::playOutroAnimation(); }
};


tsl::Overlay *overlayLoad() {
    return new TeslaOverlay();
}