#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include "SamplerVoice.h"
#include "LibraryManager.h"
#include <array>
#include <vector>

class LSampler24AudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int slotCount = 24;

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

    bool loadSample(const juce::File& file, juce::String& error);
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
    bool importLibraryArchive(const juce::File& archiveFile, int& importedSlots, int& importedSamples, int& skippedItems, juce::String& error);

    bool copyCurrentSlot();
    bool cutCurrentSlot();
    bool pasteCurrentSlot();
    void clearCurrentSlot();
    void clearBank();

    int getCurrentSlot() const noexcept { return currentSlot.load(std::memory_order_relaxed); }
    void setCurrentSlot(int slotIndex);
    juce::String getSlotLabel(int slotIndex) const;
    juce::String getSlotName(int slotIndex) const;
    void setSlotName(int slotIndex, const juce::String& name);

    juce::File getCurrentSampleFile() const;
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
    void requestSampleBoundaryAudition(bool endBoundary);

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

    LibraryManager& getLibrary() noexcept { return library; }

private:
    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        juce::String slotName;
        bool sampleAudioModified = false;
        int thresholdStartFrame = 0;
        int thresholdEndFrame = 0;
        lsampler::SlotParameters parameters;
        lsampler::SliceState slice;
        juce::String status = "No sample loaded";
    };

    juce::ValueTree makeSlotState(int slotIndex, const juce::String& type) const;
    bool restoreSlotState(int slotIndex, const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseSlotSample(int slotIndex, juce::String& error);
    void updateThresholdWindow(int slotIndex);
    void markAudioStateDirty();
    void syncAudioStateFromSlots();

    mutable juce::CriticalSection stateLock;
    std::unique_ptr<std::array<SlotState,slotCount>> slotStorage=std::make_unique<std::array<SlotState,slotCount>>();
    std::array<SlotState,slotCount>& slots=*slotStorage;
    std::atomic<int> currentSlot { 0 };
    std::atomic<bool> previewToggleRequested { false };
    std::atomic<bool> previewStopRequested { false };
    std::atomic<bool> previewRestartRequested { false };
    std::atomic<bool> previewAuditionRequested { false };
    std::atomic<double> previewAuditionStartPercent { 0.0 };
    std::atomic<int> previewTargetSlot { 0 };
    std::atomic<uint32_t> stopVoicesMask { 0 };
    std::atomic<int> slicePreviewCommand {-1};
    std::atomic<int> slicePreviewKind {0};
    int slicePreviewSlot=-1,slicePreviewItem=-1;
    bool previewPlaying = false;
    int previewPlayingSlot = -1;

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
        std::array<std::shared_ptr<SharedSample>, slotCount> owners;
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
    double outputGlueEnvelope = 0.0; // audio-thread state, JSFX lbpm_output_glue_env
    void applyOutputStage(juce::AudioBuffer<float>& buffer, const std::array<int, 25>& routes) noexcept;
    GlobalVoicePool libraryPreviewVoicePool;
    std::unique_ptr<std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>> libraryPreviewStorage=
        std::make_unique<std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>>();
    std::array<lsampler::SlotAudioState,GlobalVoicePool::slotCount>& libraryPreviewStates=*libraryPreviewStorage;
    std::array<std::shared_ptr<SharedSample>, GlobalVoicePool::slotCount> libraryPreviewOwners {};
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
