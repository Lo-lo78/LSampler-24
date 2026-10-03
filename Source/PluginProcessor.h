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
    double getSamplePlayStart() const;
    void setSamplePlayStart(double value);
    void applyZeroCrossing(bool loopWindow, int loopIndex = 0);
    // Diagnostics used by offline regression tests, never by the screen reader.
    int getActiveVoiceCount() const noexcept { return voicePool.activeVoiceCount(); }

    LibraryManager& getLibrary() noexcept { return library; }

private:
    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        lsampler::SlotParameters parameters;
        juce::String status = "No sample loaded";
    };

    juce::ValueTree makeSlotState(int slotIndex, const juce::String& type) const;
    bool restoreSlotState(int slotIndex, const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseSlotSample(int slotIndex, juce::String& error);
    void markAudioStateDirty();
    void syncAudioStateFromSlots();

    mutable juce::CriticalSection stateLock;
    std::array<SlotState, slotCount> slots;
    std::atomic<int> currentSlot { 0 };
    std::atomic<bool> previewToggleRequested { false };
    std::atomic<bool> previewStopRequested { false };
    std::atomic<bool> previewRestartRequested { false };
    std::atomic<bool> previewAuditionRequested { false };
    std::atomic<double> previewAuditionStartPercent { 0.0 };
    std::atomic<int> previewTargetSlot { 0 };
    std::atomic<uint32_t> stopVoicesMask { 0 };
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
    std::array<AudioSnapshot, 3> snapshots;
    std::atomic<int> middleSnapshot { 1 };
    int writerSnapshot = 2, readerSnapshot = 0;
    uint64_t nextRevision = 0;
    double preparedSampleRate = 44100;
    std::vector<std::shared_ptr<SharedSample>> retiredSamples;
    GlobalVoicePool voicePool;
    GlobalVoicePool libraryPreviewVoicePool;
    std::array<lsampler::SlotAudioState, GlobalVoicePool::slotCount> libraryPreviewStates {};
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
