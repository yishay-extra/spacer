#include "app/AppContext.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>

#include "app/VirtualDeviceInstaller.h"
#include "core/LayoutPresets.h"
#include "core/ProjectFile.h"
#include "engine/device/AggregateDevice.h"
#include "ui/PluginUi.h"
#include "ui/logic/EngineSync.h"
#include "ui/logic/SceneCapture.h"
#include "ui/logic/SpeakerTest.h"

namespace spacer {

namespace {

double nowSeconds() {
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

Project defaultProject() {
    Project p;
    if (auto layout = layoutpresets::load("quad")) p.layout = *layout;
    return p;
}

std::filesystem::path settingsDirectory() {
    const auto dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Spacer");
    return std::filesystem::path(dir.getFullPathName().toStdString());
}

}  // namespace

AppContext::AppContext()
    : project_(defaultProject()),
      // GUI formats (not headless) so hosted plugins can open editors (WP11e).
      plugins_(std::make_unique<PluginHost>(
          PluginHost::Options::appDefaults(),
          [](juce::AudioPluginFormatManager& f) { juce::addDefaultFormatsToManager(f); })) {
    installationSettingsPath_ = settingsDirectory() / "installation.json";
    loadInstallationSettings();
    appSettingsPath_ = settingsDirectory() / "settings.json";
    if (auto s = appsettings::load(appSettingsPath_)) appSettings_ = *s;
    engine_.setVirtualDeviceLatency(appSettings_.virtualDeviceLatencyFrames);
    appliedSettings_ = settings::fieldsOf(project_);
    engine_.setPluginHost(plugins_.get());
    engine_.onFeedback = [this](const Feedback& f) { onFeedback(f); };
    // External clocks (SPEC §9) travel with the graph; AudioEngine asks for
    // one whenever project.clock changes.
    engine_.setClockFactory([this](const ClockConfig& c) { return clockInputs_.makeClock(c); });
    engine_.setProject(project_);
    resetCueEngine();
    pluginUi_ = std::make_unique<PluginUi>(*this);
}

AppContext::~AppContext() {
    stopTimer();
    pluginUi_.reset();  // editor windows and the scan thread go before the engine and the plug-in host
    midiIn_.reset();    // no MIDI callbacks into a dying hub
    engine_.onFeedback = nullptr;
}

void AppContext::loadInstallationSettings() {
    if (auto s = installation::loadSettings(installationSettingsPath_)) installation_ = *s;
}

void AppContext::start() {
    if (installation_.enabled && !installation_.lastProjectPath.empty()) {
        bool usedAutosave = false;
        if (auto loaded = installation::autoload(installation_, &usedAutosave)) {
            project_ = loaded->project;
            projectPath_ = installation_.lastProjectPath;
            history_.clear();
            engine_.setProjectDirectory(juce::File(juce::String(projectPath_.parent_path().string())));
            engine_.setProject(project_);
        }
    }
    appliedSettings_ = settings::fieldsOf(project_);  // the device opens with these just below
    deviceError_ =
        engine_.openDevice(project_.devices, GraphBuilder::requiredInputChannels(project_)).toStdString();
    resetCueEngine();  // now with the device's rate and block size
    if (installation_.enabled) {
        installation::startShow(*cues_, installation_, cueTick());
        if (!projectPath_.empty())
            autosaver_ =
                std::make_unique<Autosaver>(projectPath_, std::max(1, installation_.autosaveSeconds));
    }
    modelChanged(Change::Loaded);
    lastTick_ = nowSeconds();
    startTimerHz(kTickHz);
}

CueEngine::Config AppContext::cueConfig() {
    const auto status = engine_.status();
    CueEngine::Config config;
    config.sampleRate = status.sampleRate > 0.0 ? status.sampleRate : project_.devices.sampleRate;
    // Same block allowance as AudioEngine's own graphs.
    config.maxBlockSize =
        std::max(status.bufferSize > 0 ? status.bufferSize : project_.devices.bufferSize, 512);
    if (status.outputChannels > 0) config.numOutputChannels = status.outputChannels;
    config.pluginHost = plugins_.get();
    // A recall changes the running graph and the live model, not the engine's
    // copy: keep that current so a later device restart rebuilds the live state.
    config.onLiveChanged = [this](const Project& live) {
        if (!speakerTest_) engine_.updateProject(live);
    };
    return config;
}

void AppContext::resetCueEngine() {
    if (midiIn_) midiIn_->close();
    control_.reset();
    cues_ = std::make_unique<CueEngine>(engine_.processor(), project_, cueConfig());
    resetControl();
}

void AppContext::resetControl() {
    // Bare MIDI paths / OSC id 0 act on the UI-selected source; motion
    // parameters and file transport use the engine commands (dispatcher defaults).
    ControlDispatcher::Config config;
    config.selectedSource = [this] { return selectedSourceId(); };
    control_ = std::make_unique<ControlHub>(project_, engine_.processor(), cues_.get(), std::move(config));
    // OSC receive thread: /spacer/clock/beat (and /stop) feed an active
    // OscClock directly, timestamped on arrival (WP12).
    control_->osc().setClockTap([this](const ControlEvent& e, double hostTime) {
        if (const auto* b = std::get_if<ev::ClockBeat>(&e))
            return clockInputs_.handleOscBeat(b->beat, hostTime);
        if (std::holds_alternative<ev::ClockStop>(e)) clockInputs_.handleOscStop(hostTime);
        return false;  // stop still reaches the internal clock
    });
    control_->applyProject();  // OSC port bind failure is visible in control().osc().stats()
    if (!midiIn_)
        midiIn_ = std::make_unique<MidiInputService>([this](const juce::MidiMessage& m) {
            // MIDI thread: hand over only. Beat clock / MTC go to the external
            // clocks lock-free (timestamp = host seconds, JUCE MidiInput); the
            // rest goes to the mapper through the hub.
            if (clockInputs_.handleMidi(m.getRawData(), m.getRawDataSize(), m.getTimeStamp())) return;
            control_->submitMidi(m);
        });
    midiIn_->openAll(appSettings_.disabledMidiInputs);
}

// ---- virtual device -------------------------------------------------------------

std::string AppContext::virtualDeviceWarning() const {
    const auto status = engine_.virtualDeviceStatus();
    if (status.state == VirtualDeviceReceiver::State::Connected) return {};
    int users = 0;
    for (const auto& source : project_.sources)
        if (source.input.kind() == SourceKind::VirtualInput) ++users;
    std::string warning = status.message;
    if (users > 0) warning += " - " + std::to_string(users) + " virtual-input source(s) silent";
    return warning;
}

std::string AppContext::installedDriverVersion() const {
    return VirtualDeviceInstaller::bundleVersion(VirtualDeviceInstaller::installedDriver()).toStdString();
}

void AppContext::installVirtualDevice(std::function<void(std::string error)> done) {
    VirtualDeviceInstaller::install([callback = std::move(done)](juce::String error) {
        if (callback) callback(error.toStdString());
    });
}

// ---- settings (WP11d) -----------------------------------------------------------

void AppContext::applyRuntimeSettings() {
    const auto now = settings::fieldsOf(project_);
    const auto delta = settings::classify(appliedSettings_, now);
    appliedSettings_ = now;
    // Speed of sound and clock changes are structural edits: the graph was
    // rebuilt by syncEngine() and AudioEngine's ClockSelector switched the clock.
    if (delta.device) reopenDevice();  // keeps the cue engine and the control hub
    if ((delta.osc || delta.midi) && control_) control_->applyProject();
}

void AppContext::reopenDevice() {
    if (speakerTest_) {
        speakerTest_.reset();
        const auto copy = listeners_;
        for (auto* l : copy) l->projectChanged(Change::SpeakerTest);
    }
    // Parameter commands (and recalls) reach the running graph, not the
    // engine's project copy: refresh it (no rebuild) so the graph the device
    // restart builds shows the live state.
    engine_.updateProject(project_);
    deviceError_ =
        engine_.openDevice(project_.devices, GraphBuilder::requiredInputChannels(project_)).toStdString();
    // The cue engine follows the new rate / block size and keeps its state
    // (armed list and position).
    if (cues_)
        cues_->setConfig(cueConfig(), cueTick());
    else
        resetCueEngine();
    feedback_.clearPositions();
}

void AppContext::setAppSettings(const AppSettings& settings) {
    const bool midiChanged = settings.disabledMidiInputs != appSettings_.disabledMidiInputs;
    appSettings_ = settings;
    appSettings_.virtualDeviceLatencyFrames =
        appsettings::normaliseVirtualLatency(appSettings_.virtualDeviceLatencyFrames);
    engine_.setVirtualDeviceLatency(appSettings_.virtualDeviceLatencyFrames);
    if (midiChanged && midiIn_) midiIn_->openAll(appSettings_.disabledMidiInputs);
    appsettings::save(appSettings_, appSettingsPath_);
}

void AppContext::setInstallationSettings(InstallationSettings settings) {
    installation_ = std::move(settings);
    installation_.autosaveSeconds = std::max(1, installation_.autosaveSeconds);
    std::error_code ec;
    std::filesystem::create_directories(installationSettingsPath_.parent_path(), ec);
    installation::saveSettings(installation_, installationSettingsPath_);
    if (installation_.enabled && !projectPath_.empty())
        autosaver_ = std::make_unique<Autosaver>(projectPath_, installation_.autosaveSeconds);
    else
        autosaver_.reset();
}

std::string AppContext::oscBindError() const {
    if (control_ && control_->osc().running()) return {};
    return "OSC input port " + std::to_string(project_.osc.inPort) + " could not be bound (in use?)";
}

std::vector<settings::AudioDeviceInfo> AppContext::audioDevices() const {
    std::vector<settings::AudioDeviceInfo> out;
    for (const auto& d : AggregateDevice::listDevices())
        out.push_back({d.uid, d.name, d.numInputs, d.numOutputs});
    return out;
}

std::string AppContext::createAggregateDevice(const settings::AggregateRequest& request) {
    if (!request.error.empty()) return request.error;
    auto created = AggregateDevice::create(request.name, request.uid, request.subDeviceUids,
                                           request.subDeviceUids.front());
    if (!created) return created.error();
    return {};
}

std::vector<std::string> AppContext::midiInputNames() const {
    return MidiInputService::availableNames();
}

// ---- model --------------------------------------------------------------------

bool AppContext::perform(ProjectEdit edit) {
    const auto effect = history_.perform(project_, std::move(edit));
    if (!effect) return false;
    syncEngine(*effect);
    syncArmedList();
    applyRuntimeSettings();
    modelChanged(Change::Edited);
    return true;
}

void AppContext::beginGesture() {
    history_.beginGesture();
}

void AppContext::endGesture() {
    history_.endGesture();
    if (rebuildPending_) rebuildGraph();
}

bool AppContext::undo() {
    const auto effect = history_.undo(project_);
    if (!effect) return false;
    syncEngine(*effect);
    if (rebuildPending_) rebuildGraph();
    syncArmedList();
    applyRuntimeSettings();
    modelChanged(Change::UndoRedo);
    return true;
}

bool AppContext::redo() {
    const auto effect = history_.redo(project_);
    if (!effect) return false;
    syncEngine(*effect);
    if (rebuildPending_) rebuildGraph();
    syncArmedList();
    applyRuntimeSettings();
    modelChanged(Change::UndoRedo);
    return true;
}

void AppContext::syncEngine(const EngineEffect& effect) {
    const auto plan = planEngineSync(project_, effect);
    // While a speaker test runs, the engine plays the overlay (all sources
    // muted): parameter commands derived from the model would unmute them.
    if (plan.rebuildGraph || (speakerTest_ && !plan.commands.empty())) {
        if (history_.gestureOpen() && nowSeconds() - lastRebuild_ < kGestureRebuildSeconds)
            rebuildPending_ = true;
        else
            rebuildGraph();
        return;
    }
    for (const auto& c : plan.commands) engine_.send(c);
}

void AppContext::rebuildGraph() {
    rebuildPending_ = false;
    lastRebuild_ = nowSeconds();
    engine_.setProject(speakerTest_ ? speakertest::speakerTestProject(project_, *speakerTest_) : project_);
}

void AppContext::newProject(Project project) {
    stopSpeakerTest();
    project_ = std::move(project);
    projectPath_.clear();
    history_.clear();
    autosaver_.reset();
    engine_.setProject(project_);
    // A project that names another device / rate / buffer reopens the device.
    const auto fields = settings::fieldsOf(project_);
    const bool deviceChanged = settings::classify(appliedSettings_, fields).device;
    appliedSettings_ = fields;
    if (deviceChanged) reopenDevice();
    resetCueEngine();  // a new project: fresh sequencer state
    feedback_.clearPositions();
    savedRevision_ = revision_ + 1;  // a fresh project is not dirty
    modelChanged(Change::Loaded);
}

std::string AppContext::openProject(const std::filesystem::path& path) {
    auto loaded = projectfile::load(path);
    if (!loaded) return loaded.error().message;
    // Project-relative file sources resolve against the project's folder (SPEC §15).
    engine_.setProjectDirectory(juce::File(juce::String(path.parent_path().string())));
    newProject(std::move(loaded->project));
    projectPath_ = path;
    rememberProjectPath();
    return {};
}

std::string AppContext::saveProject(const std::filesystem::path& path) {
    const auto target = path.empty() ? projectPath_ : path;
    if (target.empty()) return "No file name";
    capturePluginState();  // edits made in plugin editors are saved too
    if (const auto err = projectfile::save(project_, target)) return err->message;
    projectPath_ = target;
    savedRevision_ = revision_;
    rememberProjectPath();
    return {};
}

int AppContext::capturePluginState() {
    const auto snapshot = [](const Project& p) {
        std::vector<PluginSlot> all;
        for (const auto& s : p.sources) all.insert(all.end(), s.inserts.begin(), s.inserts.end());
        for (const auto& c : p.speakerChains) all.insert(all.end(), c.inserts.begin(), c.inserts.end());
        return all;
    };
    const auto before = snapshot(project_);
    const int n = plugins_->captureState(project_);
    if (snapshot(project_) != before) ++revision_;  // the plug-in changed: the project is modified
    return n;
}

void AppContext::rememberProjectPath() {
    // Installation mode reopens the last project (SPEC §11).
    installation_.lastProjectPath = projectPath_.string();
    std::error_code ec;
    std::filesystem::create_directories(installationSettingsPath_.parent_path(), ec);
    installation::saveSettings(installation_, installationSettingsPath_);
}

void AppContext::selectSource(int sourceId) {
    if (!selection_.select(sourceId)) return;
    notify(Change::Selection);
}

void AppContext::modelChanged(Change change) {
    ++revision_;
    selection_.reconcile(project_);
    notify(change);
}

void AppContext::notify(Change change) {
    const auto copy = listeners_;  // listeners may remove themselves
    for (auto* l : copy) l->projectChanged(change);
}

void AppContext::syncArmedList() {
    // The armed cue list is a copy: follow edits / undo of the project's lists.
    cues_->refreshArmedList(cueTick());
}

// ---- engine -------------------------------------------------------------------

void AppContext::setPanic(bool engaged) {
    if (!engine_.send(cmd::SetPanic{engaged})) return;
    panic_ = engaged;
    const auto copy = listeners_;
    for (auto* l : copy) l->projectChanged(Change::Panic);
}

void AppContext::startSpeakerTest(int speakerId) {
    if (project_.layout.findSpeaker(speakerId) == nullptr) return;
    speakerTest_ = speakerId;
    rebuildGraph();
    const auto copy = listeners_;
    for (auto* l : copy) l->projectChanged(Change::SpeakerTest);
}

void AppContext::stopSpeakerTest() {
    if (!speakerTest_) return;
    speakerTest_.reset();
    rebuildGraph();
    const auto copy = listeners_;
    for (auto* l : copy) l->projectChanged(Change::SpeakerTest);
}

void AppContext::onFeedback(const Feedback& f) {
    feedback_.apply(f);
    control_->onFeedback(f);
    if (const auto* fired = std::get_if<fb::CueFired>(&f)) {
        if (cues_->onEngineCueFired(fired->token, cueTick())) modelChanged(Change::SceneRecalled);
    }
}

// ---- cues ---------------------------------------------------------------------

CueTick AppContext::cueTick() const {
    CueTick t;
    t.nowSeconds = nowSeconds();
    t.clock = feedback_.clock();
    const auto now = juce::Time::getCurrentTime();
    t.wall.minuteStamp = now.toMilliseconds() / 60000;
    t.wall.dayOfWeek = now.getDayOfWeek();
    t.wall.hour = now.getHours();
    t.wall.minute = now.getMinutes();
    return t;
}

bool AppContext::go() {
    const auto tick = cueTick();
    // Nothing armed: the default cue list (installation settings), else the first.
    if (!cues_->armed() && !installation::armDefault(*cues_, installation_, tick) && !cues_->armFirst(tick))
        return false;
    // A recall rebuilds the engine graph from the model: the test overlay ends.
    if (speakerTest_) stopSpeakerTest();
    if (!cues_->go(tick)) return false;
    modelChanged(Change::SceneRecalled);
    return true;
}

bool AppContext::back() {
    const auto tick = cueTick();
    if (!cues_->armed()) return false;
    if (speakerTest_) stopSpeakerTest();
    if (!cues_->back(tick)) return false;
    modelChanged(Change::SceneRecalled);
    return true;
}

bool AppContext::armCueList(const std::string& name) {
    if (!cues_->arm(name, cueTick())) return false;
    notify(Change::CueState);
    return true;
}

bool AppContext::jumpToCue(int index) {
    const auto tick = cueTick();
    if (!cues_->armed() && !cues_->armFirst(tick)) return false;
    if (speakerTest_) stopSpeakerTest();
    if (!cues_->jump(index, tick)) return false;
    modelChanged(Change::SceneRecalled);
    return true;
}

bool AppContext::recallScene(const std::string& name, float transitionSeconds, bool restartMotionPhase) {
    if (speakerTest_) stopSpeakerTest();
    if (!cues_->recallScene(name, transitionSeconds, restartMotionPhase)) return false;
    modelChanged(Change::SceneRecalled);
    return true;
}

bool AppContext::updateScene(const std::string& name) {
    return perform(
        scenecapture::updateScene(project_, name, [this](Project& p) { return plugins_->captureState(p); }));
}

bool AppContext::newScene(const std::string& name) {
    return perform(
        scenecapture::newScene(project_, name, [this](Project& p) { return plugins_->captureState(p); }));
}

// ---- observers ----------------------------------------------------------------

void AppContext::addListener(Listener* listener) {
    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end())
        listeners_.push_back(listener);
}

void AppContext::removeListener(Listener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void AppContext::timerCallback() {
    const double now = nowSeconds();
    feedback_.decay(now - lastTick_);
    lastTick_ = now;

    if (rebuildPending_ && now - lastRebuild_ >= kGestureRebuildSeconds) rebuildGraph();
    // A plugin changed its latency: rebuild so it is compensated (SPEC §5.6).
    if (plugins_->takeLatencyChanged()) rebuildGraph();
    // An edit in a plugin editor modifies the project (dirty, autosave).
    if (plugins_->takeEdited()) {
        ++revision_;
        notify(Change::PluginEdited);
    }

    // External clocks report the beat at the time a block is heard (WP14).
    {
        const auto status = engine_.status();
        clockInputs_.setCompensation(
            ClockInputs::compensationSeconds(status.outputLatencySamples, status.graphLatencySamples,
                                             status.sampleRate, project_.clock.latencyTrimMs));
    }

    // OSC / MIDI. A cue fired from there changes the live model (SPEC §11).
    // Any recall counts, also an OSC / MIDI scene recall that leaves the cue
    // index alone (PLAN WP14 item, fixed here because the Cues panel shows it).
    const auto recallsBefore = cues_->recallCount();
    control_->pump();
    if (cues_->recallCount() != recallsBefore) modelChanged(Change::SceneRecalled);

    // Automatic cue triggers are held while a speaker test runs: a recall
    // rebuilds the engine from the model and would end the test half-way.
    if (!speakerTest_ && cues_->armed() && cues_->tick(cueTick())) modelChanged(Change::SceneRecalled);

    if (autosaver_) {
        const bool dirty = revision_ != autosavedRevision_;
        // Live plugin state (editor edits) is part of the project (SPEC §13).
        if (autosaver_->tick(now, project_, dirty, [this](Project&) { capturePluginState(); }))
            autosavedRevision_ = revision_;
    }

    const auto copy = listeners_;
    for (auto* l : copy) l->feedbackUpdated();
}

}  // namespace spacer
