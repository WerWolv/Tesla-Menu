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
#include <tuple>

#include <switch/nro.h>
#include <switch/nacp.h>

#include "logo_bin.h"

constexpr int Module_OverlayLoader  = 348;

constexpr Result ResultSuccess      = MAKERESULT(0, 0);
constexpr Result ResultParseError   = MAKERESULT(Module_OverlayLoader, 1);

std::tuple<Result, std::string, std::string> getOverlayInfo(std::string filePath) {
    FILE *file = fopen(filePath.c_str(), "r");

    NroHeader header;
    NroAssetHeader assetHeader;
    NacpStruct nacp;

    fseek(file, sizeof(NroStart), SEEK_SET);
    if (fread(&header, sizeof(NroHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }

    fseek(file, header.size, SEEK_SET);
    if (fread(&assetHeader, sizeof(NroAssetHeader), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }

    fseek(file, header.size + assetHeader.nacp.offset, SEEK_SET);
    if (fread(&nacp, sizeof(NacpStruct), 1, file) != 1) {
        fclose(file);
        return { ResultParseError, "", "" };
    }
    
    fclose(file);

    return { ResultSuccess, std::string(nacp.lang[0].name, std::strlen(nacp.lang[0].name)), std::string(nacp.display_version, std::strlen(nacp.display_version)) };
}

static tsl::elm::HeaderOverlayFrame *rootFrame = nullptr;

static void rebuildUI() {
    auto *overlayList = new tsl::elm::List();  
    auto *header = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
        const u8 *logo = logo_bin;

        for (s32 y1 = 0; y1 < 31; y1++) {
            for (s32 x1 = 0; x1 < 84; x1++) {
                const tsl::Color color = { static_cast<u8>(logo[3] >> 4), static_cast<u8>(logo[2] >> 4), static_cast<u8>(logo[1] >> 4), static_cast<u8>(logo[0] >> 4) };
                renderer->setPixelBlendSrc(20 + x1, 20 + y1, renderer->a(color));
                logo += 4;
            }
        }

        renderer->drawString(envGetLoaderInfo(), false, 20, 68, 15, renderer->a(0xFFFF));
    });

    auto noOverlaysError = new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
        renderer->drawString("\uE150", false, (tsl::cfg::FramebufferWidth - 90) / 2, 300, 90, renderer->a(tsl::style::color::ColorText));
        renderer->drawString("No Overlays found!", false, 105, 380, 25, renderer->a(tsl::style::color::ColorText));
        renderer->drawString("Place your .ovl files in /switch/.overlays", false, 82, 410, 15, renderer->a(tsl::style::color::ColorDescription));
    });

    u16 entries = 0;
    for (const auto &entry : std::filesystem::directory_iterator("sdmc:/switch/.overlays")) {
        if (entry.path().filename() == "ovlmenu.ovl")
            continue;

        if (entry.path().extension() != ".ovl")
            continue;

        auto [result, name, version] = getOverlayInfo(entry.path());
        if (result != ResultSuccess)
            continue;

        auto *listEntry = new tsl::elm::ListItem(name);
        listEntry->setValue(version, true);
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

    rootFrame->setHeader(header);

    if (entries == 0) {
        rootFrame->setContent(noOverlaysError);
        delete overlayList;
    } else {
        rootFrame->setContent(overlayList);
    }
}

#include "netload.hpp"

constexpr const SocketInitConfig sockConf = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size = 0x800,
    .tcp_rx_buf_size = 0x800,
    .tcp_tx_buf_max_size = 0x25000,
    .tcp_rx_buf_max_size = 0x25000,

    .udp_tx_buf_size = 0,
    .udp_rx_buf_size = 0,

    .sb_efficiency = 1,

    .num_bsd_sessions = 0,
    .bsd_service_type = BsdServiceType_Auto,
};

class GuiNetloader : public tsl::Gui {
private:
    Thread thread;
    std::string err = "Good";
    Result rc = 0;

public:
    GuiNetloader() {
        tsl::hlp::doWithSmSession([this] {
            if (R_SUCCEEDED(rc = socketInitialize(&sockConf)))
                rc = nifmInitialize(NifmServiceType_User);
            std::sprintf(err.data(), "0x%x", rc);
        });
        threadCreate(&thread, netloader::task, nullptr, nullptr, 0x1000, 0x2C, -2);
        threadStart(&thread);
    }

    ~GuiNetloader() {
        netloader::signalExit();

        threadWaitForExit(&thread);
        threadClose(&thread);

        nifmExit();
        socketExit();
    }

    tsl::elm::Element* createUI() override {
        auto *rootFrame = new tsl::elm::OverlayFrame(envGetLoaderInfo(), "Netloader");
        
        rootFrame->setContent(
            new tsl::elm::CustomDrawer(
                [this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                    renderer->drawString(err.c_str(), false, 100, 400, 20, tsl::style::color::ColorText);
                }
            )
        );

        return rootFrame;
    }

    void update() {
        static int counter = 0;
        if ((counter++ % 5) != 0)
            return;

        netloader::State state = {};
        netloader::getState(&state);

        if (state.launch_app && !state.activated) {
            netloader::setNext();

            tsl::Overlay::get()->close();

            err = "Finished!";
        }
        if (state.errormsg[0])
            err = state.errormsg;
    }
};

class GuiMain : public tsl::Gui {
public:
    GuiMain() { }
    ~GuiMain() { }

    tsl::elm::Element* createUI() override {
        rootFrame = new tsl::elm::HeaderOverlayFrame();
        
        rebuildUI();

        return rootFrame;
    }

    bool handleInput(u64 keysDown, u64 keysHeld, touchPosition touchInput, JoystickPosition leftJoyStick, JoystickPosition rightJoyStick) {
        if (keysDown & KEY_Y) {
            tsl::changeTo<GuiNetloader>();
            return true;
        }
        return false;
    }
};

class OverlayTeslaMenu : public tsl::Overlay {
public:
    OverlayTeslaMenu() { }
    ~OverlayTeslaMenu() { }
    void initServices() {
        fsdevMountSdmc();
    }
    void exitServices() {
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
