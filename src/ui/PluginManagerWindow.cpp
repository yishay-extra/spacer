#include "ui/PluginManagerWindow.h"

#include <algorithm>

#include "ui/PluginUi.h"
#include "ui/logic/ScanText.h"

namespace spacer {

namespace {

/// ListBox model driven by lambdas (two small lists in one panel).
class LambdaListModel final : public juce::ListBoxModel {
public:
    std::function<int()> rows;
    std::function<juce::String(int)> text;
    std::function<juce::String(int)> tooltip;
    std::function<void(int)> selected;

    int getNumRows() override { return rows ? rows() : 0; }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) override {
        if (isSelected) g.fillAll(juce::Colour(0xff2b4a6f));
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f));
        if (text) g.drawText(text(row), 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }
    juce::String getTooltipForRow(int row) override { return tooltip ? tooltip(row) : juce::String(); }
    void selectedRowsChanged(int last) override {
        if (selected) selected(last);
    }
};

}  // namespace

class PluginManagerWindow::Content final : public juce::Component, private juce::Timer {
public:
    Content(AppContext& context, PluginUi& ui) : context_(context), ui_(ui) {
        scanButton_.onClick = [this] {
            ui_.startScan(false);
            refresh();
        };
        scanButton_.setTooltip("Scan new and changed plug-ins");
        rescanButton_.onClick = [this] {
            ui_.startScan(true);
            refresh();
        };
        rescanButton_.setTooltip("Scan every plug-in again, also blacklisted ones");
        cancelButton_.onClick = [this] {
            ui_.scanJob().cancel();
            refresh();
        };
        retryButton_.onClick = [this] { retrySelected(); };
        retryButton_.setTooltip("Take the selected plug-in off the blacklist and scan it again");
        for (auto* b : {&scanButton_, &rescanButton_, &cancelButton_, &retryButton_}) {
            b->setWantsKeyboardFocus(false);
            addAndMakeVisible(b);
        }
        addAndMakeVisible(progressBar_);
        status_.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(status_);
        counts_.setFont(juce::FontOptions(12.0f));
        counts_.setColour(juce::Label::textColourId, juce::Colour(0xffa0a7b0));
        addAndMakeVisible(counts_);
        for (auto* l : {&pluginsTitle_, &blacklistTitle_}) {
            l->setFont(juce::FontOptions(13.0f, juce::Font::bold));
            addAndMakeVisible(l);
        }
        pluginsTitle_.setText("Plug-ins", juce::dontSendNotification);
        blacklistTitle_.setText("Blacklisted (crashed, hung or failed to scan)", juce::dontSendNotification);

        pluginModel_.rows = [this] { return static_cast<int>(plugins_.size()); };
        pluginModel_.text = [this](int row) {
            if (row < 0 || row >= static_cast<int>(plugins_.size())) return juce::String();
            const auto& d = plugins_[static_cast<std::size_t>(row)];
            return d.name + "   " + d.manufacturerName + "   [" + d.pluginFormatName + "]" +
                   (d.isInstrument ? "   instrument (not usable as an insert)" : "");
        };
        blacklistModel_.rows = [this] { return static_cast<int>(blacklist_.size()); };
        blacklistModel_.text = [this](int row) {
            if (row < 0 || row >= static_cast<int>(blacklist_.size())) return juce::String();
            const auto& b = blacklist_[static_cast<std::size_t>(row)];
            return juce::String::fromUTF8(b.name.c_str()) + "   -   " +
                   juce::String::fromUTF8(b.reason.c_str());
        };
        blacklistModel_.tooltip = [this](int row) {
            if (row < 0 || row >= static_cast<int>(blacklist_.size())) return juce::String();
            return juce::String::fromUTF8(blacklist_[static_cast<std::size_t>(row)].identifier.c_str());
        };
        blacklistModel_.selected = [this](int) { updateButtons(); };
        pluginList_.setModel(&pluginModel_);
        blacklistList_.setModel(&blacklistModel_);
        for (auto* l : {&pluginList_, &blacklistList_}) {
            l->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1b1e22));
            l->setRowHeight(22);
            addAndMakeVisible(l);
        }
        setSize(620, 520);
        refreshLists();
        refresh();
        startTimerHz(10);
    }

    ~Content() override {
        stopTimer();
        pluginList_.setModel(nullptr);
        blacklistList_.setModel(nullptr);
    }

    void scanFinished() {
        refreshLists();
        refresh();
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff15171a)); }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto bar = r.removeFromTop(26);
        scanButton_.setBounds(bar.removeFromLeft(110));
        bar.removeFromLeft(6);
        rescanButton_.setBounds(bar.removeFromLeft(110));
        bar.removeFromLeft(6);
        cancelButton_.setBounds(bar.removeFromLeft(80));
        r.removeFromTop(8);
        progressBar_.setBounds(r.removeFromTop(18));
        r.removeFromTop(4);
        status_.setBounds(r.removeFromTop(22));
        counts_.setBounds(r.removeFromTop(20));
        r.removeFromTop(6);
        pluginsTitle_.setBounds(r.removeFromTop(20));
        const int listHeight = (r.getHeight() - 20 - 30 - 12) * 2 / 3;
        pluginList_.setBounds(r.removeFromTop(std::max(40, listHeight)));
        r.removeFromTop(8);
        blacklistTitle_.setBounds(r.removeFromTop(20));
        retryButton_.setBounds(r.removeFromBottom(26).removeFromLeft(110));
        r.removeFromBottom(4);
        blacklistList_.setBounds(r);
    }

