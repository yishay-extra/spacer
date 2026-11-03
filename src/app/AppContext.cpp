#include "app/AppContext.h"

#include <algorithm>

#include "core/LayoutPresets.h"
#include "core/ProjectFile.h"
#include "ui/logic/EngineSync.h"
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

AppContext::AppContext() : project_(defaultProject()) {
    installationSettingsPath_ = settingsDirectory() / "installation.json";
    loadInstallationSettings();
    engine_.onFeedback = [this](const Feedback& f) { onFeedback(f); };
    // External clocks (SPEC §9) travel with the graph; AudioEngine asks for
    // one whenever project.clock changes.
    engine_.setClockFactory([this](const ClockConfig& c) { return clockInputs_.makeClock(c); });
    engine_.setProject(project_);
    resetCueEngine();
}

AppContext::~AppContext() {
    stopTimer();
    midiIn_.reset();  // no MIDI callbacks into a dying hub
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

void AppContext::resetCueEngine() {
    const auto status = engine_.status();
    CueEngine::Config config;
    config.sampleRate = status.sampleRate > 0.0 ? status.sampleRate : project_.devices.sampleRate;
    // Same block allowance as AudioEngine's own graphs.
    config.maxBlockSize =
        std::max(status.bufferSize > 0 ? status.bufferSize : project_.devices.bufferSize, 512);
    if (status.outputChannels > 0) config.numOutputChannels = status.outputChannels;
    if (midiIn_) midiIn_->close();
    control_.reset();
    cues_ = std::make_unique<CueEngine>(engine_.processor(), project_, std::move(config));
    resetControl();
}

void AppContext::resetControl() {
    control_ = std::make_unique<ControlHub>(project_, engine_.processor(), cues_.get());
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
    midiIn_->openAll();
}

// ---- model --------------------------------------------------------------------

bool AppContext::perform(ProjectEdit edit) {
    const auto effect = history_.perform(project_, std::move(edit));
    if (!effect) return false;
    syncEngine(*effect);
    syncArmedList();
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
    modelChanged(Change::UndoRedo);
    return true;
}

bool AppContext::redo() {
    const auto effect = history_.redo(project_);
    if (!effect) return false;
    syncEngine(*effect);
    if (rebuildPending_) rebuildGraph();
    syncArmedList();
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
    resetCueEngine();
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
    if (const auto err = projectfile::save(project_, target)) return err->message;
    projectPath_ = target;
    savedRevision_ = revision_;
    rememberProjectPath();
    return {};
}

void AppContext::rememberProjectPath() {
    // Installation mode reopens the last project (SPEC §11).
    installation_.lastProjectPath = projectPath_.string();
    std::error_code ec;
    std::filesystem::create_directories(installationSettingsPath_.parent_path(), ec);
    installation::saveSettings(installation_, installationSettingsPath_);
}

void AppContext::modelChanged(Change change) {
    ++revision_;
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
    if (!cues_->armed() && !cues_->armFirst(tick)) return false;
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
        if (autosaver_->tick(now, project_, dirty)) autosavedRevision_ = revision_;
    }

    const auto copy = listeners_;
    for (auto* l : copy) l->feedbackUpdated();
}

}  // namespace spacer
