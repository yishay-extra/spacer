#include "app/MainComponent.h"

#include <algorithm>
#include <cmath>

namespace spacer {

MainComponent::MainComponent(AppContext& context)
    : context_(context), statusBar_(context), sources_(context) {
    openButton_.onClick = [this] { openProject(); };
    saveButton_.onClick = [this] { saveProject(false); };
    saveAsButton_.onClick = [this] { saveProject(true); };
    undoButton_.onClick = [this] { context_.undo(); };
    redoButton_.onClick = [this] { context_.redo(); };
    backButton_.onClick = [this] { context_.back(); };
    goButton_.onClick = [this] { context_.go(); };
    panicButton_.onClick = [this] { context_.setPanic(!context_.panic()); };
    goButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e7d32));
    for (auto* b : {&openButton_, &saveButton_, &saveAsButton_, &undoButton_, &redoButton_, &backButton_,
                    &goButton_, &panicButton_}) {
        b->setWantsKeyboardFocus(false);  // keep shortcuts on this component
        addAndMakeVisible(b);
    }
    undoButton_.setTooltip("Undo (Cmd+Z)");
    redoButton_.setTooltip("Redo (Shift+Cmd+Z)");
    goButton_.setTooltip("Go (space)");
    panicButton_.setTooltip("Panic: silence all outputs (Esc)");

    settingsButton_.onClick = [this] { SettingsPanel::show(context_); };
    settingsButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(settingsButton_);

    // The status bar owns the driver status and its install offer (SPEC §16.6).
    statusBar_.onInstallDriver = [this] { installVirtualDevice(); };
    addAndMakeVisible(statusBar_);

    const auto tabColour = juce::Colour(0xff1e2126);
    tabs_.addTab("Stage", tabColour, new StageView(context_, StageView::Mode::Perform), true);
    tabs_.addTab("Layout", tabColour, new LayoutEditor(context_), true);
    tabs_.setTabBarDepth(28);
    addAndMakeVisible(sources_);
    addAndMakeVisible(tabs_);
    addAndMakeVisible(cuesPanel_);

    setWantsKeyboardFocus(true);
    context_.addListener(this);
    updateButtons();
    setSize(1280, 800);
}

MainComponent::~MainComponent() {
    SettingsPanel::close();  // the dialog listens to the context
    context_.removeListener(this);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff111316));
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(40).reduced(6);
    for (auto* b : {&openButton_, &saveButton_, &saveAsButton_}) {
        b->setBounds(bar.removeFromLeft(80));
        bar.removeFromLeft(4);
    }
    bar.removeFromLeft(16);
    for (auto* b : {&undoButton_, &redoButton_}) {
        b->setBounds(bar.removeFromLeft(64));
        bar.removeFromLeft(4);
    }
    panicButton_.setBounds(bar.removeFromRight(80));
    bar.removeFromRight(12);
    goButton_.setBounds(bar.removeFromRight(90));
    bar.removeFromRight(4);
    backButton_.setBounds(bar.removeFromRight(64));
    bar.removeFromRight(12);
    bar.removeFromLeft(12);
    settingsButton_.setBounds(bar.removeFromLeft(100));
    statusBar_.setBounds(area.removeFromBottom(StatusBar::kHeight));
    sources_.setBounds(area.removeFromLeft(kSourcePanelWidth));
    cuesPanel_.setBounds(area.removeFromRight(CuesPanel::kPreferredWidth));
    tabs_.setBounds(area);
}

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        context_.go();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        context_.setPanic(!context_.panic());
        return true;
    }
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')) {
        if (mods.isShiftDown())
            context_.redo();
        else
            context_.undo();
        return true;
    }
    return false;
}

void MainComponent::projectChanged(AppContext::Change) {
    updateButtons();
}

void MainComponent::updateButtons() {
    const auto& h = context_.history();
    undoButton_.setEnabled(h.canUndo());
    redoButton_.setEnabled(h.canRedo());
    undoButton_.setTooltip(h.canUndo() ? "Undo " + juce::String(h.undoName()) + " (Cmd+Z)" : "Undo (Cmd+Z)");
    redoButton_.setTooltip(h.canRedo() ? "Redo " + juce::String(h.redoName()) + " (Shift+Cmd+Z)"
                                       : "Redo (Shift+Cmd+Z)");
    panicButton_.setColour(juce::TextButton::buttonColourId,
                           context_.panic() ? juce::Colour(0xffc62828) : juce::Colour(0xff4a2a2a));
    panicButton_.setButtonText(context_.panic() ? "PANIC ON" : "Panic");
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>()) {
        juce::String title = "Spacer - " + juce::String(context_.project().name);
        if (context_.isDirty()) title << " *";
        window->setName(title);
    }
}

void MainComponent::openProject() {
    chooser_ = std::make_unique<juce::FileChooser>("Open project", juce::File(), "*.spacer");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            const auto error =
                context_.openProject(std::filesystem::path(file.getFullPathName().toStdString()));
            if (!error.empty() && !context_.installationSettings().suppressDialogs)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not open project", juce::String(error));
        });
}

void MainComponent::saveProject(bool saveAs) {
    if (!saveAs && !context_.projectPath().empty()) {
        context_.saveProject();
        updateButtons();
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>("Save project", juce::File(), "*.spacer");
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File()) return;
            if (!file.hasFileExtension("spacer")) file = file.withFileExtension("spacer");
            const auto error =
                context_.saveProject(std::filesystem::path(file.getFullPathName().toStdString()));
            if (!error.empty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not save project", juce::String(error));
            updateButtons();
        });
}

void MainComponent::installVirtualDevice() {
    context_.installVirtualDevice(
        [safe = juce::Component::SafePointer<MainComponent>(this)](std::string error) {
            if (safe == nullptr) return;
            if (error.empty())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon, "Virtual device installed",
                    "Spacer.driver " + juce::String(safe->context_.installedDriverVersion()) +
                        " is installed. The \"Spacer\" output device appears "
                        "in other apps after Core Audio restarts.");
            else
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not install the virtual device",
                                                       juce::String(error));
        });
}

}  // namespace spacer