private:
    void timerCallback() override { refresh(); }

    void refreshLists() {
        plugins_ = context_.plugins().cache().known();
        std::sort(plugins_.begin(), plugins_.end(),
                  [](const juce::PluginDescription& a, const juce::PluginDescription& b) {
                      return a.name.compareIgnoreCase(b.name) < 0;
                  });
        std::vector<std::pair<std::string, std::string>> entries;
        for (const auto& b : context_.plugins().cache().blacklist())
            entries.emplace_back(b.identifier.toStdString(), b.reason.toStdString());
        blacklist_ = scantext::blacklistRows(entries);
        pluginList_.updateContent();
        blacklistList_.updateContent();
        counts_.setText(
            scantext::countsLine(static_cast<int>(plugins_.size()), static_cast<int>(blacklist_.size())),
            juce::dontSendNotification);
    }

    void refresh() {
        const auto progress = ui_.scanJob().progress();
        progressValue_ = progress.fraction();
        status_.setText(juce::String::fromUTF8(scantext::status(progress).c_str()),
                        juce::dontSendNotification);
        progressBar_.setVisible(progress.active());
        if (generation_ != ui_.scanGeneration()) {
            generation_ = ui_.scanGeneration();
            refreshLists();
        }
        updateButtons();
    }

    void updateButtons() {
        const auto b = scantext::buttons(ui_.scanJob().progress());
        scanButton_.setEnabled(b.scan);
        rescanButton_.setEnabled(b.rescan);
        cancelButton_.setEnabled(b.cancel);
        retryButton_.setEnabled(b.retry && blacklistList_.getSelectedRow() >= 0);
    }

    void retrySelected() {
        const int row = blacklistList_.getSelectedRow();
        if (row < 0 || row >= static_cast<int>(blacklist_.size())) return;
        auto& cache = context_.plugins().cache();
        cache.removeFromBlacklist(
            juce::String::fromUTF8(blacklist_[static_cast<std::size_t>(row)].identifier.c_str()));
        cache.save();
        refreshLists();
        ui_.startScan(false);  // the entry is uncached now, so only it is scanned
        refresh();
    }

    AppContext& context_;
    PluginUi& ui_;
    double progressValue_{0.0};
    unsigned generation_{0};
    std::vector<juce::PluginDescription> plugins_;
    std::vector<scantext::BlacklistRow> blacklist_;
    LambdaListModel pluginModel_, blacklistModel_;
    juce::TextButton scanButton_{"Scan"}, rescanButton_{"Rescan all"}, cancelButton_{"Cancel"},
        retryButton_{"Retry selected"};
    juce::ProgressBar progressBar_{progressValue_};
    juce::Label status_, counts_, pluginsTitle_, blacklistTitle_;
    juce::ListBox pluginList_{"plugins"}, blacklistList_{"blacklist"};
};

PluginManagerWindow::PluginManagerWindow(AppContext& context, PluginUi& ui)
    : juce::DocumentWindow("Plug-ins", juce::Colour(0xff25292f), juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    content_ = new Content(context, ui);
    setContentOwned(content_, true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
}

PluginManagerWindow::~PluginManagerWindow() = default;

void PluginManagerWindow::scanFinished() {
    if (content_ != nullptr) content_->scanFinished();
}

}  // namespace spacer
