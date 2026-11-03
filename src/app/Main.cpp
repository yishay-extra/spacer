#include <JuceHeader.h>

#include <memory>

#include "app/AppContext.h"
#include "app/MainComponent.h"

namespace spacer {

class SpacerApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& /*commandLine*/) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    /// Owns the composition root (AppContext) and the UI built on it.
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff111316), DocumentWindow::allButtons),
              context_(std::make_unique<AppContext>()) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(*context_), true);
            setResizable(true, true);
            setResizeLimits(MainComponent::kMinWidth, MainComponent::kMinHeight, 10000, 10000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            context_->start();
            if (auto* content = getContentComponent()) content->grabKeyboardFocus();
        }

        ~MainWindow() override {
            // The UI listens to the context: delete it first.
            clearContentComponent();
            context_ = nullptr;
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        std::unique_ptr<AppContext> context_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    juce::LookAndFeel_V4 lookAndFeel_{juce::LookAndFeel_V4::getDarkColourScheme()};
    std::unique_ptr<MainWindow> mainWindow;
};

}  // namespace spacer

START_JUCE_APPLICATION(spacer::SpacerApplication)
