#pragma once
#include "HostParameter.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include "SamplerVoice.h"
#include "LibraryManager.h"
#include <array>
#include <vector>
#include <functional>

class LSampler24AudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int slotCount = 24;
    static constexpr int sampleSetSize = lsampler::SlotAudioState::sampleSetSize;
    enum VariationMode { variationOff = 0, variationRoundRobin = 1, variationRandom = 2, variationRandomNoRepeat = 3 };
    struct SampleSetEntryInfo
    {
        bool loaded = false;
        juce::File file;
        juce::String name;
        int velocityLow = 1, velocityHigh = 127;
    };

    explicit LSampler24AudioProcessor(const juce::File& libraryRootOverride = {});
    ~LSampler24AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    struct FileTaskResult { bool ok = false; juce::String message; int count = 0, skipped = 0, slot = -1; };
    using FileTask = std::function<FileTaskResult(LSampler24AudioProcessor&)>;
    bool startFileTask(FileTask, std::function<void(FileTaskResult)> completion);
    bool isFileTaskRunning() const noexcept { return fileTaskRunning.load(std::memory_order_acquire); }
    FileTaskResult getLastFileTaskResult() const { const juce::ScopedLock lock(fileResultLock); return lastFileTaskResult; }
    bool shouldStopFileTask() const noexcept { return shuttingDown.load(std::memory_order_acquire); }
    void setFileTaskProgress(double progress) noexcept { fileTaskProgress.store(juce::jlimit(0.0, 1.0, progress), std::memory_order_release); }
    double getFileTaskProgress() const noexcept { return fileTaskProgress.load(std::memory_order_acquire); }
    uint64_t getUiRevision() const noexcept { return uiRevision.load(std::memory_order_acquire); }

    bool loadSample(const juce::File& file, juce::String& error);
    bool loadSampleToSlot(const juce::File& file, int slot, juce::String& error);
    bool loadSampleSetEntryToSlot(const juce::File& file, int slot, int sampleIndex, juce::String& error);
    bool loadSampleSetEntryFromSlotPreset(const juce::File& presetFile, int slot, int sampleIndex, juce::String& error);
    void clearSampleSetEntry(int slot, int sampleIndex);
    SampleSetEntryInfo getSampleSetEntry(int slot, int sampleIndex) const;
    juce::File getSampleSetEntryFile(int slot, int sampleIndex) const;
    void setSampleSetVelocityRange(int slot, int sampleIndex, int low, int high);
    int getVariationMode(int slot) const;
    void setVariationMode(int slot, int mode);
    static juce::String variationModeName(int mode);
    bool importSampleToSlot(const juce::File& file, int slotIndex, double startSeconds, double endSeconds, juce::String& error);
    bool isSlotOccupied(int slotIndex) const;
    bool prepareImportPreview(const juce::File& file, juce::String& error);
    void requestImportPreviewToggle() noexcept { importPreviewToggleRequested.store(true); }
    void requestImportPreviewStop() noexcept { importPreviewStopRequested.store(true); }
    void requestImportPreviewSeek(double seconds) noexcept { importPreviewSeekSeconds.store(seconds); importPreviewSeekRequested.store(true); }
    void requestImportPreviewLoop(double startSeconds, double endSeconds, bool enabled) noexcept
    {
        importPreviewLoopStartSeconds.store(startSeconds, std::memory_order_relaxed);
        importPreviewLoopEndSeconds.store(endSeconds, std::memory_order_relaxed);
        importPreviewLoopEnabled.store(enabled, std::memory_order_release);
    }
    double getImportPreviewPositionSeconds() const noexcept { return importPreviewPositionSeconds.load(std::memory_order_relaxed); }
    double getImportPreviewLengthSeconds() const noexcept { return importPreviewLengthSeconds.load(std::memory_order_relaxed); }
    bool isImportPreviewPlaying() const noexcept { return importPreviewPlayingAtomic.load(std::memory_order_relaxed); }
    bool saveSlotPreset(const juce::File& presetFile, juce::String& error);
    bool saveSlotPresetAt(const juce::File& presetFile, int slot, juce::String& error);
    bool loadSlotPreset(const juce::File& presetFile, juce::String& error);
    bool loadSlotPresetToSlot(const juce::File& presetFile, int slotIndex, juce::String& error);
    bool prepareLibrarySlotPreview(const juce::File& presetFile, juce::String& error);
    void requestLibraryPreviewToggle() noexcept { libraryPreviewToggleRequested.store(true); }
    void requestLibraryPreviewStop() noexcept { libraryPreviewStopRequested.store(true); }
    bool isLibraryPreviewPlaying() const noexcept { return libraryPreviewPlayingAtomic.load(std::memory_order_relaxed); }
    bool saveBankPreset(const juce::File& presetFile, juce::String& error);
    bool loadBankPreset(const juce::File& presetFile, juce::String& error);
    bool importFilesToLibrary(const juce::Array<juce::File>& sourceFiles, int& importedSlots, int& skippedFiles, juce::String& error);
    bool importFolderToLibrary(const juce::File& sourceFolder, int& importedSlots, int& skippedFiles, juce::String& error);
    bool exportLibraryArchive(const juce::File& targetFile, int& exportedSlots, int& exportedSamples, juce::String& error);
    bool exportLibraryFolderArchive(const juce::File& slotFolder, const juce::File& targetFile, int& exportedSlots, int& exportedSamples, juce::String& error);
    bool importLibraryArchive(const juce::File& archiveFile, int& importedSlots, int& importedSamples, int& skippedItems, juce::String& error);

    bool copyCurrentSlot();
    bool cutCurrentSlot();
    bool pasteCurrentSlot();
    void clearCurrentSlot();
    void clearBank();
    bool slotHasSample(int slotIndex) const;
    int getSlotGridPosition(int slotIndex) const noexcept;
    void setSlotGridPosition(int slotIndex, int gridIndex) noexcept;

    int getCurrentSlot() const noexcept { return currentSlot.load(std::memory_order_relaxed); }
    void setCurrentSlot(int slotIndex);
    juce::String getSlotLabel(int slotIndex) const;
    juce::String getSlotName(int slotIndex) const;
    void setSlotName(int slotIndex, const juce::String& name);

    juce::File getCurrentSampleFile() const;
    juce::File getCurrentSlotPresetFile() const;
    juce::String getCurrentSamplePropertiesText() const;
    juce::String getCurrentSlotPropertiesText() const;
    juce::String getSampleStatus() const;
    int getLowKey() const noexcept;
    void setLowKey(int note);
    int getHighKey() const noexcept;
    void setHighKey(int note);
    int getRootNote() const noexcept;
    void setRootNote(int note);
    float getVolume() const noexcept;
    void setVolume(float newVolume);
    void requestPreviewToggle() noexcept
    {
        previewTargetSlot.store(currentSlot.load(std::memory_order_relaxed));
        previewToggleRequested.store(true);
    }
    void requestPreviewStop() noexcept { previewStopRequested.store(true); }
    void requestSampleSetPreview(int slotIndex, int sampleIndex) noexcept
    {
        previewTargetSlot.store(juce::jlimit(0, slotCount - 1, slotIndex), std::memory_order_relaxed);
        previewTargetSampleIndex.store(juce::jlimit(0, sampleSetSize - 1, sampleIndex), std::memory_order_relaxed);
        previewSampleSetRequested.store(true, std::memory_order_release);
    }
    // The slot to audition is published as ONE audio-thread command. The
    // separate Toggle/Audition handlers reuse previewTargetSlot, so sharing
    // that target with Start could otherwise race with another GUI action.
    void requestPreviewStartForSlot(int slotIndex) noexcept
    {
        previewStartSlot.store(juce::jlimit(0, slotCount - 1, slotIndex), std::memory_order_release);
    }
    void requestPreviewStart() noexcept
    {
        requestPreviewStartForSlot(currentSlot.load(std::memory_order_relaxed));
    }
    void requestPreviewRestartIfPlaying() noexcept
    {
        previewTargetSlot.store(currentSlot.load(std::memory_order_relaxed));
        previewRestartRequested.store(true);
    }
    void requestPreviewAuditionFromPercent(double startPercent) noexcept
    {
        previewTargetSlot.store(currentSlot.load(std::memory_order_relaxed));
        previewAuditionStartPercent.store(juce::jlimit(0.0, 100.0, startPercent), std::memory_order_relaxed);
        previewAuditionRequested.store(true, std::memory_order_release);
    }
    void requestSampleBoundaryAudition(bool endBoundary, bool latchPlayStart = false);

    double getSlotParameter(int gridIndex, int loopIndex = 0) const;
    void setSlotParameter(int gridIndex, double value, int loopIndex = 0);
    void resetSlotParameter(int gridIndex, int loopIndex = 0);
    double getGlobalOutputParameter(lsampler::GlobalP parameter) const noexcept;
    void setGlobalOutputParameter(lsampler::GlobalP parameter, double value) noexcept;
    double getSamplePlayStart() const;
    double getSampleWindowStart() const;
    double getSampleWindowEnd() const;
    void setSamplePlayStart(double value);
    void applyZeroCrossing(bool loopWindow, int loopIndex = 0);
    // Diagnostics used by offline regression tests, never by the screen reader.
    int getActiveVoiceCount() const noexcept { return voicePool.activeVoiceCount(); }

    lsampler::SliceState getSliceState(int slot) const;
    void setSliceState(int slot, const lsampler::SliceState&);
    lsampler::SliceAudioState getSliceLayout(int slot, int& absoluteStart, double& sourceRate) const;
    bool moveSliceBoundary(int slot,int boundary,int direction,int frames);
    // One packed command: no separately published target/kind fields can tear.
    void requestSlicePreview(int slot,int kind,int item=0,bool toggle=true) noexcept;
    void stopSlicePreview() noexcept { slicePreviewCommand.store(0,std::memory_order_release); }
    int getSlicePreviewKind() const noexcept { return slicePreviewKind.load(std::memory_order_relaxed); }

    // Read-only GUI snapshot. Shared audio is immutable after publication.
    struct VisualSlotState {
        std::shared_ptr<const SharedSample> sample;
        lsampler::SlotParameters parameters;
        lsampler::SliceState slice;
        int effectiveStart = 0, effectiveEnd = 0;
        juce::String name;
    };
    VisualSlotState getVisualSlotState(int slot) const;
    float consumeVisualPeak(int channel) noexcept {
        return visualPeaks[size_t(juce::jlimit(0, 1, channel))].exchange(0.0f, std::memory_order_relaxed);
    }

    LibraryManager& getLibrary() noexcept { return library; }
    const LibraryManager& getLibrary() const noexcept { return library; }
    void setLibraryRoot(const juce::File& root);
    static juce::File defaultLibraryRoot();

