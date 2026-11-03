#pragma once

#include <juce_events/juce_events.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "control/ClockInputs.h"
#include "control/ControlHub.h"
#include "control/CueEngine.h"
#include "control/InstallationMode.h"
#include "control/device/MidiInputService.h"
#include "core/Project.h"
#include "engine/PluginHost.h"
#include "engine/device/AudioEngine.h"
#include "ui/logic/AppSettings.h"
#include "ui/logic/FeedbackState.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SettingsModel.h"
#include "ui/logic/SourceSelection.h"

namespace spacer {

class PluginUi;

/// The app's composition root. Owned by the main window; everything here runs
/// on the JUCE message thread (SPEC §5.1).
///
/// Owns:
///   - the live Project (single source of truth) and its ProjectHistory (undo),
///   - the AudioEngine (WP3) and the FeedbackState the UI draws from,
///   - the CueEngine (WP9) driving the engine's command queue,
///   - the ControlHub (WP8: OSC in/out, MIDI mapping) and MidiInputService,
///   - the external-clock inputs (WP12: MIDI clock / MTC / OSC beat feeds,
///     factory for the engine's clock; SPEC §9),
///   - installation-mode settings and the Autosaver (WP9, SPEC §11),
///   - the speaker-test overlay (SPEC §16).
///
/// Wiring:
///   model edit -> perform(edit) -> ProjectHistory -> planEngineSync() ->
///       structural: AudioEngine::setProject (GraphBuilder -> SwapGraph)
///       parameters: commands on the engine queue (send())
///   engine feedback (30 Hz, AudioEngine's poll) -> FeedbackState
///       -> fb::CueFired forwarded to CueEngine::onEngineCueFired
///       -> ControlHub::onFeedback (OSC feedback out)
///   own 30 Hz tick -> ControlHub::pump (OSC / MIDI -> engine commands, cues),
///       meter decay, CueEngine::tick, autosave, Listener::feedbackUpdated
///
/// The ControlHub and the CueEngine are rebuilt together when a project is
/// loaded or the device opens (the hub holds a CueEngine pointer); MIDI input
/// is closed meanwhile, so the MIDI thread never sees a stale hub.
///
/// How other subsystems plug in (WP11b-e):
///   - Parameter changes that are *not* model edits (live OSC/MIDI control):
///     send(Command). Single producer (SPEC §5.2): call it on the message
///     thread only — OSC/MIDI callbacks hand events over first.
///   - Model edits that should be undoable: perform(ProjectEdit).
///   - Cue transport and scene manager: cues() (+ cueTick() for the "now"
///     every CueEngine call needs) or go() / back() which also arm.
///   - Observe the model or the 30 Hz feedback: addListener().
class AppContext final : private juce::Timer {
public:
    enum class Change {
        Loaded,         // a new / opened project replaced the model
        Edited,         // perform() applied an edit
        UndoRedo,       // undo() / redo()
        SceneRecalled,  // a cue or scene recall changed the live model
        SpeakerTest,    // speaker test started / stopped
        Panic,          // panic engaged / released
        Selection,      // the selected source changed (selectSource())
        CueState,       // a cue list was armed (the model is untouched)
    };

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void projectChanged(Change) {}
        /// Called at kTickHz after the feedback state was updated.
        virtual void feedbackUpdated() {}
    };

    static constexpr int kTickHz = 30;
    /// While a gesture is open, structural rebuilds are throttled to this interval.
    static constexpr double kGestureRebuildSeconds = 0.1;

    AppContext();
    ~AppContext() override;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    /// Opens the audio device for the current project and, if installation
    /// mode is on, loads the last project and starts the show (SPEC §11).
    void start();

