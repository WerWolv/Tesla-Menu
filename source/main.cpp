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
#include "dir_iterator.hpp"
#include "logo_bin.h"

#include <cstdlib>
#include <cstring>
#include <tesla.hpp>

constexpr int Module_OverlayLoader = 348;

constexpr Result ResultSuccess = MAKERESULT(0, 0);
constexpr Result ResultParseError = MAKERESULT(Module_OverlayLoader, 1);

tsl::elm::ListItem *parseOverlay(FsFile *file) {
    NroHeader header;
    NroAssetHeader assetHeader;
    static NacpStruct nacp;

    u64 bytesRead;

    /* Read NRO Header. */
    Result rc = fsFileRead(file, sizeof(NroStart), &header, sizeof(NroHeader), FsReadOption_None, &bytesRead);
    if (R_FAILED(rc) || bytesRead != sizeof(NroHeader))
        return nullptr;

    /* Read asset header. */
    rc = fsFileRead(file, header.size, &assetHeader, sizeof(NroAssetHeader), FsReadOption_None, &bytesRead);
    if (R_FAILED(rc) || bytesRead != sizeof(NroAssetHeader))
        return nullptr;

    /* Read NACP. */
    rc = fsFileRead(file, header.size + assetHeader.nacp.offset, &nacp, sizeof(NacpStruct), FsReadOption_None, &bytesRead);
    if (R_FAILED(rc) || bytesRead != sizeof(NacpStruct))
        return nullptr;

    auto *listEntry = new tsl::elm::ListItem(nacp.lang[0].name);
    listEntry->setValue(nacp.display_version, true);
    return listEntry;
}

constexpr const char *const ovlPath = "/switch/.overlays/";
constexpr size_t OvlPathLength = std::strlen(ovlPath);

class FileBrowser : public tsl::elm::List {
  private:
    char m_cwdPath[FS_MAX_PATH];
    bool m_empty = true;

  public:
    FileBrowser() : List() {
        std::strcpy(this->m_cwdPath, ovlPath);
    }

    virtual bool onClick(u64 keys) override {
        if (keys & KEY_B) {
            if (this->m_cwdPath[OvlPathLength - 1] != '\0') {
                this->upCwd();
                return true;
            }
        }
        return List::onClick(keys);
    }

    void setCwd(const char *path) {
        std::strcpy(this->m_cwdPath, path);
    }

    bool isEmpty() { return this->m_empty; }

    Result scanCwd() {
        this->List::clear();
        this->m_empty = true;

        /* Open sd card filesystem. */
        FsFileSystem sdmc;
        R_TRY(fsOpenSdCardFileSystem(&sdmc));
        tsl::hlp::ScopeGuard fs_guard([&sdmc] { fsFsClose(&sdmc); });

        /* Mount directory. */
        FsDir cwd;
        R_TRY(fsFsOpenDirectory(&sdmc, this->m_cwdPath, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &cwd));
        tsl::hlp::ScopeGuard cwd_guard([&cwd] { fsDirClose(&cwd); });

        /* Show absolute folder path. */
        this->List::addItem(new tsl::elm::CategoryHeader(this->m_cwdPath, true));

        std::vector<tsl::elm::ListItem *> folders, files;

        /* Iterate over folder. */
        for (const FsDirectoryEntry &entry : FsDirIterator(cwd)) {
            if (entry.type == FsDirEntryType_Dir) {
                /* Add directory entries. */
                auto *item = new tsl::elm::ListItem(entry.name);
                item->setClickListener([this, item](u64 down) -> bool {
                    if (down & KEY_A) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                        std::snprintf(this->m_cwdPath, FS_MAX_PATH, "%s%s/", this->m_cwdPath, item->getText().c_str());
#pragma GCC diagnostic pop
                        tsl::Overlay::get()->getCurrentGui()->removeFocus();
                        this->scanCwd();
                        return true;
                    }
                    return false;
                });
                folders.push_back(item);
                continue;
            } else {
                /* Check extension. */
                if (strcasecmp(entry.name + std::strlen(entry.name) - 4, ".ovl") != 0)
                    continue;

                /* Ignore overlay menu. */
                if (std::strcmp(entry.name, "ovlmenu.ovl") == 0)
                    continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
                static char pathBuffer[FS_MAX_PATH];
                std::snprintf(pathBuffer, FS_MAX_PATH, "%s%s", this->m_cwdPath, entry.name);
#pragma GCC diagnostic pop

                /* Open file. */
                FsFile file;
                Result rc = fsFsOpenFile(&sdmc, pathBuffer, FsOpenMode_Read, &file);
                if (R_FAILED(rc))
                    continue;
                tsl::hlp::ScopeGuard file_guard([&file] { fsFileClose(&file); });

                auto *listEntry = parseOverlay(&file);
                if (listEntry == nullptr)
                    continue;

                files.push_back(listEntry);
                std::string absolutePath = std::string("sdmc:") + pathBuffer;
                listEntry->setClickListener([absolutePath](s64 key) {
                    if (key & KEY_A) {
                        tsl::setNextOverlay(absolutePath);

                        tsl::Overlay::get()->close();
                        return true;
                    }

                    return false;
                });
            }
        }
        if (folders.size() == 0 && files.size() == 0) {
            this->m_empty = true;
            return ResultSuccess;
        }

