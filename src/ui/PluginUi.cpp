#include "ui/PluginUi.h"

#include <algorithm>

#include "ui/PluginEditorWindow.h"
#include "ui/PluginManagerWindow.h"
#include "ui/logic/InsertEditing.h"

namespace spacer {

PluginUi::PluginUi(AppContext& context) : context_(context), scan_(context.plugins()) {
    context_.addListener(this);
}

PluginUi::~PluginUi() {
    context_.removeListener(this);
    closeAllEditors();
    manager_.reset();
}

pluginwindows::InstanceLookup PluginUi::lookup() {
    return [this](const pluginwindows::SlotKey& key) {
        return reinterpret_cast<pluginwindows::InstanceToken>(
            context_.plugins().instanceFor(key.owner, key.ownerId, key.slot).get());
    };
}

PluginUi::Window* PluginUi::windowFor(const pluginwindows::SlotKey& key) {
    for (auto& w : windows_)
        if (w.key == key) return &w;
    return nullptr;
}

juce::String PluginUi::titleFor(const pluginwindows::SlotKey& key) const {
    const Project& project = context_.project();
    juce::String owner;
    if (key.owner == InsertOwner::Source) {
        owner = "Source " + juce::String(key.ownerId);
        for (const auto& s : project.sources)
            if (s.id == key.ownerId) owner = juce::String::fromUTF8(s.name.c_str());
    } else {
        owner = "Speaker " + juce::String(key.ownerId);
        if (const Speaker* sp = project.layout.findSpeaker(key.ownerId))
            owner = "Speaker " + juce::String::fromUTF8(sp->name.c_str());
    }
    const std::string pluginId = pluginwindows::pluginIdAt(project, key);
    juce::String name = juce::String::fromUTF8(insertedit::fallbackName(pluginId).c_str());
    if (const auto d = context_.plugins().cache().find(pluginId)) name = d->name;
    return name + " - " + owner + ", insert " + juce::String(key.slot + 1);
}

std::string PluginUi::openEditor(InsertOwner owner, int ownerId, int slot) {
    const pluginwindows::SlotKey key{owner, ownerId, slot};
    switch (book_.open(context_.project(), key, lookup())) {
        case pluginwindows::WindowBook::OpenResult::AlreadyOpen:
            if (auto* w = windowFor(key)) {
                w->window->setMinimised(false);
                w->window->toFront(true);
            }
            return {};
        case pluginwindows::WindowBook::OpenResult::NoSuchSlot:
            return "This insert slot is empty.";
        case pluginwindows::WindowBook::OpenResult::NotLoaded:
            return "The plug-in could not be loaded (it is missing or failed to start), so there is no "
                   "editor to show. The slot stays bypassed.";
        case pluginwindows::WindowBook::OpenResult::Opened:
            break;
    }
    auto plugin = context_.plugins().instanceFor(owner, ownerId, slot);
    if (!plugin) {
        book_.closed(key);
        return "The plug-in is not loaded.";
    }
    Window w;
    w.key = key;
    w.window = std::make_unique<PluginEditorWindow>(titleFor(key), std::move(plugin), [this, key] {
        // Close button: defer, the window is deleted from inside its own callback otherwise.
        juce::MessageManager::callAsync([this, key, alive = std::weak_ptr<bool>(alive_)] {
            if (alive.expired()) return;
            book_.closed(key);
            windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                          [&](const Window& x) { return x.key == key; }),
                           windows_.end());
        });
    });
    windows_.push_back(std::move(w));
    return {};
}

bool PluginUi::isEditorOpen(InsertOwner owner, int ownerId, int slot) const {
    return book_.isOpen({owner, ownerId, slot});
}

void PluginUi::closeAllEditors() {
    windows_.clear();
    for (const auto& e : std::vector<pluginwindows::Entry>(book_.entries())) book_.closed(e.key);
}

void PluginUi::reconcile() {
    if (windows_.empty()) return;
    const auto plan = book_.reconcile(context_.project(), lookup());
    for (const auto& key : plan.close)
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(), [&](const Window& x) { return x.key == key; }),
            windows_.end());
    for (const auto& entry : plan.retarget) {
        if (auto* w = windowFor(entry.key)) {
            w->window->setPlugin(
                context_.plugins().instanceFor(entry.key.owner, entry.key.ownerId, entry.key.slot));
            w->window->setName(titleFor(entry.key));
        }
    }
}

void PluginUi::projectChanged(AppContext::Change) {
    reconcile();
}

void PluginUi::feedbackUpdated() {
    reconcile();  // graph rebuilds can lag the model change (throttled during gestures)
    if (scan_.poll()) {
        ++scanGeneration_;
        if (manager_ != nullptr) manager_->scanFinished();
    }
}

bool PluginUi::startScan(bool rescanAll) {
    return scan_.start(rescanAll);
}

void PluginUi::showPluginManager() {
    if (manager_ == nullptr) manager_ = std::make_unique<PluginManagerWindow>(context_, *this);
    manager_->setVisible(true);
    manager_->toFront(true);
}

}  // namespace spacer