    // ---- model --------------------------------------------------------------
    const Project& project() const noexcept { return project_; }
    /// Monotonic; changes whenever the model changes.
    std::uint64_t revision() const noexcept { return revision_; }
    /// Applies and records an undoable edit and syncs the engine. False if the
    /// edit was invalid.
    bool perform(ProjectEdit edit);
    /// Brackets a continuous gesture (drag): its edits merge into one undo step
    /// and graph rebuilds are throttled until endGesture().
    void beginGesture();
    void endGesture();
    bool undo();
    bool redo();
    const ProjectHistory& history() const noexcept { return history_; }

    // ---- selection ----------------------------------------------------------
    /// The selected source (Sources panel, stage view; OSC / MIDI bare paths
    /// via `selectedSourceId()`, see ControlDispatcher::Config::selectedSource).
    /// Kept valid across edits, undo, loads and recalls (SourceSelection::reconcile).
    const SourceSelection& selection() const noexcept { return selection_; }
    int selectedSourceId() const noexcept { return selection_.selectedId(); }
    /// Selects `sourceId` (0 = none) and notifies with Change::Selection.
    void selectSource(int sourceId);
    bool isDirty() const noexcept { return revision_ != savedRevision_; }

    void newProject(Project project);
    /// Opens a project file. Returns an error message (empty on success).
    std::string openProject(const std::filesystem::path& path);
    /// Saves to `path` (or the current path when empty). Returns an error message.
    std::string saveProject(const std::filesystem::path& path = {});
    const std::filesystem::path& projectPath() const noexcept { return projectPath_; }

    // ---- engine -------------------------------------------------------------
    AudioEngine& engine() noexcept { return engine_; }
    /// Plug-in scan results and hosting (WP10); the Sources panel lists them for inserts.
    PluginHost& plugins() noexcept { return *plugins_; }
    /// Plug-in editor windows and the background plug-in scan (WP11e).
    PluginUi& pluginUi() noexcept { return *pluginUi_; }
    /// Writes the live plug-in state (edits made in plug-in editors) into the
    /// model (SPEC §13) and marks the project modified if it changed. Save,
    /// scene update and cue recalls do this themselves. Returns the number
    /// of slots read.
    int capturePluginState();
    /// Live parameter change (not undoable, not stored in the model). False if dropped.
    bool send(const Command& command) { return engine_.send(command); }
    const FeedbackState& feedback() const noexcept { return feedback_; }
    const std::string& deviceError() const noexcept { return deviceError_; }

    /// Spacer.driver (SPEC §14). Without it the app runs normally; the status
    /// bar shows virtualDeviceWarning() and VirtualInput sources are silent.
    VirtualDeviceReceiver::Status virtualDeviceStatus() const { return engine_.virtualDeviceStatus(); }
    /// Non-empty when the driver is not connected; mentions the affected
    /// sources when the project has VirtualInput sources.
    std::string virtualDeviceWarning() const;
    /// Version of the driver installed in /Library/Audio/Plug-Ins/HAL ("" if none).
    std::string installedDriverVersion() const;
    /// "Install virtual device…": admin prompt, copy, coreaudiod restart.
    /// `done(error)` on the message thread, error empty on success.
    void installVirtualDevice(std::function<void(std::string error)> done);
    /// External clock feeds and the active external clock's lock status.
    const ClockInputs& clockInputs() const noexcept { return clockInputs_; }
    /// Clock kind actually running (Internal after a fallback, e.g. Link not built).
    ClockKind activeClockKind() const noexcept { return engine_.activeClockKind(); }

    void setPanic(bool engaged);
    bool panic() const noexcept { return panic_; }

    /// Speaker test: solo pink noise on one speaker; the model is not touched.
    void startSpeakerTest(int speakerId);
    void stopSpeakerTest();
    std::optional<int> speakerTestId() const noexcept { return speakerTest_; }