        auto ListItemTextCompare = [](const tsl::elm::ListItem *_lhs, const tsl::elm::ListItem *_rhs) -> bool {
            return strcasecmp(_lhs->getText().c_str(), _rhs->getText().c_str()) < 0;
        };

        if (folders.size() > 0) {
            std::sort(folders.begin(), folders.end(), ListItemTextCompare);
            for (auto element : folders)
                this->List::addItem(element);
        }
        if (files.size() > 0) {
            this->List::addItem(new tsl::elm::CategoryHeader("Files"));
            std::sort(files.begin(), files.end(), ListItemTextCompare);
            for (auto element : files)
                this->List::addItem(element);
        }
        this->m_empty = false;
        return ResultSuccess;
    }

    void upCwd() {
        size_t length = std::strlen(this->m_cwdPath);
        if (length <= 1)
            return;

        for (size_t i = length - 2; i >= 0; i--) {
            if (this->m_cwdPath[i] == '/') {
                this->m_cwdPath[i + 1] = '\0';
                tsl::Overlay::get()->getCurrentGui()->removeFocus();
                this->scanCwd();
                return;
            }
        }
    }
};

class GuiMain : public tsl::Gui {
  private:
    tsl::elm::HeaderOverlayFrame *rootFrame = nullptr;

  public:
    GuiMain() {
        rootFrame = new tsl::elm::HeaderOverlayFrame();
    }
    ~GuiMain() {}

    tsl::elm::Element *createUI() override {
        rootFrame->setHeader(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            const u8 *logo = logo_bin;

            for (s32 y1 = 0; y1 < 31; y1++) {
                for (s32 x1 = 0; x1 < 84; x1++) {
                    const tsl::gfx::Color color = {static_cast<u8>(logo[3] >> 4), static_cast<u8>(logo[2] >> 4), static_cast<u8>(logo[1] >> 4), static_cast<u8>(logo[0] >> 4)};
                    renderer->setPixelBlendSrc(20 + x1, 20 + y1, renderer->a(color));
                    logo += 4;
                }
            }

            renderer->drawString(envGetLoaderInfo(), false, 20, 68, 15, renderer->a(tsl::style::color::ColorText));
        }));

        this->refreshGui();

        return rootFrame;
    }

    void refreshGui() {
        this->removeFocus();
        auto browser = std::make_unique<FileBrowser>();
        Result rc = browser->scanCwd();

        if (R_FAILED(rc)) {
            /* Something went wrong. */
            char resultBuffer[0x10];
            std::snprintf(resultBuffer, 0x10, "2%03X-%04X", R_MODULE(rc), R_DESCRIPTION(rc));
            rootFrame->setContent(new tsl::elm::CustomDrawer([resultBuffer](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
                renderer->drawString("\uE150", false, (tsl::cfg::FramebufferWidth - 90) / 2, 300, 90, renderer->a(tsl::style::color::ColorText));
                renderer->drawString("Failed to scan cwd!", false, 105, 380, 25, renderer->a(tsl::style::color::ColorText));
                renderer->drawString(resultBuffer, false, 82, 410, 15, renderer->a(tsl::style::color::ColorDescription));
            }));
        } else if (browser->isEmpty()) {
            /* Folder is empty */
            rootFrame->setContent(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, u16 x, u16 y, u16 w, u16 h) {
                renderer->drawString("\uE150", false, (tsl::cfg::FramebufferWidth - 90) / 2, 300, 90, renderer->a(tsl::style::color::ColorText));
                renderer->drawString("No Overlays found!", false, 105, 380, 25, renderer->a(tsl::style::color::ColorText));
                renderer->drawString("Place your .ovl files in /switch/.overlays", false, 82, 410, 15, renderer->a(tsl::style::color::ColorDescription));
            }));
        } else {
            /* Release actual browser gui */
            rootFrame->setContent(browser.release());
        }
    }
};

class OverlayTeslaMenu : public tsl::Overlay {
  public:
    OverlayTeslaMenu() {}
    ~OverlayTeslaMenu() {}

    void onShow() override {
        auto *gui = dynamic_cast<GuiMain *>(tsl::Overlay::get()->getCurrentGui().get());
        if (gui != nullptr)
            gui->refreshGui();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return std::make_unique<GuiMain>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTeslaMenu, tsl::impl::LaunchFlags::None>(argc, argv);
}
