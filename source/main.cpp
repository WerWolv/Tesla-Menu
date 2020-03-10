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

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
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

class TeslaMenuFrame : public tsl::elm::OverlayFrame {
public:
    TeslaMenuFrame() : OverlayFrame("", "") {}
    ~TeslaMenuFrame() {}

    virtual void draw(tsl::gfx::Renderer *renderer) override {
        OverlayFrame::draw(renderer);

        renderer->drawBitmap(20, 20, 84, 31, logo_bin);
        renderer->drawString(envGetLoaderInfo(), false, 20, 68, 15, renderer->a(0xFFFF));
    }
};

static TeslaMenuFrame *rootFrame = nullptr;

static void rebuildUI() {
    auto *overlayList = new tsl::elm::List();  

    auto noOverlaysError = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->drawString("\uE150", false, (tsl::cfg::FramebufferWidth - 90) / 2, 300, 90, renderer->a(0xFFFF));
        renderer->drawString("No Overlays found!", false, 105, 380, 25, renderer->a(0xFFFF));
    });

    u16 entries = 0;
    for (const auto &entry : std::filesystem::directory_iterator("sdmc:/switch/.overlays")) {
        if (entry.path().filename() == "ovlmenu.ovl")
            continue;

        if (entry.path().extension() != ".ovl")
            continue;

        auto [result, name] = getOverlayName(entry.path());
        if (result != ResultSuccess)
            continue;

        auto *listEntry = new tsl::elm::ListItem(name);
        listEntry->setClickListener([entry, entries](s64 key) {
            if (key & KEY_A) {
                tsl::setNextOverlay(entry.path());
                
                tsl::Overlay::get()->close();
                return true;
            }

            return false;
        });

        overlayList->addItem(listEntry);
        entries++;
    }

    if (entries == 0) {
        rootFrame->setContent(noOverlaysError);
        delete overlayList;
    } else {
        rootFrame->setContent(overlayList);
    }
}

class GuiMain : public tsl::Gui {
public:
    GuiMain() { }
    ~GuiMain() { }

    tsl::elm::Element* createUI() override {
        rootFrame = new TeslaMenuFrame();
        
        rebuildUI();

        return rootFrame;
    }
};

class OverlayTeslaMenu : public tsl::Overlay {
public:
    OverlayTeslaMenu() { }
    ~OverlayTeslaMenu() { }

    void initServices() override {
        ASSERT_FATAL(fsdevMountSdmc());
    }

    void exitServices() override {
        fsdevUnmountAll();
    }

    void onShow() override { 
        if (rootFrame != nullptr) {
            tsl::Overlay::get()->getCurrentGui()->removeFocus();
            rebuildUI();
            rootFrame->invalidate();
            tsl::Overlay::get()->getCurrentGui()->requestFocus(rootFrame, tsl::FocusDirection::None);
        }
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<GuiMain>();
    }
};


int main(int argc, char **argv) {
    return tsl::loop<OverlayTeslaMenu, tsl::impl::LaunchFlags::None>(argc, argv);
}