    // ---- cues ---------------------------------------------------------------
    CueEngine& cues() noexcept { return *cues_; }
    /// OSC / MIDI (WP8). Rebuilt with the CueEngine; do not keep the reference
    /// across a project load.
    ControlHub& control() noexcept { return *control_; }
    /// "Now" for CueEngine calls: monotonic seconds, last engine clock, wall clock.
    CueTick cueTick() const;
    /// Go / Back on the armed cue list (arms the first list if none is armed).
    bool go();
    bool back();
    /// Arms the named cue list (position resets to "nothing fired"). False if
    /// there is no such list.
    bool armCueList(const std::string& name);
    /// Fires cue `index` of the armed list directly (arms the first list if none is armed).
    bool jumpToCue(int index);
    /// Scene manager: recalls a stored scene outside any cue list.
    bool recallScene(const std::string& name, float transitionSeconds, bool restartMotionPhase);
    /// Scene manager: "Update scene" / "New scene". Both capture the live
    /// plug-in state first (SPEC §13), then store the live state as an undoable show edit.
    bool updateScene(const std::string& name);
    bool newScene(const std::string& name);

    const InstallationSettings& installationSettings() const noexcept { return installation_; }
    /// Installation mode on / off and its autosave interval (SPEC §11); stored
    /// in the app's settings folder, not in the project.
    void setInstallationSettings(InstallationSettings settings);

    // ---- settings (WP11d, SPEC §16.5) -----------------------------------------
    /// App-level preferences (virtual device latency, MIDI inputs); not in the project.
    const AppSettings& appSettings() const noexcept { return appSettings_; }
    void setAppSettings(const AppSettings& settings);
    /// Project-stored settings (device, rate, buffer, speed of sound, OSC,
    /// program-change channel, clock) are edited with
    /// perform(edits::setSettings(...)). The app applies what a change needs
    /// from the model diff, so undo / redo and project loads take the same
    /// path: device reopen, graph rebuild (ClockSelector switches), OSC / MIDI
    /// re-bind.
    /// Message of the last failed OSC input bind ("" if bound).
    std::string oscBindError() const;
    /// Creates a CoreAudio aggregate device (macOS). Returns an error message.
    std::string createAggregateDevice(const settings::AggregateRequest& request);
    std::vector<settings::AudioDeviceInfo> audioDevices() const;
    /// MIDI inputs currently present, by name.
    std::vector<std::string> midiInputNames() const;

    // ---- observers ----------------------------------------------------------
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

private:
    void timerCallback() override;
    void onFeedback(const Feedback& feedback);
    void syncEngine(const EngineEffect& effect);
    void rebuildGraph();
    void resetCueEngine();
    void resetControl();
    void modelChanged(Change change);
    void notify(Change change);
    void syncArmedList();
    void loadInstallationSettings();
    void rememberProjectPath();
    /// Applies what changed in the settings fields since the last call.
    void applyRuntimeSettings();
    void reopenDevice();

    Project project_;
    ProjectHistory history_;
    std::unique_ptr<PluginHost> plugins_;  // before engine_: outlives its graphs
    ClockInputs clockInputs_;              // before engine_: its clock factory refers to it
    AudioEngine engine_;
    std::unique_ptr<CueEngine> cues_;
    std::unique_ptr<ControlHub> control_;  // after cues_: holds a pointer to it
    std::unique_ptr<MidiInputService> midiIn_;
    std::unique_ptr<PluginUi> pluginUi_;  // reset first in ~AppContext (it listens to us)
    FeedbackState feedback_;
    SourceSelection selection_;

    InstallationSettings installation_;
    AppSettings appSettings_;
    std::filesystem::path appSettingsPath_;
    settings::SettingsFields appliedSettings_;  // what the device / control / clock currently run
    std::filesystem::path installationSettingsPath_;
    std::unique_ptr<Autosaver> autosaver_;

    std::filesystem::path projectPath_;
    std::string deviceError_;
    std::uint64_t revision_{0};
    std::uint64_t savedRevision_{0};
    std::uint64_t autosavedRevision_{0};
    std::optional<int> speakerTest_;
    bool panic_{false};
    bool rebuildPending_{false};
    double lastRebuild_{0.0};
    double lastTick_{0.0};
    std::vector<Listener*> listeners_;
};

}  // namespace spacer
