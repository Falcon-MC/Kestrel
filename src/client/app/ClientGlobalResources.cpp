#include "client/Client.h"
#include "platform/Shell.h"
#include "ui/Image.h"
#include "ui/Localization.h"

namespace kestrel {

void Client::updateGlobalResources()
{
    using Kind = world::GlobalPackAction::Kind;
    for (auto action : menu.takeGlobalPackActions()) {
        if (action.kind == Kind::OpenFolder) {
            std::error_code error;
            std::filesystem::create_directories(globalResources.directory(), error);
            if (error) menu.notify(error.message());
            else {
                auto directory = globalResources.directory().u8string();
                platform::openUrl(std::string(directory.begin(), directory.end()));
            }
            continue;
        }
        if (action.kind == Kind::Import) {
            platform::showFilePicker(platform::FileKind::ResourcePack);
            continue;
        }
        globalResources.request(std::move(action));
    }
    if (auto picked = platform::takePickedFile(platform::FileKind::ResourcePack)) {
        globalResources.request({ Kind::Import, {}, std::move(*picked) });
    }
    if (globalResources.poll()) {
        for (const auto& name : globalPackIcons) skin.clearDynamic(name);
        globalPackIcons.clear();
        auto entries = globalResources.entries();
        for (auto& entry : entries) {
            if (!entry.icon) continue;
            ui::Bitmap icon;
            if (!ui::decodeBitmap(*entry.icon, icon)) { entry.icon.reset(); continue; }
            std::string name = "dynamic/global_pack/" + entry.id;
            skin.setDynamic(name, ui::shrinkBitmap(icon, 64));
            globalPackIcons.insert(std::move(name));
        }
        menu.setGlobalResources(std::move(entries));
        if (globalResources.packs() != selectedGlobalPacks) {
            selectedGlobalPacks = globalResources.packs();
            ++globalResourcesRevision;
            session.setGlobalPacks(globalResources.packs(), globalResourcesRevision);
        }
    }
    const auto snapshot = session.sharedSnapshot();
    bool reloading = snapshot->state == SessionState::Joined && snapshot->resourceReloading;
    std::string status = globalResources.message();
    if (status == "Global resources updated") status = ui::tr("kestrel.globalResources.updated", status);
    else if (status == "Deactivated packs with missing dependencies") status = ui::tr("kestrel.globalResources.dependencies", status);
    else if (status.starts_with("Imported ")) status = ui::trf("kestrel.globalResources.imported", "Imported %s", { status.substr(9) });
    if (reloading) status = ui::tr("kestrel.globalResources.applying", "Applying resource packs...");
    if (snapshot->state == SessionState::Joined && !snapshot->resourceReloadError.empty()) status = snapshot->resourceReloadError;
    menu.setGlobalResourceStatus(globalResources.busy() || reloading, std::move(status));
}

}