private:
    void notifyHostControl(lsampler::HostParameter*);
    void handleAsyncUpdate() override;
    void createHostParameters();
    void absorbHostValuesLocked() const;
    void publishHostValuesLocked(bool notify = true, int forceSlot = -1);
    void updateAutomatedAudio(bool snapshotChanged);
    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        std::array<std::shared_ptr<SharedSample>, sampleSetSize - 1> alternateSamples {};
        std::array<juce::File, sampleSetSize - 1> alternateSampleFiles {};
        std::array<int, sampleSetSize> sampleVelocityLow = [] { std::array<int, sampleSetSize> v {}; v.fill(1); return v; }();
        std::array<int, sampleSetSize> sampleVelocityHigh = [] { std::array<int, sampleSetSize> v {}; v.fill(127); return v; }();
        int variationMode = variationOff;
        juce::String slotName;
        juce::File presetFile; // Runtime-only origin/save location for Alt+Enter properties.
        bool sampleAudioModified = false;
        int thresholdStartFrame = 0;
        int thresholdEndFrame = 0;
        lsampler::SlotParameters parameters;
        lsampler::SliceState slice;
        juce::String status = "No sample loaded";
    };

    struct HostSlot {
        std::array<lsampler::HostParameter*,lsampler::parameterCount> values {};
        std::array<std::array<lsampler::HostParameter*,lsampler::loopParameterCount>,lsampler::loopCount> loops {};
        std::array<lsampler::HostParameter*,13> slice {};
        lsampler::HostParameter* variation = nullptr;
        std::array<lsampler::HostParameter*,sampleSetSize> velocityLow {}, velocityHigh {};
        mutable lsampler::SlotParameters control;
        mutable lsampler::SliceState controlSlice;
        mutable int controlVariation = 0;
        mutable std::array<int,sampleSetSize> controlLow {}, controlHigh {};
    };
    std::array<HostSlot,slotCount> hostSlots;
    std::array<lsampler::HostParameter*,lsampler::globalParameterCount> hostGlobals {};
    std::array<std::atomic<uint64_t>,slotCount> hostGenerations {};
    std::atomic<uint64_t> globalHostGeneration {0};
    mutable std::array<uint64_t,slotCount> controlHostGenerations {};
    std::array<uint64_t,slotCount> audioHostGenerations {};
    std::unique_ptr<std::array<lsampler::SlotAudioState,slotCount>> automatedAudioStorage =
        std::make_unique<std::array<lsampler::SlotAudioState,slotCount>>();
    std::array<lsampler::SlotAudioState,slotCount>& automatedAudio = *automatedAudioStorage;
    uint64_t audioAutomationRevision = 0;
    bool hostParametersReady = false;
    bool restoringHostState = false; // accessed under stateLock only

    juce::ValueTree makeSlotState(int slotIndex, const juce::String& type) const;
    bool restoreSlotState(int slotIndex, const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseSlotSample(int slotIndex, juce::String& error);
    bool materialiseSampleSetExtras(int slotIndex, juce::String& error);
    void updateThresholdWindow(int slotIndex);
    void markAudioStateDirty();
    void syncAudioStateFromSlots();

    mutable juce::CriticalSection fileResultLock;
    FileTaskResult lastFileTaskResult;
    std::atomic<uint64_t> uiRevision { 1 };
    std::atomic<bool> fileTaskRunning { false }, shuttingDown { false };
    std::atomic<double> fileTaskProgress { 0.0 };
    juce::ThreadPool fileWorker { 1, 0, juce::Thread::Priority::low };
    std::atomic<bool> resetOutputEnvelope { false };
    mutable juce::CriticalSection stateLock;
    std::unique_ptr<std::array<SlotState,slotCount>> slotStorage=std::make_unique<std::array<SlotState,slotCount>>();
    std::array<SlotState,slotCount>& slots=*slotStorage;
    std::atomic<int> currentSlot { 0 };
    // UI working memory only. Deliberately excluded from plugin/project/preset state.
    std::atomic<int> slotGridPosition { 0 };
    std::atomic<bool> previewToggleRequested { false };
    std::atomic<int> previewStartSlot { -1 }; // -1 = no pending start
    std::atomic<bool> previewStopRequested { false };
    std::atomic<bool> previewRestartRequested { false };
    std::atomic<bool> previewAuditionRequested { false };
    std::atomic<bool> previewSampleSetRequested { false };
    std::atomic<double> previewAuditionStartPercent { 0.0 };
    std::atomic<int> previewTargetSlot { 0 };
    std::atomic<int> previewTargetSampleIndex { 0 };
    std::atomic<uint32_t> stopVoicesMask { 0 };
    std::atomic<int> slicePreviewCommand {-1};
    std::atomic<int> slicePreviewKind {0};
    int slicePreviewSlot=-1,slicePreviewItem=-1;
    bool previewPlaying = false;
    int previewPlayingSlot = -1;
    int previewPlayingSampleIndex = -1;

    struct ImportPreviewSnapshot {
        std::shared_ptr<SharedSample> owner;
        SharedSample* sample = nullptr;
        uint64_t revision = 0;
    };
    std::array<ImportPreviewSnapshot, 3> importPreviewSnapshots;
    std::atomic<int> importPreviewMiddle { 1 };
    int importPreviewWriter = 2, importPreviewReader = 0;
    uint64_t importPreviewRevision = 0;
    std::atomic<bool> importPreviewToggleRequested { false };
    std::atomic<bool> importPreviewStopRequested { false };
    std::atomic<bool> importPreviewSeekRequested { false };
    std::atomic<double> importPreviewSeekSeconds { 0.0 };
    std::atomic<double> importPreviewPositionSeconds { 0.0 };
    std::atomic<double> importPreviewLengthSeconds { 0.0 };
    std::atomic<bool> importPreviewPlayingAtomic { false };
    std::atomic<bool> importPreviewLoopEnabled { false };
    std::atomic<double> importPreviewLoopStartSeconds { 0.0 };
    std::atomic<double> importPreviewLoopEndSeconds { 0.0 };
    bool importPreviewPlaying = false;
    double importPreviewPosition = 0.0;
    uint64_t importPreviewSeenRevision = 0;

    struct AudioSnapshot {
        std::array<lsampler::SlotAudioState, slotCount> states;
        std::array<std::array<std::shared_ptr<SharedSample>, sampleSetSize>, slotCount> owners;
    };
    // Single writer (stateLock), single audio reader. Only the writer touches owners.
    // Dirty flag and buffer index travel in the same lock-free atomic exchange.
    std::unique_ptr<std::array<AudioSnapshot,3>> snapshotStorage=std::make_unique<std::array<AudioSnapshot,3>>();
    std::array<AudioSnapshot,3>& snapshots=*snapshotStorage;
    std::atomic<int> middleSnapshot { 1 };
    int writerSnapshot = 2, readerSnapshot = 0;
    uint64_t nextRevision = 0;
    double preparedSampleRate = 44100;
    std::vector<std::shared_ptr<SharedSample>> retiredSamples;
    GlobalVoicePool voicePool;

    std::array<std::atomic<double>, lsampler::globalParameterCount> globalOutputParameters {
        std::atomic<double>{ 0.0 },     // Master Output Gain dB
        std::atomic<double>{ 1.0 },     // Output Stage: LR-608
        std::atomic<double>{ 35.0 },    // Bus Glue, human percent; JSFX equivalent 3500
        std::atomic<double>{ 0.0 },     // Bus Soft Drive, human percent; JSFX equivalent 0
        std::atomic<double>{ 0.98 }     // Output Ceiling
    };
    std::array<std::atomic<float>, 2> visualPeaks {}; // observation only; no audio feedback
    double outputGlueEnvelope = 0.0; // audio-thread state, JSFX lbpm_output_glue_env
    void applyOutputStage(juce::AudioBuffer<float>& buffer, const std::array<int, 25>& routes) noexcept;
    GlobalVoicePool libraryPreviewVoicePool;
    std::unique_ptr<std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>> libraryPreviewStorage=
        std::make_unique<std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>>();
    std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>& libraryPreviewStates=*libraryPreviewStorage;
    struct LibraryPreviewSnapshot {
        lsampler::SlotAudioState state;
        std::shared_ptr<SharedSample> owner;
    };
    std::unique_ptr<std::array<LibraryPreviewSnapshot, 3>> libraryPreviewSnapshots =
        std::make_unique<std::array<LibraryPreviewSnapshot, 3>>();
    std::atomic<int> libraryPreviewMiddle { 1 };
    int libraryPreviewWriter = 2, libraryPreviewReader = 0;
    std::atomic<bool> libraryPreviewToggleRequested { false };
    std::atomic<bool> libraryPreviewStopRequested { false };
    std::atomic<bool> libraryPreviewPlayingAtomic { false };
    bool libraryPreviewPlaying = false;
    int libraryPreviewNote = 60;
    LibraryManager library;
    SlotState slotClipboard;
    bool slotClipboardHasData = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessor)
};
