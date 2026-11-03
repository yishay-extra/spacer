#pragma once

#include <juce_events/juce_events.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "control/ControlHub.h"
#include "control/CueEngine.h"
#include "control/InstallationMode.h"
#include "control/device/MidiInputService.h"
#include "core/Project.h"
#include "engine/device/AudioEngine.h"
#include "ui/logic/FeedbackState.h"
#include "ui/logic/ProjectHistory.h"
#include "ui/logic/SourceSelection.h"

namespace spacer {

/// The app's composition root. Owned by the main window; everything here runs
/// on the JUCE message thread (SPEC §5.1).
///
/// Owns:
///   - the live Project (single source of truth) and its ProjectHistory (undo),
///   - the AudioEngine (WP3) and the FeedbackState the UI draws from,
///   - the CueEngine (WP9) driving the engine's command queue,
///   - the ControlHub (WP8: OSC in/out, MIDI mapping) and MidiInputService,
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
    /// Live parameter change (not undoable, not stored in the model). False if dropped.
    bool send(const Command& command) { return engine_.send(command); }
    const FeedbackState& feedback() const noexcept { return feedback_; }
    const std::string& deviceError() const noexcept { return deviceError_; }

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

    const InstallationSettings& installationSettings() const noexcept { return installation_; }

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
    void loadInstallationSettings();
    void rememberProjectPath();

    Project project_;
    ProjectHistory history_;
    AudioEngine engine_;
    std::unique_ptr<CueEngine> cues_;
    std::unique_ptr<ControlHub> control_;  // after cues_: holds a pointer to it
    std::unique_ptr<MidiInputService> midiIn_;
    FeedbackState feedback_;
    SourceSelection selection_;

    InstallationSettings installation_;
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
